#include <limits.h>
#include "kitchensink/internal/kitlibstate.h"

static Kit_LibraryState _librarystate = {
    0,
    1,
    0,
    3,
    64,
    64,
    1024,
    1024,
    1024,
    INT_MAX,
    INT_MAX,
    NULL,
    NULL,
};

Kit_LibraryState* Kit_GetLibraryState() {
    return &_librarystate;
}
