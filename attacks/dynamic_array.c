#include "dynamic_array.h"

dyn_array* create_dyn_array()
{
    dyn_array* arr = malloc(sizeof(dyn_array));

    arr -> data = malloc(sizeof(uint32_t*));
    arr -> size = 0;
    arr -> capacity = 1;

    return arr;
}

void insert(dyn_array* arr, uint32_t* value)
{
    if (arr -> size >= arr -> capacity) 
    {
        // Expand Array
        arr -> capacity *= 2;
        arr -> data = realloc(arr -> data, arr -> capacity * sizeof(uint32_t*));
    }

    // Data not filled
    arr -> data[arr -> size++] = value;
}
