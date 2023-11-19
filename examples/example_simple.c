#include <SDL.h>
#include <kitchensink/kitchensink.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * Note! This example does not do proper error handling etc.
 * It is for example use only!
 */

#define AUDIO_BUFFER_SIZE (1024 * 64)
#define ATLAS_WIDTH 4096
#define ATLAS_HEIGHT 4096
#define ATLAS_MAX 1024

int main(int argc, char *argv[]) {
    int err = 0, ret = 0;
    const char *filename = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    bool run = true;
    Kit_Source *src = NULL;
    Kit_Player *player = NULL;
    SDL_AudioSpec wanted_spec, audio_spec;
    SDL_AudioDeviceID audio_dev;

    // Get filename to open
    if(argc != 2) {
        fprintf(stderr, "Usage: simple <filename>\n");
        return 0;
    }
    filename = argv[1];

    // Init SDL
    err = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize SDL2!\n");
        return 1;
    }

    // Create a resizable window.
    window =
        SDL_CreateWindow(filename, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_RESIZABLE);
    if(window == NULL) {
        fprintf(stderr, "Unable to create a new window!\n");
        return 1;
    }

    // Create an accelerated renderer. Enable vsync, so we don't need to play around with SDL_Delay.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if(renderer == NULL) {
        fprintf(stderr, "Unable to create a renderer!\n");
        return 1;
    }

    // Initialize Kitchensink with network and libass support.
    err = Kit_Init(KIT_INIT_NETWORK | KIT_INIT_ASS);
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

    // Create the player. Pick best video, audio and subtitle streams, and set subtitle
    // rendering resolution to screen resolution.
    player = Kit_CreatePlayer(
        src,
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_SUBTITLE),
        1280,
        720
    );
    if(player == NULL) {
        fprintf(stderr, "Unable to create player: %s\n", Kit_GetError());
        return 1;
    }

    // Print some information
    Kit_PlayerInfo pinfo;
    Kit_GetPlayerInfo(player, &pinfo);

    // Make sure there is video in the file to play first.
    if(Kit_GetPlayerVideoStream(player) == -1) {
        fprintf(stderr, "File contains no video!\n");
        return 1;
    }

    // Init audio
    SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));
    wanted_spec.freq = pinfo.audio_format.sample_rate;
    wanted_spec.format = pinfo.audio_format.format;
    wanted_spec.channels = pinfo.audio_format.channels;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &audio_spec, 0);
    SDL_PauseAudioDevice(audio_dev, 0);

    // Initialize video texture. This will probably end up as YV12 most of the time.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_Texture *video_tex = SDL_CreateTexture(
        renderer,
        pinfo.video_format.format,
        SDL_TEXTUREACCESS_STATIC,
        pinfo.video_format.width,
        pinfo.video_format.height
    );
    if(video_tex == NULL) {
        fprintf(stderr, "Error while attempting to create a video texture\n");
        return 1;
    }

    // This is the subtitle texture atlas. This contains all the subtitle image fragments.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest"); // Always nearest for atlas operations
    SDL_Texture *subtitle_tex =
        SDL_CreateTexture(renderer, pinfo.subtitle_format.format, SDL_TEXTUREACCESS_STATIC, ATLAS_WIDTH, ATLAS_HEIGHT);
    if(subtitle_tex == NULL) {
        fprintf(stderr, "Error while attempting to create a subtitle texture atlas\n");
        return 1;
    }

    // Make sure subtitle texture is in correct blending mode
    SDL_SetTextureBlendMode(subtitle_tex, SDL_BLENDMODE_BLEND);

    // Clear screen with black
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Start playback
    Kit_PlayerPlay(player);

    // Playback temporary data buffers
    char audio_buf[AUDIO_BUFFER_SIZE];
    SDL_Rect sources[ATLAS_MAX];
    SDL_Rect targets[ATLAS_MAX];

    // Get movie area size
    SDL_RenderSetLogicalSize(renderer, pinfo.video_format.width, pinfo.video_format.height);
    while(run) {
        if(Kit_GetPlayerState(player) == KIT_STOPPED) {
            run = false;
            continue;
        }

        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_QUIT:
                    run = false;
                    break;
            }
        }

        // Refresh audio
        int queued = SDL_GetQueuedAudioSize(audio_dev);
        if(queued < AUDIO_BUFFER_SIZE) {
            int need = AUDIO_BUFFER_SIZE - queued;

            while(need > 0) {
                ret = Kit_GetPlayerAudioData(player, queued, (unsigned char *)audio_buf, AUDIO_BUFFER_SIZE);
                need -= ret;
                if(ret > 0) {
                    SDL_QueueAudio(audio_dev, audio_buf, ret);
                } else {
                    break;
                }
            }
            // If we now have data, start playback (again)
            if(SDL_GetQueuedAudioSize(audio_dev) > 0) {
                SDL_PauseAudioDevice(audio_dev, 0);
            }
        }

        // Refresh video texture and render it
        Kit_GetPlayerVideoData(player, video_tex, NULL);
        SDL_RenderCopy(renderer, video_tex, NULL, NULL);

        // Refresh subtitle texture atlas and render subtitle frames from it
        // For subtitles, use screen size instead of video size for best quality
        int got = Kit_GetPlayerSubtitleData(player, subtitle_tex, sources, targets, ATLAS_MAX);
        for(int i = 0; i < got; i++) {
            SDL_RenderCopy(renderer, subtitle_tex, &sources[i], &targets[i]);
        }

        // Render to screen + wait for vsync
        SDL_RenderPresent(renderer);
    }

    Kit_ClosePlayer(player);
    Kit_CloseSource(src);
    Kit_Quit();

    SDL_DestroyTexture(subtitle_tex);
    SDL_DestroyTexture(video_tex);
    SDL_CloseAudioDevice(audio_dev);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
