#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_thread.h>

#include "kitchensink3/internal/utils/kitalloc.h"
#include "kitchensink3/kiterror.h"

#define KIT_ERRBUFSIZE 1024

typedef struct Kit_ErrorState {
    bool available;
    char message[KIT_ERRBUFSIZE];
} Kit_ErrorState;

static SDL_TLSID error_tls_id;

/**
 * Fetch the error state of the current thread (TLS).
 */
static Kit_ErrorState *Kit_GetErrorState(void) {
    Kit_ErrorState *state = SDL_GetTLS(&error_tls_id);
    if(state == NULL) {
        if((state = Kit_Calloc(1, sizeof(Kit_ErrorState))) == NULL)
            return NULL;
        if(!SDL_SetTLS(&error_tls_id, state, free)) {
            free(state);
            return NULL;
        }
    }
    return state;
}

const char *Kit_GetError(void) {
    Kit_ErrorState *state = Kit_GetErrorState();
    if(state != NULL && state->available) {
        state->available = false;
        return state->message;
    }
    return NULL;
}

void Kit_SetError(const char *fmt, ...) {
    assert(fmt != NULL);
    Kit_ErrorState *state = Kit_GetErrorState();
    if(state == NULL)
        return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(state->message, KIT_ERRBUFSIZE, fmt, args);
    va_end(args);
    state->available = true;
}

void Kit_ClearError(void) {
    Kit_ErrorState *state = Kit_GetErrorState();
    if(state == NULL)
        return;
    state->message[0] = 0;
    state->available = false;
}
