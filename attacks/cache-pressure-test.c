#include <stdlib.h>
#include <stdio.h>

#define BUF_SZ 4096
#define REPEATS 5

static volatile bool keepRunning = true;


int main() {
    printf("Starting pressure test...\n");

    char* buff = malloc(BUF_SZ * sizeof(char));

    volatile char sink;

    for (int i = 0; i < REPEATS; i++) {
        printf("Repeat %d\n", i);

        for (int j = 0; j < BUF_SZ; j++){
            sink = buff[j];
        }
    }

    free(buff);

    return 0;
}