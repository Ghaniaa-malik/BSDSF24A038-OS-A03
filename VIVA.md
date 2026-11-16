# PA-03 Viva Revision Notes

Use these notes to review the implementation before the viva. Be ready to trace
the code and explain each operation in your own words.

## Program flow

- `main.c` initializes the `Shell`, reads a line, records it in the bounded
  history ring, handles `!n`, and dispatches ordinary input or an `if` block.
- `execute.c` splits command lists, tokenizes words and operators, expands
  variables, handles built-ins, and launches external commands.
- `shell.c` owns shared state: history, variables, and background jobs. Its
  destroy function frees the dynamically allocated strings and variable list.

## Operating-system concepts to explain

- **fork/exec/wait:** `fork()` creates a child. The child configures file
  descriptors and calls `execvp()` to replace its process image. The parent
  calls `waitpid()` for foreground work and reads the child's exit status.
- **Built-ins:** `cd`, `exit`, `history`, `jobs`, `set`, and `help` run in the
  shell process. In particular, `cd` must change the shell's own working
  directory, so it cannot run only in a child process.
- **Redirection:** `open()` obtains a file descriptor and `dup2()` connects it
  to standard input or output before `execvp()`.
- **Pipelines:** `pipe()` creates a read end and write end. Each process uses
  `dup2()` to connect the previous pipe to stdin and its next pipe to stdout.
- **Background jobs:** a trailing `&` skips the foreground wait. The shell
  stores the job PID and command; `waitpid(..., WNOHANG)` reaps completed
  children when the prompt loop runs again.
- **Command history:** a fixed-size circular buffer keeps the latest 100
  commands; `!n` indexes the displayed history sequence.
- **Readline:** when enabled, GNU Readline supplies line editing and history;
  the completion callback suggests built-ins/PATH commands at the start of a
  line and filenames elsewhere.
- **Conditionals:** the input loop collects lines through `fi`, runs the
  condition, and selects the `then` or optional `else` command list from its
  exit status.
- **Variables:** a linked list stores shell assignments. Expansion copies
  text into a new string and substitutes values before the command is run.

## Useful demonstrations

```sh
pwd
cd /
history
echo first; echo second
echo hello | tr a-z A-Z
echo saved > /tmp/pa03.txt
cat < /tmp/pa03.txt
sleep 2 &
jobs
MSG=hello
echo $MSG
set
if true
then
  echo condition-passed
else
  echo condition-failed
fi
```

The shell implements a learning subset of shell syntax. Explain its limits as
well as its supported behavior; it is not a complete POSIX shell.
