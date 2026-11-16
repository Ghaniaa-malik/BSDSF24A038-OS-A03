#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static int is_name_start(unsigned char c) { return isalpha(c) || c == '_'; }
static int is_name_char(unsigned char c) { return isalnum(c) || c == '_'; }

static int is_assignment(const char *s, char **name, char **value)
{
    const char *eq = strchr(s, '=');
    if (!eq || eq == s || !is_name_start((unsigned char)s[0])) return 0;
    for (const char *p = s + 1; p < eq; ++p) if (!is_name_char((unsigned char)*p)) return 0;
    *name = strndup(s, (size_t)(eq - s));
    *value = strdup(eq + 1);
    return *name && *value;
}

static int builtin(Shell *shell, char **argv)
{
    if (!argv[0]) return 1;
    if (!strcmp(argv[0], "exit")) { shell->should_exit = 1; shell->last_status = 0; return 1; }
    if (!strcmp(argv[0], "cd")) {
        const char *path = argv[1] ? argv[1] : getenv("HOME");
        if (!path || chdir(path) < 0) { perror("cd"); shell->last_status = 1; }
        else shell->last_status = 0;
        return 1;
    }
    if (!strcmp(argv[0], "help")) {
        puts("Built-ins: cd exit help jobs history set");
        puts("Syntax: command [args] [< file] [> file] [| command] [; command] [&]");
        shell->last_status = 0;
        return 1;
    }
    if (!strcmp(argv[0], "jobs")) {
        if (!shell->job_count) puts("No active background jobs.");
        for (size_t i = 0; i < shell->job_count; ++i)
            printf("[%zu] %ld  %s\n", i + 1, (long)shell->jobs[i].pid, shell->jobs[i].command);
        shell->last_status = 0;
        return 1;
    }
    if (!strcmp(argv[0], "history")) { shell_print_history(shell); shell->last_status = 0; return 1; }
    if (!strcmp(argv[0], "set")) {
        for (Variable *v = shell->variables; v; v = v->next)
            printf("%s=%s\n", v->name, v->value);
        shell->last_status = 0;
        return 1;
    }
    char *name = NULL, *value = NULL;
    if (is_assignment(argv[0], &name, &value) && !argv[1]) {
        shell_set_variable(shell, name, value);
        free(name); free(value);
        shell->last_status = 0;
        return 1;
    }
    free(name); free(value);
    return 0;
}

/* Tokenizer keeps quoted strings together and recognizes shell operators. */
static int tokenize(const char *line, char **tokens, size_t *count)
{
    *count = 0;
    size_t i = 0, n = strlen(line);
    while (i < n) {
        while (i < n && isspace((unsigned char)line[i])) ++i;
        if (i == n) break;
        if (*count >= TOKEN_LIMIT - 1) { fprintf(stderr, "too many tokens\n"); return -1; }
        if (strchr("<>|", line[i])) {
            tokens[(*count)++] = strndup(line + i++, 1);
            continue;
        }
        size_t cap = 64, len = 0;
        char *word = malloc(cap);
        if (!word) return -1;
        char quote = 0;
        while (i < n) {
            char c = line[i];
            if (!quote && (isspace((unsigned char)c) || strchr("<>|", c))) break;
            ++i;
            if ((c == '\'' || c == '"') && (!quote || quote == c)) {
                quote = quote ? 0 : c;
                continue;
            }
            if (c == '\\' && line[i] && quote != '\'') c = line[i++];
            if (len + 2 > cap) { cap *= 2; char *g = realloc(word, cap); if (!g) { free(word); return -1; } word = g; }
            word[len++] = c;
        }
        if (quote) { fprintf(stderr, "unclosed quote\n"); free(word); return -1; }
        word[len] = '\0';
        tokens[(*count)++] = word;
    }
    tokens[*count] = NULL;
    return 0;
}

static void free_tokens(char **tokens, size_t count)
{
    for (size_t i = 0; i < count; ++i) free(tokens[i]);
}

