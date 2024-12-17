#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGS 64
#define MAX_JOBS 100

// Job structure to track background processes
typedef struct {
    pid_t pid;
    char command[MAX_COMMAND_LENGTH];
    int status; // 0 = running, 1 = completed
} Job;

// Global variables for signal and job management
pid_t foreground_pid = -1;
Job background_jobs[MAX_JOBS];
int job_count = 0;

// Function prototypes
void handle_signals(void);
int parse_command(char *command, char **args, int *background, int *redirect_input);
int execute_command(char ***args_ptr, int background, int redirect_input); 
int execute_piped_command(char ***args);          
void builtin_cd(char **args);
void add_background_job(pid_t pid, char *command);
void remove_background_job(pid_t pid);
void list_background_jobs(void);
int validate_command(char **args);

// Function to count pipes in the command
int count_pipes(char ***args) {
    int pipe_count = 0;
    int i = 0;

    // Check if args is valid
    if (args == NULL || *args == NULL) {
        return 0;
    }

    // Iterate throughthe command arguments
    while ((*args)[i] != NULL) {
        if (strcmp((*args)[i], "|") == 0) {
            pipe_count++;
        }
        i++;
    }
    
    return pipe_count;
}

// Validate command before execution
int validate_command(char **args) {
    // 1. Check for NULL input
    if (args == NULL) {
        fprintf(stderr, "Error: Invalid command arguments\n");
        return 0;
    }

    // 2. Check if command is empty
    if (args[0] == NULL) {
        fprintf(stderr, "Error: Empty command\n");
        return 0;
    }

    // 3. Comprehensive command length check
    if (strlen(args[0]) == 0 || strlen(args[0]) > MAX_COMMAND_LENGTH) {
        fprintf(stderr, "Error: Invalid command length (must be 1-%d characters)\n", MAX_COMMAND_LENGTH);
        return 0;
    }

    // 4. Check for valid command characters
    for (int i = 0; args[0][i] != '\0'; i++) {
        if (!isalnum(args[0][i]) && 
            args[0][i] != '_' && 
            args[0][i] != '-' && 
            args[0][i] != '.') {
            fprintf(stderr, "Error: Invalid characters in command name\n");
            return 0;
        }
    }

    // 5. Check argument count and length
    int arg_count = 0;
    for (int i = 0; i < MAX_ARGS; i++) {
        if (args[i] == NULL) break;

        // Check individual argument length
        if (strlen(args[i]) > MAX_COMMAND_LENGTH) {
            fprintf(stderr, "Error: Argument too long\n");
            return 0;
        }

        arg_count++;
    }

    // 6. Specific built-in command validations
    if (strcmp(args[0], "cd") == 0) {
        if (arg_count > 2) {
            fprintf(stderr, "Error: cd takes at most one argument\n");
            return 0;
        }
    }

    // 7. Additional built-in command checks
    if (strcmp(args[0], "exit") == 0 || 
        strcmp(args[0], "jobs") == 0) {
        if (arg_count > 1) {
            fprintf(stderr, "Error: %s command does not take arguments\n", args[0]);
            return 0;
        }
    }

    return 1;
}

// Signal handler function
void signal_handler(int signum) {
    int status;
    pid_t terminated_pid;

    switch(signum) {
        case SIGCHLD:
            // Reap zombie processes and update job status
            while ((terminated_pid = waitpid(-1, &status, WNOHANG)) > 0) {
                // Log child process termination
                if (WIFEXITED(status)) {
                    printf("Background process %d exited with status %d\n", 
                           terminated_pid, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    printf("Background process %d terminated by signal %d\n", 
                           terminated_pid, WTERMSIG(status));
                }
                remove_background_job(terminated_pid);
            }
            break;
        
        case SIGINT:
            // Interrupt foreground process if running
            if (foreground_pid > 0) {
                kill(foreground_pid, SIGINT);
                printf("\nInterrupted process %d\n", foreground_pid);
                foreground_pid = -1;
            }
            printf("\nmyshell> ");
            fflush(stdout);
            break;
        
        case SIGHUP:
            // Terminate shell and all jobs
            printf("\nReceived SIGHUP. Terminating shell and jobs.\n");
            for (int i = 0; i < job_count; i++) {
                if (background_jobs[i].pid != 0) {
                    kill(background_jobs[i].pid, SIGTERM);
                }
            }
            exit(0);
    }
}

// Setup signal handling
void handle_signals(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    // Register signal handlers
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

// Parse command into arguments
int parse_command(char *command, char **args, int *background, int *redirect_input) {
    char *token;
    int arg_count = 0;

    // Remove newline
    command[strcspn(command, "\n")] = 0;

    // Check for background job
    *background = 0;
    if (command[strlen(command) - 1] == '&') {
        *background = 1;
        command[strlen(command) - 1] = 0;
    }

    // Trim trailing whitespace
    while (strlen(command) > 0 && command[strlen(command) - 1] == ' ') {
        command[strlen(command) - 1] = 0;
    }

    // Check for empty command
    if (strlen(command) == 0) {
        return 0;
    }

    // Tokenize command
    token = strtok(command, " ");
    while (token != NULL && arg_count < MAX_ARGS - 1) {
        args[arg_count++] = token;
        token = strtok(NULL, " ");
    }
    args[arg_count] = NULL;

    // Validate pipe placement
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            if (i == 0 || args[i + 1] == NULL) {
                fprintf(stderr, "Error: Invalid pipe placement\n");
                return 0;
            }
        }
    }

    // Detect input redirection, if any
    *redirect_input = 0;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            *redirect_input = 1;
            
            if(args[i+1] == NULL) {
                fprintf(stderr, "Error: Missing file name for input redirection\n");
                return 0;
            }
            
            args[i] = NULL;
            strcpy(command, args[i+1]);
            args[i+1] = NULL;
            break;
        }
    }

    return 1;
}

