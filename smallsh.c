#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define MAX_ARGS 513
#define MAX_BG 100

volatile sig_atomic_t foregroundOnly = 0;
volatile sig_atomic_t foregroundRunning = 0;
volatile sig_atomic_t toggleMessage = 0;

void handle_SIGTSTP(int signo) {
    if (foregroundOnly == 0) {
        foregroundOnly = 1;
    } else {
        foregroundOnly = 0;
    }

    if (foregroundRunning == 0) {
        if (foregroundOnly == 1) {
            char *message = "\nEntering foreground-only mode (& is now ignored)\n";
            write(STDOUT_FILENO, message, strlen(message));
        } else {
            char *message = "\nExiting foreground-only mode\n";
            write(STDOUT_FILENO, message, strlen(message));
        }
    } else {
        toggleMessage = 1;
    }
}

int main(void) {
    char *command = NULL;
    char *args[MAX_ARGS];
    char *word;
    char *saveptr;
    size_t size = 0;
    ssize_t result;

    int lastStatus = 0;
    int childStatus;
    pid_t spawnPid;

    int background = 0;
    char *inputFile = NULL;
    char *outputFile = NULL;

    pid_t bgPids[MAX_BG];
    int bgCount = 0;

    // Set up SIGINT for the parent
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Set up SIGTSTP for the parent
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while (1) {

        // Check background processes
        for (int i = 0; i < bgCount; i++) {
            spawnPid = waitpid(bgPids[i], &childStatus, WNOHANG);

            if (spawnPid > 0) {
                if (WIFEXITED(childStatus)) {
                    printf("background pid %d is done: exit value %d\n",bgPids[i], WEXITSTATUS(childStatus));
                } else if (WIFSIGNALED(childStatus)) {  printf("background pid %d is done: terminated by signal %d\n", bgPids[i], WTERMSIG(childStatus));
                }

                fflush(stdout);

                bgPids[i] = bgPids[bgCount - 1];
                bgCount--;
                i--;
            }
        }

        printf (": ");
        fflush(stdout);

        result = getline(&command, &size, stdin );

        if (result == -1) {
            break;
        }

        // Ignore blank lines
        if (result == 1) {
            continue;
        }

        // Remove newline
        command[strcspn(command, "\n")] = '\0';

        // Ignore comments
        word = command;
        while (*word == ' ' || *word == '\t') {
            word++;
        }

        if (*word == '#') {
            continue;
        }

        // Split command into words
        int count = 0;

        word = strtok_r(command, " \t", &saveptr);

        while (word != NULL && count < MAX_ARGS - 1) {

            // Replace $$ with shell PID
            char newWord[2048];
            char pidString[30];

            sprintf ( pidString, "%d", getpid());

            int j = 0;
            int k = 0;

            while (word[j] != '\0' && k < 2046) {
                if (word[j] == '$' && word[j + 1] == '$') {
                    for (int x = 0; pidString[x] != '\0' && k < 2047; x++) {
                        newWord[k++] = pidString[x];
                    }
                    j += 2;
                } else {
                    newWord[k++] = word[j++];
                }
            }

            newWord[k] = '\0';
            args[count] = strdup(newWord);
            count++;

            word = strtok_r(NULL, " \t", &saveptr);
        }

        args[count] = NULL;

        if (count == 0) {
            continue;
        }

        // Check for background command
        background = 0;

        if (strcmp (args[count - 1], "&") == 0) {
            if (foregroundOnly == 0) {
                background = 1;
            }

            free(args[count - 1]);
            args[count - 1] = NULL;
            count--;
        }

        if (count == 0) {
            continue;
        }

        // Find input and output redirection
        inputFile = NULL;
        outputFile = NULL;

        for (int i = 0; i < count; i++) {

            if (strcmp (args[i], "<") == 0) {
                if (i + 1 < count) {
                    inputFile = args[i + 1];
                    free(args[i]);

                    for (int j = i; j < count - 2; j++) {
                        args[j] = args[j + 2];
                    }

                    count -= 2;
                    i--;
                    args[count] = NULL;
                }
            }

            else if (strcmp( args[i], ">") == 0) {
                if (i + 1 < count) {
                    outputFile = args[i + 1];
                    free(args[i]);

                    for (int j = i; j < count - 2; j++) {
                        args[j] = args[j + 2];
                    }

                    count -= 2;
                    i--;
                    args[count] = NULL;
                }
            }
        }

        // exit command
        if (strcmp(args[0], "exit") == 0) {
            for (int i = 0; i < count; i++) {
                free(args[i]);
            }

            for (int i = 0; i < bgCount; i++) {
                kill( bgPids[i], SIGTERM);
            }

            while (waitpid (-1, NULL, 0) > 0) {
            }

            break;
        }

        // cd command
        else if (strcmp(args[0], "cd") == 0) {

            if (args[1] == NULL) {
                chdir(getenv("HOME"));
            } else {
                if  (chdir ( args[1]) == -1) {
                    perror("cd");
                }
            }
        }

        // status command
        else if (strcmp(args[0], "status") == 0) {

            if ( WIFSIGNALED(lastStatus)) {
                printf("terminated by signal %d\n",  WTERMSIG(lastStatus));
            } else {
                printf("exit value %d\n", WEXITSTATUS(lastStatus));
            }

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

                // Set child signal behavior
                struct sigaction childSIGINT = {0};
                struct sigaction childSIGTSTP = {0};

                if (background == 1) {
                    childSIGINT.sa_handler = SIG_IGN;
                } else {
                    childSIGINT.sa_handler = SIG_DFL;
                }

                childSIGTSTP.sa_handler = SIG_IGN;

                sigfillset( &childSIGINT.sa_mask);
                sigfillset( &childSIGTSTP.sa_mask);

                sigaction (SIGINT, &childSIGINT, NULL);
                sigaction(SIGTSTP, &childSIGTSTP, NULL);

                // Input redirection
                if (inputFile != NULL) {
                    int inputFD = open(inputFile, O_RDONLY);

                    if (inputFD == -1) {
                        perror("cannot open input file");
                        exit(1);
                    }

                    dup2(inputFD, STDIN_FILENO);
                    close(inputFD);
                }

                else if (background == 1) {
                    int inputFD = open("/dev/null", O_RDONLY);
                    dup2( inputFD, STDIN_FILENO);
                    close(inputFD);
                }

                // Output redirection
                if (outputFile != NULL) {
                    int outputFD = open(outputFile,
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);

                    if (outputFD == -1) {
                        perror( "cannot open output file");
                        exit(1);
                    }

                    dup2(outputFD, STDOUT_FILENO);
                    close( outputFD);
                }

                else if (background == 1) {
                    int outputFD = open("/dev/null", O_WRONLY);
                    dup2(outputFD, STDOUT_FILENO);
                    close(outputFD);
                }

                execvp(args[0], args);

                perror(args[0]);
                exit(1);
            }

            else {

                if (background == 1) {

                    printf("background pid is %d\n", spawnPid);
                    fflush(stdout);

                    if (bgCount < MAX_BG) {
                        bgPids[bgCount] = spawnPid;
                        bgCount++;
                    }
                }

                else {

                    foregroundRunning = 1;

                    while ( waitpid(spawnPid, &childStatus, 0) == -1) {
                        if (errno != EINTR) {
                            perror("waitpid");
                            break;
                        }
                    }

                    foregroundRunning = 0;

                    lastStatus = childStatus;

                    if (WIFSIGNALED(childStatus)) {
                        printf ( "terminated by signal %d\n", WTERMSIG(childStatus));
                        fflush( stdout);
                    }

                    // Print pending SIGTSTP message
                    if (toggleMessage == 1) {
                        if (foregroundOnly == 1) {
                            printf ("Entering foreground-only mode (& is now ignored)\n");
                        } else {
                            printf( "Exiting foreground-only mode\n");
                        }

                        fflush(stdout);
                        toggleMessage = 0;
                    }
                }
            }
        }

        // Free allocated memory
        for (int i = 0; i < count; i++) {
            free(args[i]);
        }
    }

    free( command);
    return 0;
}
