#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

#include <inttypes.h>
#include <stdlib.h>

typedef struct 
{
    char** data;
    size_t size;
    size_t capacity;
} dyn_array;

// Helpers
dyn_array* create_dyn_array();
void free_dyn_array(dyn_array* arr);

void insert(dyn_array* arr, char* value);
bool contains(dyn_array* arr, char* value);
bool remove_el(dyn_array* arr, char* value);


#endif