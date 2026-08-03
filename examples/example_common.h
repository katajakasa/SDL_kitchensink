#ifndef EXAMPLE_COMMON_H
#define EXAMPLE_COMMON_H

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Common helper functions for the examples.
 *
 * Everything here is plain SDL boilerplate that has nothing to do with
 * SDL_kitchensink itself; it is collected here to keep the actual examples
 * focused on showing how the kitchensink API is used. Since these are examples,
 * the helpers don't bother with error propagation -- they just print an error
 * and exit the process on failure.
 */

/**
 * @brief Reads the filename argument from the command line, exiting the process if it is missing.
 *
 * A missing argument is not treated as an error; the usage message is printed
 * and the process exits with status 0.
 *
 * @param argc Argument count from main()
 * @param argv Argument list from main()
 * @param example_name Example binary name to show in the usage message
 * @return The filename argument
 */
static inline const char *get_filename_arg(int argc, char *argv[], const char *example_name) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", example_name);
        exit(0);
    }
    return argv[1];
}

/**
 * @brief Initializes SDL, exiting the process on failure.
 *
 * @param flags Subsystem init flags, passed to SDL_Init() as-is
 */
static inline void initialize_sdl(Uint32 flags) {
    if(!SDL_Init(flags)) {
        fprintf(stderr, "Unable to initialize SDL3: %s\n", SDL_GetError());
        exit(1);
    }
}

/**
 * @brief Creates a resizable window, exiting the process on failure.
 *
 * @param title Window title
 * @param w Window width in pixels
 * @param h Window height in pixels
 * @param flags Extra SDL_WindowFlags to set in addition to SDL_WINDOW_RESIZABLE, 0 for none
 * @return The created window
 */
static inline SDL_Window *create_window(const char *title, int w, int h, SDL_WindowFlags flags) {
    SDL_Window *window = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | flags);
    if(window == NULL) {
        fprintf(stderr, "Unable to create a new window: %s\n", SDL_GetError());
        exit(1);
    }
    return window;
}

/**
 * @brief Creates an accelerated renderer for a window, exiting the process on failure.
 *
 * Vsync is enabled on the renderer, so the examples don't need to play around
 * with SDL_Delay.
 *
 * @param window Window to render into
 * @return The created renderer
 */
static inline SDL_Renderer *create_renderer(SDL_Window *window) {
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if(renderer == NULL) {
        fprintf(stderr, "Unable to create a renderer: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetRenderVSync(renderer, 1);
    return renderer;
}

/**
 * @brief Creates an OpenGL context for a window, exiting the process on failure.
 *
 * Vsync is enabled via the swap interval, so the examples don't need to play
 * around with SDL_Delay. The window must have been created with the
 * SDL_WINDOW_OPENGL flag.
 *
 * @param window Window to create the context for
 * @return The created OpenGL context
 */
static inline SDL_GLContext create_gl_context(SDL_Window *window) {
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if(gl_ctx == NULL) {
        fprintf(stderr, "Unable to create an OpenGL context: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_GL_SetSwapInterval(1);
    return gl_ctx;
}

#endif // EXAMPLE_COMMON_H
