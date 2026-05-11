#include <stdlib.h>
#include <stdio.h>

#define MEM_SZ 128

static volatile bool keepRunning = true;


int main() {
    printf("Starting pressure test...\n");

    char* buff = malloc(MEM_SZ * sizeof(char));

    volatile char sink;

    int i = 0;
    while (keepRunning) sink = buff[i++ % MEM_SZ];

    free(buff);

    return 0;
}