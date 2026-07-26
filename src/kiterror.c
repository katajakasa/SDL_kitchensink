#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL_atomic.h>
#include <SDL_thread.h>

#include "kitchensink2/internal/utils/kitalloc.h"
#include "kitchensink2/kiterror.h"

#define KIT_ERRBUFSIZE 1024

typedef struct Kit_ErrorState {
    bool available;
    char message[KIT_ERRBUFSIZE];
} Kit_ErrorState;

static SDL_SpinLock error_tls_lock;
static SDL_TLSID error_tls_id = 0;

/**
 * Fetch the error state of the current thread (TLS).
 */
static Kit_ErrorState *Kit_GetErrorState(void) {
    if(error_tls_id == 0) {
        SDL_AtomicLock(&error_tls_lock);
        if(error_tls_id == 0)
            error_tls_id = SDL_TLSCreate();
        SDL_AtomicUnlock(&error_tls_lock);
        if(error_tls_id == 0)
            return NULL;
    }
    Kit_ErrorState *state = SDL_TLSGet(error_tls_id);
    if(state == NULL) {
        if((state = Kit_Calloc(1, sizeof(Kit_ErrorState))) == NULL)
            return NULL;
        // Note: check for < 0; sdl2-compat returns 1 (SDL3 bool) on success here, real SDL2 returns 0.
        if(SDL_TLSSet(error_tls_id, state, free) < 0) {
            free(state);
            return NULL;
        }
    }
    return state;
}

const char *Kit_GetError() {
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

void Kit_ClearError() {
    Kit_ErrorState *state = Kit_GetErrorState();
    if(state == NULL)
        return;
    state->message[0] = 0;
    state->available = false;
}