// Implement CD builtin with error checking
void builtin_cd(char **args) {
    // Check if path exists and is accessible
    if (args[1] != NULL) {
        struct stat path_stat;
        if (stat(args[1], &path_stat) != 0) {
            fprintf(stderr, "cd: %s: No such file or directory\n", args[1]);
            return;
        }
        
        if (!S_ISDIR(path_stat.st_mode)) {
            fprintf(stderr, "cd: %s: Not a directory\n", args[1]);
            return;
        }
    }

    // Attempt to change directory
    if (args[1] == NULL) {
        // Default to home directory if no argument
        char *home_dir = getenv("HOME");
        if (home_dir == NULL) {
            fprintf(stderr, "cd: Unable to find home directory\n");
            return;
        }
        chdir(home_dir);
    } else {
        if (chdir(args[1]) != 0) {
            perror("cd");
        }
    }
}

// Add background job to tracking list
void add_background_job(pid_t pid, char *command) {
    if (job_count < MAX_JOBS) {
        background_jobs[job_count].pid = pid;
        strncpy(background_jobs[job_count].command, command, MAX_COMMAND_LENGTH - 1);
        background_jobs[job_count].status = 0;
        job_count++;
        printf("[%d] %d\n", job_count, pid);
    } else {
        fprintf(stderr, "Maximum number of background jobs reached\n");
    }
}

// Remove background job from tracking list
void remove_background_job(pid_t pid) {
    for (int i = 0; i < job_count; i++) {
        if (background_jobs[i].pid == pid) {
            // Shift remaining jobs
            for (int j = i; j < job_count - 1; j++) {
                background_jobs[j] = background_jobs[j + 1];
            }
            job_count--;
            break;
        }
    }
}

// List all background jobs
void list_background_jobs(void) {
    printf("Background Jobs:\n");
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %d %s\n", i + 1, background_jobs[i].pid, background_jobs[i].command);
    }
}

// Execute command with enhanced error handling /
int execute_command(char ***args_ptr, int background, int redirect_input) {
    // Dereference the pointer to get the actual arguments
    char **args = *args_ptr;
    
    // Existing validation
    if (!validate_command(args)) {
        return -1;
    }

    // Handle built-in commands BEFORE  forking
    if (strcmp(args[0], "exit") == 0) {
        // check for force exit
        if (args[1] && (strcmp(args[1], "-f") == 0 || strcmp(args[1], "--force") == 0)) {
            //Terminate all background jobs
            for (int i = 0; i < job_count; i++) {
                kill(background_jobs[i].pid, SIGTERM);
            }
            exit(0);
        }
        
        // check if background jobs are running
        if(job_count > 0) {
            fprintf(stderr, "Warning: %d background jobs are stil runing.\n", job_count);
            fprintf(stderr, "Please use 'jobs' to list or terminate the before exiting.\n");
            return -1;
        }
        exit(0);
    }

    // Existing built-in commands 
    if (strcmp(args[0], "cd") == 0) {
        builtin_cd(args);
        return 0;
    }

    if (strcmp(args[0], "jobs") == 0){
        list_background_jobs();
        return 0;
    }

    // Fork and execute with enhanced error handling
    pid_t pid = fork();
    
    if (pid < 0) {
        // Fork failure
        perror("Error: Failed to create new process");
        return -1;
    } else if (pid == 0) {
        // Child process execution logic
        if (background) {
            // Case with both background AND input redirection
            if (redirect_input) {
                fprintf(stderr, "Error: Cannot run background job with input redirection\n");
                exit(1);
            }

            // Redirect background job input
            int dev_null = open("/dev/null", O_RDWR);
            if (dev_null == -1) {
                perror("Error: Failed to redirect background job input");
                exit(1);
            }
            dup2(dev_null, STDIN_FILENO);
            close(dev_null);
        } else if (redirect_input) {
            // Open input file
            int fd = open(args[0], O_RDONLY);
            if (fd == -1) {
                fprintf(stderr, "Error: Failed to open %s for input redirection: %s\n", 
                        args[0], strerror(errno));
                exit(1);
            }

            // Duplicate file descriptor for STDIN
            if(dup2(fd, STDIN_FILENO) == -1) {
                fprintf(stderr, "Error: Failed to duplicate file descriptor for input: %s\n", 
                        strerror(errno));
                close(fd); // clean descriptor before exit
                exit(1);
            }
            close(fd);
        }

        // Execute with path search
        execvp(args[0], args);

        // If execvp fails Error handling
        switch(errno) {
            case EACCES:
                fprintf(stderr, "Error: Permission denied for command '%s'\n", args[0]);
                break;
            case ENOENT:
                fprintf(stderr, "Error: Command '%s' not found\n", args[0]);
                break;
            default:
                fprintf(stderr, "Error: Failed to execute command '%s': %s\n", 
                        args[0], strerror(errno));
        }
        exit(127); // Standard command not found exit code
    } else {
        // Parent process handling
        if (!background) {
            foreground_pid = pid;
            int status;
            waitpid(pid, &status, 0);
            foreground_pid = -1;

            // Detailed status reporting
            if (WIFEXITED(status)) {
                int exit_status = WEXITSTATUS(status);
                if (exit_status != 0) {
                    fprintf(stderr, "Command exited with status %d\n", exit_status);
                }
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "Command terminated by signal %d\n", WTERMSIG(status));
            }
        } else {
            add_background_job(pid, args[0]);
        }
    }

    return 0;
}

