#include <stdio.h>
#include <stdbool.h>
#include "performance.h"

#define LINE_SIZE 16
#define WAYS 2
#define SETS 2
#define SKEWS 1
#define INITIAL_SIZE (LINE_SIZE * WAYS * SETS * SKEWS * 2)
#define STRIDE (LINE_SIZE * SETS)

#define THRESHOLD 100

bool probe_evicted(char* target, char** set) {
    volatile int sink;

    sink = *target;

    int i = 0;
    while (set[STRIDE * i] != 0) {
        sink = *set[STRIDE * i++];
    }

    int t1 = rdcycle();

    asm volatile ("fence rw, rw" ::: "memory");

    sink = *target;

    asm volatile ("fence rw, rw" ::: "memory");

    int t2 = rdcycle() - t1;

    printf("t = %d (len = %d)\n", t2, i);

    return t2 > THRESHOLD;
}

int main() {
    char target;
    volatile char padding[233] = {0}; // random padding to hopefully misalign the target from the arrays
    char candidates[INITIAL_SIZE];
    char* ptrs[((INITIAL_SIZE / LINE_SIZE) + 1) * sizeof(char*) * STRIDE];
    char* eviction_set[((INITIAL_SIZE / LINE_SIZE) + 1) * sizeof(char*) * STRIDE];

    for (int i = 0; i < (INITIAL_SIZE / LINE_SIZE); ++i) {
        ptrs[i * STRIDE] = &candidates[i * LINE_SIZE];
        eviction_set[i * STRIDE] = &candidates[i * LINE_SIZE];
    }
    ptrs[STRIDE * INITIAL_SIZE / LINE_SIZE] = 0;
    eviction_set[STRIDE * INITIAL_SIZE / LINE_SIZE] = 0;

    puts("Setup done");

    // algorithm: take out the first address, test the set without it, repeat until the target is not evicted
    // then move on to the second address and repeat
    // go until there's no more addresses left

    int candidate_removal = 0;

    while (ptrs[STRIDE * candidate_removal] != 0) {
        int ptr_i = 0;
        int evs_i = 0;
        while (ptrs[ptr_i * STRIDE] != 0) {
            if (ptr_i != candidate_removal) {
                eviction_set[STRIDE * evs_i++] = ptrs[STRIDE * ptr_i];
            }
            ptr_i++;
        }
        eviction_set[STRIDE * evs_i] = 0;
        if (probe_evicted(&target, eviction_set)) {
            for (int i = 0; i < (INITIAL_SIZE / LINE_SIZE); ++i) {
                ptrs[STRIDE * i] = eviction_set[STRIDE * i];
            }
        } else {
            candidate_removal++;
        }
    }

    puts("finished");

    puts("final eviction set:");

    int idx = 0;
    int l = 0;

    while (eviction_set[STRIDE * idx] != 0) {
        printf("%p ", eviction_set[STRIDE * idx++]);
        l++;
    }

    printf("\nlength: %i\n", l);

    puts("");
}
