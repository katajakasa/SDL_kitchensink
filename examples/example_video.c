#include <kitchensink/kitchensink.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>

/*
* Requires SDL2 2.0.4 !
*
* Note! This example does not do proper error handling etc.
* It is for example use only!
*/

#define AUDIOBUFFER_SIZE (16384)

int main(int argc, char *argv[]) {
    int err = 0, ret = 0;
    const char* filename = NULL;

    // Video
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    // Events
    SDL_Event event;
    bool run = true;
    
    // Kitchensink
    Kit_Source *src = NULL;
    Kit_Player *player = NULL;

    // Audio playback
    SDL_AudioSpec wanted_spec, audio_spec;
    SDL_AudioDeviceID audio_dev;
    char audiobuf[AUDIOBUFFER_SIZE];

    // Get filename to open
    if(argc != 2) {
        fprintf(stderr, "Usage: exampleplay <filename>\n");
        return 0;
    }
    filename = argv[1];

    // Init SDL
    err = SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize SDL2!\n");
        return 1;
    }

    // Create a resizable window.
    window = SDL_CreateWindow("Example Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_RESIZABLE);
    if(window == NULL) {
        fprintf(stderr, "Unable to create a new window!\n");
        return 1;
    }

    // Create an accelerated renderer. Enable vsync, so we don't need to play around with SDL_Delay.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(window == NULL) {
        fprintf(stderr, "Unable to create a renderer!\n");
        return 1;
    }

    // Initialize Kitchensink with network support and all formats.
    err = Kit_Init(KIT_INIT_FORMATS|KIT_INIT_NETWORK);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize Kitchensink: %s", Kit_GetError());
        return 1;
    }

    // Open up the sourcefile.
    // This can be a local file, network url, ...
    src = Kit_CreateSourceFromUrl(filename);
    if(src == NULL) {
        fprintf(stderr, "Unable to load file '%s': %s\n", filename, Kit_GetError());
        return 1;
    }

    // Print stream information
    Kit_StreamInfo sinfo;
    fprintf(stderr, "Source streams:\n");
    for(int i = 0; i < Kit_GetSourceStreamCount(src); i++) {
        err = Kit_GetSourceStreamInfo(src, &sinfo, i);
        if(err) {
            fprintf(stderr, "Unable to fetch stream #%d information: %s.\n", i, Kit_GetError());
            return 1;
        }
        fprintf(stderr, " * Stream #%d: %s\n", i, Kit_GetKitStreamTypeString(sinfo.type));
    }

    // Create the player
    player = Kit_CreatePlayer(src);
    if(player == NULL) {
        fprintf(stderr, "Unable to create player: %s\n", Kit_GetError());
        return 1;
    }

    // Print some information
    Kit_PlayerInfo pinfo;
    Kit_GetPlayerInfo(player, &pinfo);

    if(!pinfo.video.is_enabled) {
        fprintf(stderr, "File contains no video!\n");
        return 1;
    }

    fprintf(stderr, "Media information:\n");
    fprintf(stderr, " * Audio: %s (%s), %dHz, %dch, %db, %s\n",
        pinfo.acodec,
        pinfo.acodec_name,
        pinfo.audio.samplerate,
        pinfo.audio.channels,
        pinfo.audio.bytes,
        pinfo.audio.is_signed ? "signed" : "unsigned");
    fprintf(stderr, " * Video: %s (%s), %dx%d\n",
        pinfo.vcodec,
        pinfo.vcodec_name,
        pinfo.video.width,
        pinfo.video.height);
    fprintf(stderr, "Duration: %f seconds\n", Kit_GetPlayerDuration(player));

    // Init audio
    SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));
    wanted_spec.freq = pinfo.audio.samplerate;
    wanted_spec.format = pinfo.audio.format;
    wanted_spec.channels = pinfo.audio.channels;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &audio_spec, 0);
    SDL_PauseAudioDevice(audio_dev, 0);

    // Print some format info
    fprintf(stderr, "Texture type: %s\n", Kit_GetSDLPixelFormatString(pinfo.video.format));
    fprintf(stderr, "Audio format: %s\n", Kit_GetSDLAudioFormatString(pinfo.audio.format));

    // Initialize texture
    SDL_Texture *tex = SDL_CreateTexture(
        renderer,
        pinfo.video.format,
        SDL_TEXTUREACCESS_STATIC,
        pinfo.video.width,
        pinfo.video.height);
    if(tex == NULL) {
        fprintf(stderr, "Error while attempting to create a texture\n");
        return 1;
    }

    fflush(stderr);

    // Set logical size for the renderer. This way when we scale, we keep aspect ratio.
    SDL_RenderSetLogicalSize(renderer, pinfo.video.width, pinfo.video.height); 

    // Start playback
    Kit_PlayerPlay(player);

    while(run) {
        if(Kit_GetPlayerState(player) == KIT_STOPPED) {
            run = false;
            continue;
        }

        // Check for events
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_KEYUP:
                    if(event.key.keysym.sym == SDLK_ESCAPE) {
                        run = false;
                    }
                    if(event.key.keysym.sym == SDLK_q) {
                        Kit_PlayerPlay(player);
                    }
                    if(event.key.keysym.sym == SDLK_w) {
                        Kit_PlayerPause(player);
                    }
                    if(event.key.keysym.sym == SDLK_e) {
                        Kit_PlayerStop(player);
                    }
                    if(event.key.keysym.sym == SDLK_RIGHT) {
                        if(Kit_PlayerSeek(player, 10.0) != 0) {
                            fprintf(stderr, "%s\n", Kit_GetError());
                        }
                    }
                    if(event.key.keysym.sym == SDLK_LEFT) {
                        if(Kit_PlayerSeek(player, -10.0) != 0) {
                            fprintf(stderr, "%s\n", Kit_GetError());
                        }
                    }
                    break;
                case SDL_QUIT:
                    run = false;
                    break;
            }
        }

        // Refresh audio
        ret = SDL_GetQueuedAudioSize(audio_dev);
        if(ret < AUDIOBUFFER_SIZE) {
            ret = Kit_GetAudioData(player, (unsigned char*)audiobuf, AUDIOBUFFER_SIZE, (size_t)ret);
            if(ret > 0) {
                SDL_LockAudio();
                SDL_QueueAudio(audio_dev, audiobuf, ret);
                SDL_UnlockAudio();
                SDL_PauseAudioDevice(audio_dev, 0);
            }
        }

        // Refresh video
        Kit_RefreshTexture(player, tex);

        // Render to the screen
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(tex);
    SDL_CloseAudioDevice(audio_dev);

    Kit_ClosePlayer(player);
    Kit_CloseSource(src);

    Kit_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
