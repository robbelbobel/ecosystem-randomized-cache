#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "performance.h"

#define WAYS         2
#define SETS         16
#define SKEWS        2
#define MEMBUS_WIDTH    16

#define SET_STRIDE   (SETS * MEMBUS_WIDTH)
#define CACHE_SIZE   (WAYS * SETS * SKEWS * MEMBUS_WIDTH)

#define BUF_SIZE     (CACHE_SIZE * 2)
#define CANDIDATE_SIZE (BUF_SIZE / SET_STRIDE)

#define THRESHOLD    50

// Takes explicit size so null-padding and stale slots are never accessed
bool probe(char *target, char **eviction_set, int set_size) {
    volatile int sink;

    sink = *target;

    for (int i = 0; i < set_size; i++) {
        sink = *eviction_set[i];
    }

    asm volatile ("fence rw, rw" ::: "memory");
    int t1 = rdcycle();
    asm volatile ("fence rw, rw" ::: "memory");
    sink = *target;
    asm volatile ("fence rw, rw" ::: "memory");
    int t2 = rdcycle() - t1;

    printf("  t2: %d, set_size: %d\n", t2, set_size);
    return t2 > THRESHOLD;
}

int main() {
    char *target = malloc(sizeof(char));
    uintptr_t target_set = ((uintptr_t)target % SET_STRIDE) / MEMBUS_WIDTH;
    char *buf    = malloc(BUF_SIZE + SET_STRIDE);

    char **candidates    = malloc(sizeof(char *) * CANDIDATE_SIZE);
    char **eviction_set  = malloc(sizeof(char *) * CANDIDATE_SIZE);

    int cand_count = 0;

    for (size_t i = 0; i < BUF_SIZE; i++) {
        if (((uintptr_t)(&buf[i]) % SET_STRIDE) / MEMBUS_WIDTH == target_set) {
            candidates[cand_count]   = &buf[i];
            eviction_set[cand_count] = &buf[i];
            cand_count++;
            i += SET_STRIDE - 1;
        }
    }

    int set_size = cand_count;
    int i = 0;
    while (i < set_size) {
        char *tmp = eviction_set[i];
        eviction_set[i] = eviction_set[set_size - 1];
        set_size--;

        if (!probe(target, eviction_set, set_size)) {
            // Essential — put it back at the end
            eviction_set[set_size] = eviction_set[i];
            eviction_set[i] = tmp;
            set_size++;
            i++;
        }
    }

    printf("Eviction set found! Size: %d\n", set_size);

    return 0;
}