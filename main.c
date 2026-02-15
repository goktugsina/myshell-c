//150122030 Burak Demirer
//150122039 G�ktu� Sina Bek�io�ullar� 
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>

#define MAX_LINE 128
#define MAX_ALIASES 10
#define MAX_TOKEN_LEN 50
#define MAX_BG_PROCS 100

struct Alias {
    char name[MAX_TOKEN_LEN];
    char command[MAX_LINE];
};

struct Alias aliases[MAX_ALIASES];
int alias_count = 0;

pid_t foreground_pid = -1;  // Tracks the currently running foreground process
int bg_pids[MAX_BG_PROCS];  // Array to store PIDs of background processes
int bg_count = 0;

// Adds a PID to the background process list
void add_bg_process(pid_t pid) {
    if (bg_count < MAX_BG_PROCS) {
        bg_pids[bg_count++] = pid;
    }
}

// Removes a PID from the background process list
void remove_bg_process(pid_t pid) {
    int i, j;
    for (i = 0; i < bg_count; i++) {
        if (bg_pids[i] == pid) {
            // Shift remaining PIDs to fill the gap
            for (j = i; j < bg_count - 1; j++) {
                bg_pids[j] = bg_pids[j + 1];
            }
            bg_count--;
            break;
        }
    }
}

// Adds or updates an alias
void add_alias(char *name, char *command) {
    int i;
    // Update if alias already exists
    for (i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            strcpy(aliases[i].command, command);
            printf("Alias '%s' updated.\n", name);
            return;
        }
    }
    // Add new alias if not found and there is space
    if (alias_count < MAX_ALIASES) {
        strcpy(aliases[alias_count].name, name);
        strcpy(aliases[alias_count].command, command);
        alias_count++;
        printf("Alias '%s' added.\n", name);
    } else {
        printf("Alias memory is full.\n");
    }
}

// Removes an alias by name
void remove_alias(char *name) {
    int i, j, found = 0;
    for (i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            // Shift remaining aliases
            for (j = i; j < alias_count - 1; j++) {
                aliases[j] = aliases[j + 1];
            }
            alias_count--;
            found = 1;
            break;
        }
    }
    if (!found) {
        printf("Alias not found: %s\n", name);
    }
}

// Finds the command associated with an alias name
char *find_alias(char *name) {
    int i;
    for (i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            return aliases[i].command;
        }
    }
    return NULL;
}


// Handles >, >>, <, 2> operators
void handle_redirection(char *args[]) {
    int i = 0;
    while (args[i] != NULL) {
        // Standard Output Redirection (>)
        if (strcmp(args[i], ">") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: expected file after >\n");
                exit(1);
            }
            // Open file: Write only, Create if not exists, Truncate (overwrite)
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO); // Redirect stdout to file
            close(fd);
            args[i] = NULL; // Nullify operator to hide it from execv
        }
        // Append Output Redirection (>>)
        else if (strcmp(args[i], ">>") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: expected file after >>\n");
                exit(1);
            }
            // Open file: Write only, Create, Append to end
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            args[i] = NULL;
        }
        // Input Redirection (<)
        else if (strcmp(args[i], "<") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: expected file after <\n");
                exit(1);
            }
            int fd = open(args[i + 1], O_RDONLY);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, STDIN_FILENO); // Redirect stdin from file
            close(fd);
            args[i] = NULL;
        }
        // Error Redirection (2>)
        else if (strcmp(args[i], "2>") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: expected file after 2>\n");
                exit(1);
            }
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("open");
                exit(1);
            }
            dup2(fd, STDERR_FILENO); // Redirect stderr to file
            close(fd);
            args[i] = NULL;
        }
        i++;
    }
}

// --- SIGTSTP (Ctrl+Z) handler ---
void handle_sigtstp(int sig) {
    if (foreground_pid > 0) {
        kill(foreground_pid, SIGKILL);
        printf("\n[PID: %d] Process terminated via Signal.\n", foreground_pid);
        foreground_pid = -1;
    } else {
        // If no foreground process, just print a newline (no effect)
        printf("\n");
    }
}

