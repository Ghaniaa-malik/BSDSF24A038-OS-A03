#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char *copy_string(const char *s)
{
    char *p = strdup(s ? s : "");
    if (!p) perror("malloc");
    return p;
}

void shell_init(Shell *shell)
{
    memset(shell, 0, sizeof(*shell));
    shell->last_status = 0;
}

void shell_destroy(Shell *shell)
{
    for (size_t i = 0; i < HISTORY_LIMIT; ++i) free(shell->history.items[i]);
    for (size_t i = 0; i < shell->job_count; ++i) free(shell->jobs[i].command);
    Variable *v = shell->variables;
    while (v) {
        Variable *next = v->next;
        free(v->name);
        free(v->value);
        free(v);
        v = next;
    }
}

void shell_add_history(Shell *shell, const char *line)
{
    size_t n = strlen(line);
    if (!n) return;
    free(shell->history.items[shell->history.next]);
    shell->history.items[shell->history.next] = copy_string(line);
    shell->history.next = (shell->history.next + 1) % HISTORY_LIMIT;
    if (shell->history.count < HISTORY_LIMIT) ++shell->history.count;
}

void shell_print_history(const Shell *shell)
{
    size_t oldest = (shell->history.next + HISTORY_LIMIT - shell->history.count) % HISTORY_LIMIT;
    for (size_t i = 0; i < shell->history.count; ++i)
        printf("%zu  %s\n", i + 1, shell->history.items[(oldest + i) % HISTORY_LIMIT]);
}

void shell_set_variable(Shell *shell, const char *name, const char *value)
{
    for (Variable *v = shell->variables; v; v = v->next) {
        if (!strcmp(v->name, name)) {
            char *replacement = copy_string(value);
            if (replacement) { free(v->value); v->value = replacement; }
            return;
        }
    }
    Variable *v = calloc(1, sizeof(*v));
    if (!v) { perror("calloc"); return; }
    v->name = copy_string(name);
    v->value = copy_string(value);
    if (!v->name || !v->value) { free(v->name); free(v->value); free(v); return; }
    v->next = shell->variables;
    shell->variables = v;
}

const char *shell_get_variable(const Shell *shell, const char *name)
{
    for (Variable *v = shell->variables; v; v = v->next)
        if (!strcmp(v->name, name)) return v->value;
    return getenv(name);
}

char *shell_expand(const Shell *shell, const char *text)
{
    size_t cap = strlen(text) + 1, len = 0;
    char *out = malloc(cap);
    if (!out) return NULL;
    for (size_t i = 0; text[i];) {
        if (text[i] == '$') {
            size_t start = ++i;
            if (text[i] == '{') {
                start = ++i;
                while (text[i] && text[i] != '}') ++i;
            } else {
                while (isalnum((unsigned char)text[i]) || text[i] == '_') ++i;
            }
            size_t n = i - start;
            char *name = strndup(text + start, n);
            const char *value = name ? shell_get_variable(shell, name) : NULL;
            if (value) {
                size_t add = strlen(value);
                while (len + add + 1 > cap) { cap *= 2; out = realloc(out, cap); if (!out) { free(name); return NULL; } }
                memcpy(out + len, value, add); len += add;
            }
            free(name);
            if (text[i] == '}') ++i;
        } else {
            if (len + 2 > cap) { cap *= 2; out = realloc(out, cap); if (!out) return NULL; }
            out[len++] = text[i++];
        }
    }
    out[len] = '\0';
    return out;
}

void shell_reap_jobs(Shell *shell)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (size_t i = 0; i < shell->job_count; ++i) {
            if (shell->jobs[i].pid == pid) {
                fprintf(stderr, "[%ld] finished: %s\n", (long)pid, shell->jobs[i].command);
                free(shell->jobs[i].command);
                shell->jobs[i] = shell->jobs[shell->job_count - 1];
                --shell->job_count;
                break;
            }
        }
    }
}

