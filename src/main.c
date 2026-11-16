#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#include <dirent.h>
#include <unistd.h>
#endif

#ifdef USE_READLINE
static char *command_generator(const char *text, int state)
{
    static const char *builtins[] = {"cd", "exit", "help", "jobs", "history", "set", NULL};
    static size_t builtin_index, path_position;
    static char *path_copy, *directory;
    static DIR *dir;
    if (!state) {
        builtin_index = path_position = 0;
        if (dir) { closedir(dir); dir = NULL; }
        free(path_copy);
        path_copy = strdup(getenv("PATH") ? getenv("PATH") : "");
    }
    size_t n = strlen(text);
    while (builtins[builtin_index]) {
        const char *candidate = builtins[builtin_index++];
        if (!strncmp(candidate, text, n)) return strdup(candidate);
    }
    while (path_copy && path_copy[path_position]) {
        if (!dir) {
            size_t end = path_position;
            while (path_copy[end] && path_copy[end] != ':') ++end;
            free(directory);
            directory = strndup(path_copy + path_position, end - path_position);
            if (!directory || !*directory) { free(directory); directory = strdup("."); }
            path_position = path_copy[end] ? end + 1 : end;
            dir = opendir(directory);
            if (!dir) continue;
        }
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!strncmp(entry->d_name, text, n)) {
                size_t a = strlen(directory), b = strlen(entry->d_name);
                char *full = malloc(a + b + 2);
                if (!full) return NULL;
                snprintf(full, a + b + 2, "%s/%s", directory, entry->d_name);
                int executable = access(full, X_OK) == 0;
                free(full);
                if (executable) return strdup(entry->d_name);
            }
        }
        closedir(dir);
        dir = NULL;
    }
    return NULL;
}

static char **complete_input(const char *text, int start, int end)
{
    (void)end;
    if (start == 0 && !strchr(text, '/')) return rl_completion_matches(text, command_generator);
    return rl_completion_matches(text, rl_filename_completion_function);
}
#endif

static char *read_line(const char *prompt)
{
#ifdef USE_READLINE
    rl_attempted_completion_function = complete_input;
    return readline(prompt);
#else
    char *line = NULL;
    size_t capacity = 0;
    fputs(prompt, stdout);
    fflush(stdout);
    if (getline(&line, &capacity, stdin) < 0) {
        free(line);
        return NULL;
    }
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    return line;
#endif
}

static int is_blank(const char *s)
{
    while (*s) if (!isspace((unsigned char)*s++)) return 0;
    return 1;
}

static char *trim_copy(const char *s)
{
    while (isspace((unsigned char)*s)) ++s;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) --n;
    char *out = malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static int starts_word(const char *line, const char *word)
{
    size_t n = strlen(word);
    return strncmp(line, word, n) == 0 &&
           (line[n] == '\0' || isspace((unsigned char)line[n]));
}

static void free_lines(char **lines, size_t count)
{
    for (size_t i = 0; i < count; ++i) free(lines[i]);
    free(lines);
}

static int run_if_block(Shell *shell, const char *first_line)
{
    char *condition = trim_copy(first_line + 2);
    char **then_lines = NULL, **else_lines = NULL;
    size_t then_count = 0, else_count = 0, then_capacity = 0, else_capacity = 0;
    int in_else = 0, found_then = 0, found_fi = 0;
    char *line = NULL;
    if (!condition) return 1;

    while (!shell->should_exit) {
        line = read_line(in_else ? "else> " : "if> ");
        if (!line) break;
        char *trimmed = trim_copy(line);
        free(line);
        if (!trimmed) break;
        if (!strcmp(trimmed, "then")) { found_then = 1; free(trimmed); continue; }
        if (!strcmp(trimmed, "else")) { in_else = 1; free(trimmed); continue; }
        if (!strcmp(trimmed, "fi")) { found_fi = 1; free(trimmed); break; }
        if (!found_then) {
            if (*condition) {
                size_t old = strlen(condition), add = strlen(trimmed);
                char *joined = realloc(condition, old + add + 2);
                if (!joined) { free(trimmed); break; }
                condition = joined;
                condition[old] = ' ';
                memcpy(condition + old + 1, trimmed, add + 1);
            } else {
                free(condition);
                condition = trimmed;
                continue;
            }
            free(trimmed);
            continue;
        }
        size_t *count = in_else ? &else_count : &then_count;
        size_t *capacity = in_else ? &else_capacity : &then_capacity;
        char ***list = in_else ? &else_lines : &then_lines;
        if (*count == *capacity) {
            size_t next_capacity = *capacity ? *capacity * 2 : 8;
            char **grown = realloc(*list, next_capacity * sizeof(*grown));
            if (!grown) { free(trimmed); break; }
            *list = grown;
            *capacity = next_capacity;
        }
        (*list)[(*count)++] = trimmed;
    }
    int status = 1;
    if (!found_fi) fprintf(stderr, "if: missing fi\n");
    else status = shell_execute_block(shell, condition, then_lines, then_count,
                                      else_lines, else_count);
    free(condition);
    free_lines(then_lines, then_count);
    free_lines(else_lines, else_count);
    return status;
}

static void maybe_replay(Shell *shell, char **line)
{
    if ((*line)[0] != '!' || !isdigit((unsigned char)(*line)[1])) return;
    char *end = NULL;
    unsigned long number = strtoul(*line + 1, &end, 10);
    if (!end || *end || number == 0 || number > shell->history.count) {
        fprintf(stderr, "history: event not found: %s\n", *line);
        free(*line);
        *line = NULL;
        return;
    }
    size_t oldest = (shell->history.next + HISTORY_LIMIT - shell->history.count) % HISTORY_LIMIT;
    size_t slot = (oldest + (size_t)number - 1) % HISTORY_LIMIT;
    char *replacement = strdup(shell->history.items[slot]);
    free(*line);
    *line = replacement;
    if (replacement) printf("%s\n", replacement);
}

int main(void)
{
    Shell shell;
    shell_init(&shell);
    char *line;
    while (!shell.should_exit) {
        shell_reap_jobs(&shell);
        line = read_line("myshell> ");
        if (!line) break;
        maybe_replay(&shell, &line);
        if (!line) continue;
        if (!is_blank(line)) {
            shell_add_history(&shell, line);
#ifdef USE_READLINE
            add_history(line);
#endif
            if (starts_word(line, "if")) {
                shell.last_status = run_if_block(&shell, line);
            } else {
                shell.last_status = shell_run_line(&shell, line, 0);
            }
        }
        free(line);
    }
    shell_destroy(&shell);
    return 0;
}
