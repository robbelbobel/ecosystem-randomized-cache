#include "dynamic_array.h"

dyn_array* create_dyn_array()
{
    dyn_array* arr = malloc(sizeof(dyn_array));

    arr -> data = malloc(1 * sizeof(char*));
    arr -> size = 0;
    arr -> capacity = 1;

    return arr;
}

void free_dyn_array(dyn_array* arr) 
{
    free(arr -> data);
    free(arr);
}

void insert(dyn_array* arr, char* value)
{
    if (arr -> size >= arr -> capacity) 
    {
        // Expand Array
        arr -> capacity *= 2;
        arr -> data = realloc(arr -> data, arr -> capacity * sizeof(char*));
    }

    // Data not filled
    arr -> data[arr -> size++] = value;
}

bool contains(dyn_array* arr, char* value)
{
    for (int i = 0; i < arr -> size; i++) 
    {
        if (arr -> data[i] == value) 
        {
            return true;
        }
    }

    return false;
}

bool remove_el(dyn_array* arr, char* value)
{
    for (int i = 0; i < arr -> size; i++)
    {
        if (arr -> data[i] == value) {
            arr -> data[i] = arr -> data[arr -> size - 1];
            arr -> size--;
            return true;
        }
    }

    return false;
}
