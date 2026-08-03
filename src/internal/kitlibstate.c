#include "kitchensink3/internal/kitlibstate.h"
#include <stddef.h>

static Kit_LibraryState _library_state = {
    .init_flags = 0,
    .libass_handle = NULL,
    .ass_so_handle = NULL,
};

Kit_LibraryState *Kit_GetLibraryState(void) {
    return &_library_state;
}
