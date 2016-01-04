#include <kitchensink/kitchensink.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>

/*
* Note! This example does not do proper error handling etc.
* It is for example use only!
*/

const char *stream_types[] = {
    "KIT_STREAMTYPE_UNKNOWN",
    "KIT_STREAMTYPE_VIDEO",
    "KIT_STREAMTYPE_AUDIO",
    "KIT_STREAMTYPE_DATA",
    "KIT_STREAMTYPE_SUBTITLE",
    "KIT_STREAMTYPE_ATTACHMENT"
};

int main(int argc, char *argv[]) {
	SDL_Window *window = NULL;
	SDL_Renderer *renderer = NULL;
    Kit_Source *src = NULL;
    SDL_Event event;
    int err = 0;
    bool run = false;
    const char* filename = NULL;

    if(argc != 2) {
        fprintf(stderr, "Usage: exampleplay <filename>\n");
        return 0;
    }
    filename = argv[1];

	err = SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);
	window = SDL_CreateWindow("Example Player", -1, -1, 1280, 800, SDL_WINDOW_SHOWN);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    Kit_Init(KIT_INIT_FORMATS|KIT_INIT_NETWORK);

    if(err != 0 || window == NULL || renderer == NULL) {
        fprintf(stderr, "Unable to initialize SDL!\n");
        return 1;
    }

    // Open up the sourcefile.
    src = Kit_CreateSourceFromUrl(filename);
    if(src == NULL) {
        fprintf(stderr, "Unable to load file '%s': %s\n", filename, Kit_GetError());
        return 1;
    }

    // Print stream information
    Kit_StreamInfo info;
    for(int i = 0; i < Kit_GetSourceStreamCount(src); i++) {
        err = Kit_GetSourceStreamInfo(src, &info, i);
        if(err) {
            fprintf(stderr, "Unable to fetch stream #%d information: %s.\n", i, Kit_GetError());
            return 1;
        }
        fprintf(stderr, "Stream #%d: %s\n", i, stream_types[info.type]);
    }

    // Initialize codecs.
    // We could choose streams before this if we wanted to; now we just use the defaults (best guesses)
    if(Kit_InitSourceCodecs(src)) {
        fprintf(stderr, "Error while initializing codecs: %s", Kit_GetError());
        return 1;
    }

    while(run) {
         while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_KEYUP:
                    if(event.key.keysym.sym == SDLK_ESCAPE) {
                        run = false;
                    }
                    break;
                case SDL_QUIT:
                    run = false;
                    break;
            }
        }
    }

    Kit_CloseSource(src);

    Kit_Quit();
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}