#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "performance.h"
#include "dynamic_array.h"

#define MEMBUS_WIDTH 16
#define CACHE_SIZE (4 * 2 * 2 * MEMBUS_WIDTH) // sets * ways * skews * MEMBUS_WIDTH

int get_threshold() {
    char* buffer = malloc(1024 * 1024 * sizeof(char));

    volatile char val;

    // ---- cold access ----
    int t1 = rdcycle();
    val = buffer[0];
    int t2 = rdcycle();

    // ---- warm access ----
    val = buffer[0];
    int t3 = rdcycle();

    free(buffer);

    int cold = t2 - t1;
    int warm = t3 - t2;

    printf("cold: %u cycles\n", cold);
    printf("warm: %u cycles\n", warm);

    return (cold - warm) / 2 + warm;
}

bool probe(dyn_array* set, char* candidate, char* excl, int threshold) {
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
    return diff > threshold;
}

int main(int argc, char** argv) 
{
    printf("Starting evsetcreation...\n");

    int threshold = get_threshold();
    printf("Choosing threshold %i\n", threshold);

    /** LINES DATA STRUCTURE CREATION **/
    printf("Allocating lines\n");

    // Initialize Buffer
    char* buffer = malloc(CACHE_SIZE * 2 * sizeof(char));

    // Initialize lines
    int num_lines = (CACHE_SIZE * 2) / MEMBUS_WIDTH;
    char** lines = malloc(num_lines * sizeof(char*));

    for (int i = 0; i < num_lines; i++) 
    {
        // Not Randomized!
        lines[i] = &(buffer[i * 16]);
    }

    // Shuffle Lines
    for (int i = num_lines - 1; i >= 0; i--) {
        uint32_t rand_idx = rand() % (i + 1);

        char* tmp = lines[i];
        lines[i] = lines[rand_idx];
        
        lines[rand_idx] = tmp;
    }

    dyn_array* conflict_set = create_dyn_array();
    for (int i = 0; i < num_lines; i++) 
    { 
        if (!probe(conflict_set, lines[i], NULL, threshold)) {
            // Insert candidate into conflict set
            insert(conflict_set, lines[i]);
        }
    }

    if (conflict_set->size == 0) {
        printf("Error: Empty conflict set...\nStopping execution\n");
        return -1;
    }

    printf("conflict set size: %u\n", conflict_set->size);

    for (int i = 0; i < num_lines; i++) 
    {
        if (!contains(conflict_set, lines[i])) 
        {
            if (probe(conflict_set, lines[i], NULL, threshold)) 
            {
                dyn_array* eviction_set = create_dyn_array();
                
                for (int j = 0; j < conflict_set -> size; j++) 
                {
                    if (!probe(conflict_set, lines[i], conflict_set -> data[j], threshold))
                    {
                        insert(eviction_set, conflict_set -> data[j]);
                    }
                }
                
                for (int j = 0; j < eviction_set -> size; j++)
                {
                    remove_el(conflict_set, eviction_set -> data[j]);
                }

                free_dyn_array(eviction_set);
            }
        }
    }

    printf("No eviction set found!\n");

    return -1;
}
