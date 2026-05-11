#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "performance.h"
#include "dynamic_array.h"

#define MEMBUS_WIDTH 16
#define CACHE_SIZE 4 * 2 * 2 * MEMBUS_WIDTH // sets * ways * skews * MEMBUS_WIDTH

#define THRESHOLD_CYCLES 130 // Cold: 192 cycles, Warm: 124 cycles

bool probe(dyn_array* set, char* candidate, char* excl) {
    // Read Candidate
    volatile char sink = *candidate;

    for (int i = 0; i < set -> size; i++) 
    {
        if (set -> data[i] != excl) 
        {
            sink = *(set -> data[i]);
        }
    }
    
    int beg_cycles = rdcycle();

    sink = *candidate;

    int diff = rdcycle() - beg_cycles;

    // Returns whether an eviction has occurred
    return diff > THRESHOLD_CYCLES;
}

int main() 
{
    /** LINES DATA STRUCTURE CREATION **/
    printf("Allocating lines\n");

    // Initialize Buffer
    char* buffer = malloc(CACHE_SIZE * 2 * sizeof(char));

    // Initialize lines
    char** lines = malloc(CACHE_SIZE * 2 * sizeof(char*));

    for (int i = 0; i < CACHE_SIZE * 2; i++) 
    {
        // Not Randomized!
        lines[i] = &(buffer[i]);
    }

    dyn_array* conflict_set = create_dyn_array();
    printf("\n\nconflict set: \n");
    for (int i = 0; i < CACHE_SIZE * 2; i++) 
    { 
        if (!probe(conflict_set, lines[i], NULL)) {
            // Insert candidate into conflict set
            insert(conflict_set, lines[i]);
        }
    }

    printf("\n\neviction set: \n");
    for (int i = 0; i < CACHE_SIZE * 2; i++) 
    {
        if (!contains(conflict_set, lines[i])) 
        {
            if (probe(conflict_set, lines[i], NULL)) 
            {
                dyn_array* eviction_set = create_dyn_array();
                
                for (int j = 0; j < conflict_set -> size; j++) 
                {
                    if (!probe(conflict_set, lines[i], conflict_set -> data[j]))
                    {
                        insert(eviction_set, conflict_set -> data[j]);
                    }
                }
                
                printf("eviction set found, size: %u\n", eviction_set -> size);
            }
        }
    }

    return -1;
}