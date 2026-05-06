#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "performance.h"
#include "dynamic_array.h"

#define CACHE_SIZE 4 * 2 * 2 // sets * ways * skews

#define THRESHOLD_CYCLES 45 // Cold: 62 cycles, Warm: 31 cycles

bool probe(dyn_array* set, uint32_t* candidate, uint32_t* excl) {
    // Read Candidate
    volatile uint32_t sink = *candidate;

    for (uint32_t i = 0; i < set -> size; i++) 
    {
        if (set -> data[i] != excl) 
        {
            sink = *(set -> data[i]);
        }
    }
    
    uint32_t beg_cycles = rdcycle();

    sink = *candidate;

    uint32_t diff = rdcycle() - beg_cycles;

    printf("diff: %u\n", diff);

    return diff > THRESHOLD_CYCLES;
}

int main() 
{
    /** LINES DATA STRUCTURE CREATION **/
    printf("Allocating lines\n");

    // Initialize Buffer
    uint32_t* buffer = malloc(CACHE_SIZE * 2 * sizeof(uint32_t));

    // Initialize lines
    uint32_t** lines = malloc(CACHE_SIZE * 2 * sizeof(uint32_t*));

    for (uint32_t i = 0; i < CACHE_SIZE * 2; i++) 
    {
        // Not Randomized!
        lines[i] = buffer + i;
    }

    dyn_array* conflict_set = create_dyn_array();
    printf("\n\nconflict set: \n");
    for (uint32_t i = 0; i < CACHE_SIZE * 2; i++) 
    { 
        if (!probe(conflict_set, lines[i], NULL)) {
            // Insert candidate into conflict set
            insert(conflict_set, lines[i]);
        }
    }

    printf("\n\neviction set: \n");
    for (uint32_t i = 0; i < CACHE_SIZE * 2; i++) 
    {
        if (!contains(conflict_set, lines[i])) 
        {
            if (probe(conflict_set, lines[i], NULL)) 
            {
                dyn_array* eviction_set = create_dyn_array();
                
                for (uint32_t j = 0; j < conflict_set -> size; j++) 
                {
                    if (!probe(conflict_set, lines[i], conflict_set -> data[j]))
                    {

                        insert(eviction_set, conflict_set -> data[j]);
                    }
                }
                
                printf("eviction set found, size: %u\n", eviction_set -> size);
                return 0;
            }
        }
    }

    return -1;
}