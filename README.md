# BSDSF24A038-OS-A03

A small Unix-style shell written in C for Operating Systems Programming Assignment 03.
It demonstrates process creation, built-in commands, command history, Readline,
redirection, pipelines, background jobs, conditional blocks, and shell variables.

## Build and run

On Linux with GCC, Make, and GNU Readline development headers installed:

```sh
make
./bin/myshell
```

Use `make clean` to remove generated files. Readline is linked with `-lreadline`.

## Supported features

- External commands through `fork`, `execvp`, and `waitpid`
- Built-ins: `cd`, `exit`, `help`, `jobs`, `history`, and `set`
- Up to 100 remembered commands and `!n` replay
- Readline editing, arrow-key history, and tab completion
- `<` and `>` redirection, pipelines, `;` command lists, and `&` background jobs
- Multi-line `if command / then / else / fi` blocks
- `NAME=value` assignments and `$NAME` expansion

The shell uses Unix process and file-descriptor APIs and is intended to run on Linux.