// --- TOKENIZE ---
// Parses the input buffer into arguments and checks for background execution (&)

void tokenize(char inputBuffer[], int length, char *args[], int *background) {
    int i, start, ct;
    ct = 0;
    start = -1;
    *background = 0;

    for (i = 0; i < length; i++) {
        switch (inputBuffer[i]) {
            case ' ':
            case '\t':
                if (start != -1) {
                    args[ct] = &inputBuffer[start];
                    ct++;
                }
                inputBuffer[i] = '\0';
                start = -1;
                break;

            case '\n':
                if (start != -1) {
                    args[ct] = &inputBuffer[start];
                    ct++;
                }
                inputBuffer[i] = '\0';
                args[ct] = NULL;
                start = -1;
                break;

            default:
                if (start == -1) start = i;
        }
    }

    // If the line didn't end with '\n', handle the last token
    if (start != -1) {
        args[ct] = &inputBuffer[start];
        ct++;
    }

    // Check for '&' to run in background
    if (ct > 0 && strcmp(args[ct - 1], "&") == 0) {
        *background = 1;
        args[ct - 1] = NULL;
        ct--;
    }
    args[ct] = NULL;
}

// --- SETUP ---
// Reads the command from user input.

void setup(char inputBuffer[], char *args[], int *background) {
    int length;

    while (1) {
        length = read(STDIN_FILENO, inputBuffer, MAX_LINE);

        if (length == 0) {
            // EOF detected (^D), exit shell
            exit(0);
        }

        if (length < 0) {
            if (errno == EINTR) {
                // Signal received during read, try reading again
                continue;
            } else {
                perror("error reading the command");
                exit(-1);
            }
        }

        break; // Successful read
    }

    if (length < MAX_LINE) {
        inputBuffer[length] = '\0';
    }

    tokenize(inputBuffer, length, args, background);
}

// --- MAIN ---

