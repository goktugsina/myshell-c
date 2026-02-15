# myshell — Unix-like Shell in C

A lightweight Unix-like shell implemented in C using POSIX system calls.

## Overview

This project implements a simple Unix shell capable of executing external commands,
handling background/foreground processes, aliases, signal control, and I/O redirection.

The implementation avoids `system()` and instead manually traverses the `PATH`
environment variable to locate executables using `execv()`.

## Features

### External Command Execution
- Uses `fork()` and `execv()`
- Manual PATH traversal
- Proper stderr error reporting

### Job Control
- Foreground execution
- Background execution using `&`
- `fg %pid` support
- Safe exit if background jobs exist

### Built-in Commands
- `alias "<command>" <name>`
- `unalias <name>`
- `alias -l`
- `exit`

### I/O Redirection
- `>`
- `>>`
- `<`
- `2>`
- Combined redirections supported

### Signal Handling
- Handles `SIGTSTP` (Ctrl+Z)
- Prevents shell from being terminated accidentally

## Build

```bash
gcc -Wall -Wextra -o myshell main.c
./myshell

## Learning Outcomes

Through this project, I gained hands-on experience with:

- Unix process lifecycle management
- Low-level system calls
- File descriptor manipulation
- Signal handling
- Building a command interpreter from scratch

This project reflects my understanding of operating system fundamentals and system-level programming.