static int wait_for(pid_t pid)
{
    int status;
    while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) { perror("waitpid"); return 1; }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int run_pipeline(Shell *shell, char **tokens, size_t count,
                        int background, const char *original)
{
    char **commands[TOKEN_LIMIT];
    size_t lengths[TOKEN_LIMIT], ncommands = 0, start = 0;
    for (size_t i = 0; i <= count; ++i) {
        if (i == count || !strcmp(tokens[i], "|")) {
            if (i == start) { fprintf(stderr, "syntax error near '|': empty command\n"); return 2; }
            commands[ncommands] = &tokens[start];
            lengths[ncommands++] = i - start;
            start = i + 1;
        }
    }
    int previous_read = -1;
    pid_t children[TOKEN_LIMIT];
    size_t child_count = 0;
    for (size_t c = 0; c < ncommands; ++c) {
        int pipefd[2] = {-1, -1};
        if (c + 1 < ncommands && pipe(pipefd) < 0) { perror("pipe"); return 1; }
        char *argv[TOKEN_LIMIT];
        size_t argc = 0;
        char *input = NULL, *output = NULL;
        for (size_t i = 0; i < lengths[c]; ++i) {
            char *token = commands[c][i];
            if (!strcmp(token, "<") || !strcmp(token, ">")) {
                if (++i >= lengths[c]) { fprintf(stderr, "redirection requires a filename\n"); return 2; }
                if (token[0] == '<') input = commands[c][i]; else output = commands[c][i];
            } else {
                if (argc >= TOKEN_LIMIT - 1) return 2;
                argv[argc++] = shell_expand(shell, token);
                if (!argv[argc - 1]) return 1;
            }
        }
        argv[argc] = NULL;
        if (c == 0 && ncommands == 1 && !background && builtin(shell, argv)) {
            for (size_t i = 0; i < argc; ++i) free(argv[i]);
            if (previous_read >= 0) close(previous_read);
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);
            return shell->last_status;
        }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            if (previous_read >= 0) { dup2(previous_read, STDIN_FILENO); close(previous_read); }
            if (pipefd[1] >= 0) dup2(pipefd[1], STDOUT_FILENO);
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);
            if (input) {
                int fd = open(input, O_RDONLY);
                if (fd < 0 || dup2(fd, STDIN_FILENO) < 0) { perror(input); _exit(1); }
                close(fd);
            }
            if (output) {
                int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd < 0 || dup2(fd, STDOUT_FILENO) < 0) { perror(output); _exit(1); }
                close(fd);
            }
            if (!argv[0]) _exit(0);
            execvp(argv[0], argv);
            perror(argv[0]);
            _exit(127);
        }
        for (size_t i = 0; i < argc; ++i) free(argv[i]);
        children[child_count++] = pid;
        if (previous_read >= 0) close(previous_read);
        if (pipefd[1] >= 0) close(pipefd[1]);
        previous_read = pipefd[0];
    }
    if (previous_read >= 0) close(previous_read);
    if (background) {
        if (shell->job_count == JOB_LIMIT) { fprintf(stderr, "jobs: job table full\n"); return 1; }
        shell->jobs[shell->job_count].pid = children[child_count - 1];
        shell->jobs[shell->job_count].command = strdup(original);
        printf("[%ld]\n", (long)children[child_count - 1]);
        ++shell->job_count;
        return 0;
    }
    int result = 0;
    for (size_t i = 0; i < child_count; ++i) result = wait_for(children[i]);
    return result;
}

int shell_execute_command(Shell *shell, const char *line, int background)
{
    char *tokens[TOKEN_LIMIT];
    size_t count;
    if (tokenize(line, tokens, &count) < 0) return 2;
    int result = run_pipeline(shell, tokens, count, background, line);
    free_tokens(tokens, count);
    shell->last_status = result;
    return result;
}

static int execute_one(Shell *shell, char *command)
{
    size_t n = strlen(command);
    while (n && isspace((unsigned char)command[n - 1])) command[--n] = '\0';
    int background = 0;
    if (n && command[n - 1] == '&') { background = 1; command[--n] = '\0'; }
    while (n && isspace((unsigned char)command[n - 1])) command[--n] = '\0';
    return shell_execute_command(shell, command, background);
}

int shell_run_line(Shell *shell, const char *line, int remember)
{
    (void)remember;
    int quote = 0, escaped = 0, status = 0;
    size_t start = 0, n = strlen(line);
    for (size_t i = 0; i <= n; ++i) {
        char c = line[i];
        if (escaped) { escaped = 0; continue; }
        if (c == '\\' && quote != '\'') { escaped = 1; continue; }
        if ((c == '\'' || c == '"') && (!quote || quote == c)) { quote = quote ? 0 : c; continue; }
        if ((c == ';' && !quote) || c == '\0') {
            char *command = strndup(line + start, i - start);
            if (!command) return 1;
            status = execute_one(shell, command);
            free(command);
            start = i + 1;
            if (shell->should_exit) break;
        }
    }
    shell->last_status = status;
    return status;
}

int shell_execute_block(Shell *shell, const char *condition,
                        char **then_lines, size_t then_count,
                        char **else_lines, size_t else_count)
{
    int status = shell_run_line(shell, condition, 0);
    char **chosen = status == 0 ? then_lines : else_lines;
    size_t count = status == 0 ? then_count : else_count;
    for (size_t i = 0; i < count && !shell->should_exit; ++i)
        status = shell_run_line(shell, chosen[i], 0);
    return status;
}
