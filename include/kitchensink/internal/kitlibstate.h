#ifndef KITLIBSTATE_H
#define KITLIBSTATE_H

#include <ass/ass.h>
#include "kitchensink/kitconfig.h"

typedef struct Kit_LibraryState {
    unsigned int init_flags;
    ASS_Library *libass_handle;
} Kit_LibraryState;

KIT_LOCAL Kit_LibraryState* Kit_GetLibraryState();

#endif // KITLIBSTATE_H
