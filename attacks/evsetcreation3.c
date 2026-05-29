#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
    
#include "performance.h"

#define WAYS 8
#define SETS 64
#define SKEWS 2
#define MEMBUS_WIDTH 16
#define CACHE_SIZE (WAYS * SETS * SKEWS * MEMBUS_WIDTH)

#define BUF_SIZE (CACHE_SIZE * 2)
#define CANDIDATE_SIZE (BUF_SIZE / MEMBUS_WIDTH)

#define THRESHOLD 50

bool probe(char* target, char** eviction_set) {
    volatile int sink;

    sink = *target;
    
    int ctr = 0;
    for (int i = 0; i < CANDIDATE_SIZE; i++) {
        if (eviction_set[i] != 0){
            ctr++;
            sink = *eviction_set[i];
        }
    }

    int t1 = rdcycle();

    asm volatile ("fence rw, rw" ::: "memory");

    sink = *target;

    asm volatile ("fence rw, rw" ::: "memory");

    int t2 = rdcycle() - t1;

    printf("t2: %d, ctr: %d\n", t2, ctr);

    return t2 > THRESHOLD;
}

int main() {
    char* target = malloc(sizeof(char));

    char* buf = malloc(sizeof(char) * BUF_SIZE);
    char** candidates = malloc(sizeof(char*) * CANDIDATE_SIZE);
    char** eviction_set = malloc(sizeof(char*) * CANDIDATE_SIZE);

    for (int i = 0; i < CANDIDATE_SIZE; i++) {
        candidates[i] = &buf[i * MEMBUS_WIDTH];
        eviction_set[i] = &buf[i * MEMBUS_WIDTH];
    }

    for (int i = 0; i < CANDIDATE_SIZE; i++) {
        for (int j = 0; j < CANDIDATE_SIZE - i; j++) {
            if (eviction_set[i + j] == 0) continue;

            eviction_set[i + j] = 0; // Remove Element

            if (!probe(target, eviction_set)) {
                // No Eviction occurred
                printf("No Eviction occurred!\n");
                eviction_set[i + j] = candidates[i + j];
                continue;
            }else {
                printf("Eviction occurred!\n");
            }
        }
    }

    printf("Eviction set found. (maybe?)\n");

    int ctr = 0;
    for (int i = 0; i < CANDIDATE_SIZE; i++) {
       if (eviction_set[i] != 0)
        ctr++;
    } 

    printf("ctr: %d\n", ctr);
}