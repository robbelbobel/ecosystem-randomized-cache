#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "performance.h"
#include "dynamic_array.h"

#define CACHE_SIZE 4 * 2 * 2 * 100 // sets * ways * skews

#define THRESHOLD_CYCLES 45 // 80 for memory response

bool probe(dyn_array* set, uint32_t* candidate) {
    volatile int32_t candidate_value = *candidate;

    for (size_t i = 0; i < set -> size; i++) 
    {
        volatile int32_t l = *(set->data[i]);
    }

    asm volatile("fence iorw, iorw");

    uint32_t beg_cycles = rdcycle();
    volatile int32_t candidate_value_repeat = *candidate;

    asm volatile("fence iorw, iorw");

    uint32_t diff_cycles = rdcycle() - beg_cycles;

    printf("diff cycles: %u\n", diff_cycles);

    return  diff_cycles > THRESHOLD_CYCLES;
}

int main() 
{
    /** LINES DATA STRUCTURE CREATION **/
    printf("Allocating lines\n");

    // Initialize Buffer
    uint32_t* buffer = malloc(CACHE_SIZE * 2 * sizeof(uint32_t));

    // Initialize lines
    uint32_t** lines = malloc(CACHE_SIZE * 2 * sizeof(uint32_t*));

    for(uint32_t i = 0; i < CACHE_SIZE * 2; i++) 
    {
        // Not Randomized!
        lines[i] = buffer + i;
    }

    dyn_array* conflict_set = create_dyn_array();

    for(uint32_t i = 0; i < CACHE_SIZE * 2; i++) 
    {
        if(probe(conflict_set, lines[i])) {
            // Insert candidate into conflict set
            insert(conflict_set, lines[i]);
        }

        printf("lines[i]: %u\n", lines[i]);
    }


    return 0;
}