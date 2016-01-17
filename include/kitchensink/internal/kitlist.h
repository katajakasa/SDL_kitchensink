#ifndef KITLIST_H
#define KITLIST_H

#include "kitchensink/kitconfig.h"

typedef struct Kit_List Kit_List;

typedef void (*Kit_ListFreeCallback)(void*);

struct Kit_List {
    unsigned int size;
    unsigned int length;
    Kit_ListFreeCallback free_cb;
    void **data;
};

KIT_LOCAL Kit_List* Kit_CreateList(unsigned int size, Kit_ListFreeCallback free_cb);
KIT_LOCAL void Kit_DestroyList(Kit_List *list);

KIT_LOCAL void Kit_ClearList(Kit_List *list);
KIT_LOCAL void Kit_RemoveFromList(Kit_List *list, unsigned int iterator);
KIT_LOCAL void* Kit_IterateList(const Kit_List *list, unsigned int *iterator);
KIT_LOCAL int Kit_WriteList(Kit_List *list, void *ptr);
KIT_LOCAL int Kit_GetListLength(const Kit_List *list);

#endif // KITLIST_H
