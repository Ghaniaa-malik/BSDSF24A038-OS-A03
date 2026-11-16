#ifndef MYSHELL_H
#define MYSHELL_H

#include <stddef.h>
#include <sys/types.h>

#define HISTORY_LIMIT 100
#define JOB_LIMIT 128
#define TOKEN_LIMIT 256
#define LINE_LIMIT 8192

typedef struct {
    char *items[HISTORY_LIMIT];
    size_t count;
    size_t next;
} History;

typedef struct {
    pid_t pid;
    char *command;
} Job;

typedef struct Variable {
    char *name;
    char *value;
    struct Variable *next;
} Variable;

typedef struct {
    History history;
    Job jobs[JOB_LIMIT];
    size_t job_count;
    Variable *variables;
    int should_exit;
    int last_status;
} Shell;

void shell_init(Shell *shell);
void shell_destroy(Shell *shell);
void shell_reap_jobs(Shell *shell);
int shell_run_line(Shell *shell, const char *line, int remember);
int shell_execute_command(Shell *shell, const char *line, int background);
int shell_execute_block(Shell *shell, const char *condition,
                        char **then_lines, size_t then_count,
                        char **else_lines, size_t else_count);
void shell_add_history(Shell *shell, const char *line);
void shell_print_history(const Shell *shell);
void shell_set_variable(Shell *shell, const char *name, const char *value);
const char *shell_get_variable(const Shell *shell, const char *name);
char *shell_expand(const Shell *shell, const char *text);

#endif