// Enhanced command execution to support piping with 3-level pointer
int execute_piped_command(char ***args) {
    // Dereference args to work with the command arguments
    char **flat_args = *args;

    int pipe_count = count_pipes(args);
    if (pipe_count == 0) {
        // No pipes, use existing execute_command
        // will require to update it as we now have 3 arg
        //return execute_command(args, 0);
        return 0;

    }

    // Prepare for piped commands
    int pipefds[2 * pipe_count];
    pid_t pid;
    int status;
    int command_start = 0;
    int i, j;

    // Create pipe file descriptors
    for (i = 0; i < pipe_count; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("Error: Failed to create pipe");
            return -1;
        }
    }

    // Execute piped commands
    for (i = 0; i <= pipe_count; i++) {
        // Prepare command arguments for this command segment
        char *command_args[MAX_ARGS];
        int arg_count = 0;

        // Extract command arguments segment
        for (j =  command_start; flat_args[j] != NULL; j++) {
            if (strcmp(flat_args[j], "|") == 0) {
                command_args[arg_count] = NULL;
                break;
            }
            command_args[arg_count++] = flat_args[j];
        }
        command_args[arg_count] = NULL;

        pid = fork();
        if (pid == 0) {
            // Check for 'jobs'
            if (strcmp(command_args[0], "jobs") == 0) {
                list_background_jobs();
                exit(0);
            }
            // child process
            if (i < pipe_count) {
                // Not the last command, redirect stdout to pipe
                if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) < 0) {
                    perror("Error: Failed to duplicate pipe write descriptor (dup2)");
                    exit(1);
                }
            }

            if (i > 0) {
                // Not the first command, redirect stdin from previous pipe
                if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) < 0) {
                    perror("Error: Failed to duplicate pipe read descriptor (dup2)");
                    exit(1);
                }
            }

            // Close all pipe file descriptors
            for (j = 0; j < 2 * pipe_count; j++) {
                close(pipefds[j]);
            }

            // Execute command
            execvp(command_args[0], command_args);

            // If execvp fails
            perror("Error: Failed to execute command (execvp)");
            exit(1);
        } else if (pid < 0) {
            // Fork failure
            perror("Error: Failed to create new process (fork)");
            return -1;
        }

        // Move to next command segment
        command_start = j + 1;
    }

    // Parent process closes pipe file descriptors
    for (i = 0; i < 2 * pipe_count; i++) {
        close(pipefds[i]);
    }

    // Wait for all child processes to complete
    for (i = 0; i <= pipe_count; i++) {
        wait(&status);
    }

    return 0;
}

int main() {
    char command[MAX_COMMAND_LENGTH];
    char *args[MAX_ARGS];
    int background, redirect_input;

    // Setup signal handling
    handle_signals();

    // Main shell loop
    while (1) {
        // Print prompt
        printf("myshell> ");
        fflush(stdout);
        
        // Read command
        if (fgets(command, sizeof(command), stdin) == NULL) break;

        // Skip empty lines
        if (strlen(command) <= 1) continue;

        // Parse command
        if (!parse_command(command, args, &background, &redirect_input)) continue;

        // Execute command
        // execute_command(&args, background); // Corrected call
        char **args_ptr = args;
        if (count_pipes(&args_ptr) > 0) {
            execute_piped_command(&args_ptr);
        } else {
            execute_command(&args_ptr, background, redirect_input); // Corrected call
        }
    }

    return 0;
}