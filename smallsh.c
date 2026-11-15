#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *command = NULL;
    size_t size = 0;
    ssize_t result;

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

        printf("You entered: %s\n", command);
    }

    free(command);
    return 0;
}