int main(void) {
    char inputBuffer[MAX_LINE];
    int background;
    char *args[MAX_LINE / 2 + 1];
    pid_t pid;

    // Catch Ctrl+Z signal (handle only in parent)
    signal(SIGTSTP, handle_sigtstp);

    while (1) {
        background = 0;
        foreground_pid = -1;

        // Cleanup Zombie processes (check for finished background jobs)
        int status;
        pid_t ended_pid;
        while ((ended_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            remove_bg_process(ended_pid);
        }

        printf("myshell: ");
        fflush(stdout);

        setup(inputBuffer, args, &background);
        if (args[0] == NULL) {
            continue; // Empty line, prompt again
        }


        // 1. exit
        if (strcmp(args[0], "exit") == 0) {
            if (bg_count > 0) {
                fprintf(stderr, "There are %d background processes running. Cannot exit.\n", bg_count);
                continue;
            }
            exit(0);
        }

        // 2. alias
        else if (strcmp(args[0], "alias") == 0) {
            // 'alias' or 'alias -l' to list aliases
            if (args[1] == NULL || strcmp(args[1], "-l") == 0) {
                if (alias_count != 0) {
                    int i;
                    for (i = 0; i < alias_count; i++)
                        printf("%s \"%s\"\n", aliases[i].name, aliases[i].command);
                } else {
                    printf("There is no alias.\n");
                }
            }
            // Create alias: alias "command" name
            else {
                char cmd_buf[MAX_LINE] = "";
                char name_buf[MAX_TOKEN_LEN] = "";

                if (args[1][0] == '"') {
                    int len1 = strlen(args[1]);

                    // CASE 1: Single token command like "ls"
                    if (len1 > 1 && args[1][len1 - 1] == '"') {
                        args[1][len1 - 1] = '\0';
                        strcpy(cmd_buf, args[1] + 1);

                        if (args[2] != NULL) {
                            strcpy(name_buf, args[2]);
                        }
                    }
                    // CASE 2: Multi-token command like "ls -l"
                    else {
                        strcpy(cmd_buf, args[1] + 1);
                        int i = 2;
                        while (args[i] != NULL) {
                            strcat(cmd_buf, " ");
                            int len = strlen(args[i]);
                            if (len > 0 && args[i][len - 1] == '"') {
                                args[i][len - 1] = '\0';
                                strcat(cmd_buf, args[i]);
                                if (args[i + 1] != NULL) {
                                    strcpy(name_buf, args[i + 1]);
                                }
                                break;
                            }
                            strcat(cmd_buf, args[i]);
                            i++;
                        }
                    }
                } else {
                    // Optional support for aliases without quotes
                    strcpy(cmd_buf, args[1]);
                    if (args[2] != NULL)
                        strcpy(name_buf, args[2]);
                }

                if (strlen(name_buf) > 0) {
                    add_alias(name_buf, cmd_buf);
                } else {
                    fprintf(stderr, "alias: invalid format. Usage: alias \"command\" name\n");
                }
            }

            continue;
        }

        // 3. unalias
        else if (strcmp(args[0], "unalias") == 0) {
            if (args[1] != NULL) remove_alias(args[1]);
            continue;
        }

        // 4. fg %pid (Bring background process to foreground)
        else if (strcmp(args[0], "fg") == 0) {
            if (args[1] == NULL) {
                fprintf(stderr, "Usage: fg %%pid\n");
            } else {
                int target_pid;
                // Handle optional '%' character
                if (args[1][0] == '%')
                    target_pid = atoi(args[1] + 1);
                else
                    target_pid = atoi(args[1]);

                if (target_pid <= 0) {
                    fprintf(stderr, "fg: invalid pid: %s\n", args[1]);
                } else {
                    int i, found = 0;
                    // Verify PID is in our background list
                    for (i = 0; i < bg_count; i++) {
                        if (bg_pids[i] == target_pid) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        fprintf(stderr, "fg: no such background process: %d\n", target_pid);
                    } else {
                        foreground_pid = target_pid;
                        printf("Moving process to foreground PID: %d\n", target_pid);
                        waitpid(target_pid, NULL, 0); // Wait for it to finish
                        foreground_pid = -1;
                        remove_bg_process(target_pid);
                    }
                }
            }
            continue;
        }

        // 5. Alias expansion
        // If the command is an alias, replace it with the real command
        char *mapped_cmd = find_alias(args[0]);
        if (mapped_cmd != NULL) {
            strcpy(inputBuffer, mapped_cmd);
            int k;
            // Append any arguments that came after the alias
            for (k = 1; args[k] != NULL; k++) {
                strcat(inputBuffer, " ");
                strcat(inputBuffer, args[k]);
            }
            // Re-tokenize the new buffer
            tokenize(inputBuffer, strlen(inputBuffer), args, &background);
        }

        // PROCESS CREATION
        pid = fork();

        if (pid < 0) {
            perror("Fork failed");
            exit(1);
        } else if (pid == 0) {
            // --- Child Process ---
            // Handling of Ctrl+Z
            signal(SIGTSTP, SIG_DFL);

            // Handle I/O redirection
            handle_redirection(args);

            // Check if args[0] is an absolute or relative path
            if (access(args[0], X_OK) == 0) {
                execv(args[0], args);
            }

            // Search in PATH environment variable
            char *path_env = getenv("PATH");
            if (path_env != NULL) {
                char *path_copy = strdup(path_env);
                char *dir = strtok(path_copy, ":");
                while (dir != NULL) {
                    char full_path[MAX_LINE];
                    strcpy(full_path, dir);
                    strcat(full_path, "/");
                    strcat(full_path, args[0]);
                    if (access(full_path, X_OK) == 0) {
                        execv(full_path, args);
                    }
                    dir = strtok(NULL, ":");
                }
                free(path_copy);
            }

            fprintf(stderr, "myshell: command not found: %s\n", args[0]);
            exit(1);
        } else {
            // --- Parent Process ---
            if (background == 0) {
                // Foreground: wait for child
                foreground_pid = pid;
                waitpid(pid, NULL, 0);
                foreground_pid = -1;
            } else {
                // Background: add to list and continue
                add_bg_process(pid);
                printf("[Process running in background] PID: %d\n", pid);
            }
        }
    }
}
