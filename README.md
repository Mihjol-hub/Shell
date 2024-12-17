**MyShell - A Custom Shell Implementation**

(Sin el titulo de  Shell  y objectivo que pusiste al inicio, ya que se repite)

## 1. Project Overview

This project implements a custom shell called `myshell`, designed to provide a basic command-line interface for interacting with the operating system. It supports executing both built-in commands and external programs, handling background processes, piping between commands, and managing signals. This project fulfills the requirements of the TP6 assignment, aimed at understanding process creation, management, and signal handling using the POSIX API.

## 2. Features

`myshell` supports the following key features:

*   **Execution of External Commands:** Executes system programs found in the `PATH` environment variable using `execvp`.
*   **Built-in Commands:**
    *   `cd [directory]`: Changes the current working directory. Supports changing to the home directory with `cd` or `cd ~`. **Note:** `cd -` is not supported.
    *   `exit [ -f | --force ]`: Terminates the shell.
        *   Without arguments: Exits only if no background jobs are running. Displays a warning message if background jobs are present.
        *   With `-f` or `--force`: Forces exit and terminates any active background jobs.
    *   `jobs`: Lists the currently active background jobs, showing their PID and command.
*   **Background Processes:**
    *   Allows execution of a command in the background by appending `&` at the end of the command line.
    *   Manages job status, prevents zombie processes by reaping terminated children, and notifies the user upon job termination with a message like "Background process PID exited with status X".
    *   **Limitation:**  Supports only one background job at a time for simplicity. Built-in commands are not intended to be run in the background.
*   **Signal Handling:**
    *   `SIGCHLD`:  Handled to detect the termination of child processes. This prevents zombie processes and updates the background job list. A message is displayed when a background job terminates.
    *   `SIGINT`: Interrupts the currently running foreground process (if any) without terminating the shell. `Ctrl+C` has no effect if no foreground job is running.
    *   `SIGHUP`: Terminates the shell and all associated jobs. Useful for cleanup in scenarios like terminal closure. The shell will send `SIGTERM` to all background jobs before exiting.
    *   `SIGTERM` and `SIGQUIT`: Ignored by the shell itself, matching standard shell behavior.
*   **Input Redirection:**
    *   Redirects input from a file to a command using `<`. **Limitation:** Only one input redirection is allowed per command, using the same file twice will result in unexpected behavior. Using `<` alone without a file name will produce an error message, as well as redirecting to a background task.
*   **Piping:**
    *   Supports piping between commands using `|`. Multiple pipes in a single command are supported.
*   **Error Handling:**
    *   Handles "command not found" errors (reports exit status 127).
    *   Handles `cd` errors:
        *   Reports an error if the specified directory does not exist or is not a directory.
        *   Reports an error if `cd ~` is used incorrectly.
    *   Handles input redirection errors:
        *   Reports an error if the specified file does not exist or cannot be opened.
        *   Reports an error if `<` is used without a filename.
        *   Reports an error if redirection to a background task is attempted.
    *   Handles `exit` calls:
        *   Displays a warning if `exit` is called while background jobs are running.
        *   Forces termination of background jobs with `exit -f`.

## 3. Known Issues / Limitations (sección nueva)

*   **Variable Expansion:** Currently, variable expansion (e.g., `echo $PATH`) does not work correctly. The shell does not expand environment variables.
*   **Command Parsing:** Command parsing, especially with combinations of `&`, `;`, and redirection, can lead to unexpected behavior or errors.
*   **Redirection Error Handling:** While basic input redirection errors are handled, some edge cases with multiple redirections might not be handled correctly.
*   **`cd ~` Implementation:** The `cd ~` implementation is basic and may not handle all edge cases correctly.

## 4. Building the Project

To build the project, navigate to the directory containing the source code (`myshell.c`) and run the following command in the terminal:

```bash
gcc -o myshell myshell.c
```

This command will compile `myshell.c` and output an executable named `myshell`.

## 5. Usage

Execute the shell from the terminal using:

```bash
./myshell
```

You will be presented with the prompt `myshell>`. You can then enter commands.

## 6. Bibliography

*   Making your own Linux Shell in C: [https://www.geeksforgeeks.org/making-linux-shell-c/](https://www.geeksforgeeks.org/making-linux-shell-c/)
*   The Linux Programmer's Guide: [https://tldp.org/LDP/lpg/](https://tldp.org/LDP/lpg/)
*   Tutorial - Write a Shell in C: [https://brennan.io/2015/01/16/write-a-shell-in-c/](https://brennan.io/2015/01/16/write-a-shell-in-c/)
