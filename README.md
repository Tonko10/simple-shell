# simple-shell

A minimal Unix shell written in C. It covers the core of what a real shell does: parsing a command line, spawning child processes, running them in the foreground or background, and managing signals and terminal ownership.

## Features

**Built-in commands** (implemented from scratch, no external binaries used):

| Command | Description |
|---|---|
| `cd <dir>` | Change the current directory (`chdir`) |
| `exit` | Kill all remaining background processes with `SIGKILL` and quit |
| `ps` | List the background processes started by this shell that are still running |
| `kill <pid> <signal>` | Send a signal to a process, only if it was started by this shell |
| `history` | Print all commands entered so far, numbered |
| `!<n>` | Re-run command number `n` from the history |

**Running programs**

- Any other command is run as an external program (`fork` + `execvp`).
- Append `&` to run a program in the background; otherwise the shell waits for it to finish.
- Each child is placed in its own process group (`setpgid`), so signals sent to the shell are not delivered to it by accident.
- Foreground programs get ownership of the terminal (`tcsetpgrp`), which lets interactive programs read from the keyboard. The shell takes it back when they finish.
- `Ctrl+C` (`SIGINT`) is caught by the shell and forwarded to the foreground process only, so the shell itself keeps running.
- A `SIGCHLD` handler reaps finished background processes and updates the process table.
- `SIGTTOU` is ignored so the shell can reclaim the terminal without being stopped.
- Terminal settings are saved at startup and restored after each foreground program (`tcgetattr` / `tcsetattr`).

## Build and run

Requires a POSIX system (Linux, macOS, WSL) and `gcc`.

```bash
gcc -Wall -o ljuska ljuska.c
./ljuska
```

## Example session

```
/home/student$ sleep 100 &
/home/student$ ps
PID	ime
4242	sleep
/home/student$ kill 4242 15
Proces 4242 je zavrsio.
/home/student$ history
  1  sleep 100 &
  2  ps
  3  kill 4242 15
  4  history
/home/student$ !2
ps
PID	ime
/home/student$ exit
```

The PIDs above are only illustrative.

## System interfaces used

`fork`, `execvp`, `waitpid`, `kill`, `setpgid`, `getpgid`, `tcsetpgrp`, `tcgetattr`, `tcsetattr`, `sigaction`, `chdir`, `getcwd`.

## Limitations

This is a teaching project, so it keeps things simple:

- No pipes, redirection, variables or quoting.
- Fixed limits: 100 tracked background processes, 1000 history entries, 127-character command lines, and at most 4 words per command.
- Foreground jobs cannot be moved to the background or back (`fg`, `bg` and `jobs` are not implemented).

## Project layout

```
ljuska.c    # the whole shell in a single source file
```