#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "performance.h"

int main() {
    uint32_t* buffer = malloc(1024 * 1024 * sizeof(uint32_t));

    volatile uint32_t val;

    // ---- cold access ----
    uint32_t t1 = rdcycle();
    val = buffer[0];
    uint32_t t2 = rdcycle();

    // ---- warm access ----
    val = buffer[0];
    uint32_t t3 = rdcycle();

    printf("cold: %u cycles\n", t2 - t1);
    printf("warm: %u cycles\n", t3 - t2);

    return 0; 
}
