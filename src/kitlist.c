#include "kitchensink/internal/kitlist.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

Kit_List* Kit_CreateList(unsigned int size, Kit_ListFreeCallback free_cb) {
    Kit_List *m = calloc(1, sizeof(Kit_List));
    if(m == NULL) {
        return NULL;
    }
    m->size = size;
    m->free_cb = free_cb;
    m->data = calloc(size, sizeof(void*));
    if(m->data == NULL) {
        free(m);
        return NULL;
    }
    return m;
}

void Kit_DestroyList(Kit_List *list) {
    if(list == NULL) return;
    Kit_ClearList(list);
    free(list->data);
    free(list);
}

void Kit_ClearList(Kit_List *list) {
    assert(list != NULL);
    for(unsigned int i = 0; i < list->size; i++) {
        if(list->data[i] != NULL) {
            list->free_cb(list->data[i]);
            list->data[i] = NULL;
        }
    }
    list->length = 0;
}

void Kit_RemoveFromList(Kit_List *list, unsigned int iterator) {
    assert(list != NULL);
    list->free_cb(list->data[iterator-1]);
    list->data[iterator-1] = NULL;
    list->length--;
}

void* Kit_IterateList(const Kit_List *list, unsigned int *iterator) {
    assert(list != NULL);
    assert(iterator != NULL);
    while((*iterator) < list->size) {
        void *ptr = list->data[(*iterator)];
        *iterator += 1;
        if(ptr != NULL) {
            return ptr;
        }
    }
    return NULL;
}

int Kit_WriteList(Kit_List *list, void *ptr) {
    assert(list != NULL);
    assert(ptr != NULL);
    if(list->length >= list->size) {
        return 1;
    }
    for(unsigned int i = 0; i < list->size; i++) {
        if(list->data[i] == NULL) {
            list->data[i] = ptr;
            list->length++;
            return 0;
        }
    }
    return 1;
}

int Kit_GetListLength(const Kit_List *list) {
    assert(list != NULL);
    return list->length;
}
