#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(void) {
    char *command = NULL;
    char *args[513];
    char *word;
    char *saveptr;
    size_t size = 0;
    ssize_t result;

    int lastStatus = 0;
    int childStatus;
    pid_t spawnPid;

    while (1) {
        printf(": ");
        fflush(stdout);

        result = getline(&command, &size, stdin);

        if (result == -1) {
            break;
        }

        // Ignore blank lines
        if (result == 1) {
            continue;
        }

        // Ignore comments
        if (command[0] == '#') {
            continue;
        }

        // Remove newline
        command[strcspn(command, "\n")] = '\0';

        // Split command into words
        int count = 0;

        word = strtok_r(command, " \t", &saveptr);

        while (word != NULL && count < 512) {
            args[count] = word;
            count++;

            word = strtok_r(NULL, " \t", &saveptr);
        }

        args[count] = NULL;

        if (count == 0) {
            continue;
        }

        // exit command
        if (strcmp(args[0], "exit") == 0) {
            break;
        }

        // cd command
        else if (strcmp(args[0], "cd") == 0) {

            if (args[1] == NULL) {
                chdir(getenv("HOME"));
            } else {
                if (chdir(args[1]) == -1) {
                    perror("cd");
                }
            }
        }

        // status command
        else if (strcmp(args[0], "status") == 0) {

            printf("exit value %d\n", lastStatus);
            fflush(stdout);
        }

        // Other commands
        else {

            spawnPid = fork();

            if (spawnPid == -1) {
                perror("fork");
                lastStatus = 1;
            }

            else if (spawnPid == 0) {

                execvp(args[0], args);

                perror(args[0]);
                exit(1);
            }

            else {
                waitpid(spawnPid, &childStatus, 0);

                if (WIFEXITED(childStatus)) {
                    lastStatus = WEXITSTATUS(childStatus);
                }
                else if (WIFSIGNALED(childStatus)) {
                    lastStatus = WTERMSIG(childStatus);

                    printf("terminated by signal %d\n", lastStatus);
                    fflush(stdout);
                }
            }
        }
    }

    free(command);
    return 0;
}
