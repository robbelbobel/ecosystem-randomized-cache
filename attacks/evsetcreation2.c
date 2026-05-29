#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include "performance.h"
#include "dynamic_array.h"
 
#define MEMBUS_WIDTH 16 * 8
#define CACHE_SIZE (8 * 2 * 1 * MEMBUS_WIDTH) // sets * ways * skews * MEMBUS_WIDTH
 
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
    // Read candidate first to establish baseline
    volatile char sink = *candidate;
 
    // Read all elements of the set except excl to attempt eviction
    for (int i = 0; i < set->size; i++) {
        if (set->data[i] != excl) {
            sink = *(set->data[i]);
        }
    }
 
    // Measure time to re-read candidate
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
 
    // Initialize buffer
    char* buffer = malloc(CACHE_SIZE * 2 * sizeof(char));
 
    // Initialize lines (one pointer per cache line)
    int num_lines = (CACHE_SIZE * 2) / MEMBUS_WIDTH;
    char** lines = malloc(num_lines * sizeof(char*));
    for (int i = 0; i < num_lines; i++) {
        lines[i] = &(buffer[i * MEMBUS_WIDTH]);
    }
 
    // Shuffle lines using rdcycle as entropy source (no srand)
    for (int i = num_lines - 1; i >= 0; i--) {
        uint32_t rand_idx = rdcycle() % (i + 1);
        char* tmp = lines[i];
        lines[i] = lines[rand_idx];
        lines[rand_idx] = tmp;
    }
 
    /** PHASE 1: Build conflict set **/
    dyn_array* conflict_set = create_dyn_array();
 
    for (int i = 0; i < num_lines; i++) {
        if (!probe(conflict_set, lines[i], NULL, threshold)) {
            // Candidate does NOT evict itself out of the set -> it maps to a
            // new cache set, so add it to conflict_set
            insert(conflict_set, lines[i]);
        }
    }
 
    if (conflict_set->size == 0) {
        printf("Error: Empty conflict set...\nStopping execution\n");
        return -1;
    }
 
    printf("conflict set size: %u\n", conflict_set->size);
 
    /** PHASE 2: Extract per-slice eviction sets **/
 
    // FIX: Snapshot which lines are NOT in the conflict_set BEFORE the loop.
    // The algorithm specifies iterating over (lines - conflict_set) as a fixed
    // set. Without the snapshot, removing elements from conflict_set mid-loop
    // would cause former conflict_set members to be incorrectly re-evaluated
    // as candidates, producing corrupt eviction sets.
    bool* is_candidate = calloc(num_lines, sizeof(bool));
    for (int i = 0; i < num_lines; i++) {
        is_candidate[i] = !contains(conflict_set, lines[i]);
    }
 
    for (int i = 0; i < num_lines; i++) {
        // Only process lines that were NOT in the original conflict_set
        if (!is_candidate[i]) continue;
 
        if (probe(conflict_set, lines[i], NULL, threshold)) {
            dyn_array* eviction_set = create_dyn_array();
 
            // Find the minimal subset of conflict_set that evicts lines[i]
            for (int j = 0; j < conflict_set->size; j++) {
                // If removing conflict_set[j] stops the eviction, then
                // conflict_set[j] is a necessary member of the eviction set
                if (!probe(conflict_set, lines[i], conflict_set->data[j], threshold)) {
                    insert(eviction_set, conflict_set->data[j]);
                }
            }
 
            if (eviction_set->size != 0) {
                printf("Eviction set found! Size: %i\n", eviction_set->size);
            }
 
            // Remove the discovered eviction set from the conflict_set so
            // subsequent candidates are tested against a fresh pool
            for (int j = 0; j < eviction_set->size; j++) {
                remove_el(conflict_set, eviction_set->data[j]);
            }
 
            free_dyn_array(eviction_set);
        }
    }
 
    // Clean up
    free(is_candidate);
    free_dyn_array(conflict_set);
    free(lines);
    free(buffer);
 
    return 0; // FIX: was incorrectly returning -1, signalling failure to caller
}
 
