#include <SDL.h>
#include <kitchensink2/kitchensink.h>

#include "example_common.h"
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
    // Get filename to open
    const char *filename = get_filename_arg(argc, argv, "simple");

    // Init SDL
    initialize_sdl(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    // Create a resizable window and an accelerated, vsynced renderer.
    SDL_Window *window = create_window(filename, 1280, 720, 0);
    SDL_Renderer *renderer = create_renderer(window);

    // Initialize Kitchensink with network and libass support.
    const int err = Kit_Init(KIT_INIT_NETWORK | KIT_INIT_ASS);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize Kitchensink: %s", Kit_GetError());
        return 1;
    }

    // Open up the sourcefile.
    // This can be a local file, network url, ...
    Kit_Source *src = Kit_CreateSourceFromUrl(filename);
    if(src == NULL) {
        fprintf(stderr, "Unable to load file '%s': %s\n", filename, Kit_GetError());
        return 1;
    }

    // Create the player. Pick best video, audio and subtitle streams, and set subtitle
    // rendering resolution to screen resolution.
    Kit_Player *player = Kit_CreatePlayer(
        src,
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_SUBTITLE),
        NULL,
        NULL,
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
    SDL_AudioSpec wanted_spec, audio_spec;
    SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));
    wanted_spec.freq = pinfo.audio_format.sample_rate;
    wanted_spec.format = pinfo.audio_format.format;
    wanted_spec.channels = Kit_GetChannelLayoutCount(pinfo.audio_format.layout);
    const SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &audio_spec, 0);
    SDL_PauseAudioDevice(audio_dev, 0);

    // Initialize video texture. This will probably end up as YV12 most of the time.
    SDL_Texture *video_tex = Kit_CreatePlayerVideoSDLTexture(player, renderer, 0, 0);
    if(video_tex == NULL) {
        fprintf(stderr, "Error while attempting to create a video texture: %s\n", Kit_GetError());
        return 1;
    }

    // This is the subtitle texture atlas. This contains all the subtitle image fragments.
    SDL_Texture *subtitle_tex = NULL;
    if(Kit_GetPlayerSubtitleStream(player) >= 0) {
        subtitle_tex = Kit_CreatePlayerSubtitleSDLTexture(player, renderer, ATLAS_WIDTH, ATLAS_HEIGHT);
        if(subtitle_tex == NULL) {
            fprintf(stderr, "Error while attempting to create a subtitle texture atlas: %s\n", Kit_GetError());
            return 1;
        }
    }

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
    bool run = true;
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
        const int queued = SDL_GetQueuedAudioSize(audio_dev);
        if(queued < AUDIO_BUFFER_SIZE) {
            int need = AUDIO_BUFFER_SIZE - queued;

            while(need > 0) {
                const int ret = Kit_GetPlayerAudioData(player, queued, (unsigned char *)audio_buf, AUDIO_BUFFER_SIZE);
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
        Kit_GetPlayerVideoSDLTexture(player, video_tex, NULL);
        SDL_RenderCopy(renderer, video_tex, NULL, NULL);

        // Refresh subtitle texture atlas and render subtitle frames from it
        // For subtitles, use screen size instead of video size for best quality
        if(subtitle_tex != NULL) {
            const int got = Kit_GetPlayerSubtitleSDLTexture(player, subtitle_tex, sources, targets, ATLAS_MAX);
            for(int i = 0; i < got; i++) {
                SDL_RenderCopy(renderer, subtitle_tex, &sources[i], &targets[i]);
            }
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
