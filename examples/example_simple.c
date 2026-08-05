#include <SDL3/SDL.h>
#include <kitchensink3/kitchensink.h>

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

    // Set up default configs for the player.
    Kit_PlayerConfig config;
    Kit_ResetPlayerConfig(&config);

    // Create the player. Pick best streams available.
    Kit_Player *player = Kit_CreatePlayer(
        src,
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_SUBTITLE),
        NULL,
        NULL,
        1280,
        720,
        &config
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
    SDL_AudioSpec audio_spec;
    SDL_memset(&audio_spec, 0, sizeof(audio_spec));
    audio_spec.freq = pinfo.audio_format.sample_rate;
    audio_spec.format = pinfo.audio_format.format;
    audio_spec.channels = Kit_GetChannelLayoutCount(pinfo.audio_format.layout);
    SDL_AudioStream *audio_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
    SDL_ResumeAudioStreamDevice(audio_stream);

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
    SDL_FRect sources[ATLAS_MAX];
    SDL_FRect targets[ATLAS_MAX];

    // Get movie area size
    SDL_SetRenderLogicalPresentation(
        renderer, pinfo.video_format.width, pinfo.video_format.height, SDL_LOGICAL_PRESENTATION_LETTERBOX
    );
    bool run = true;
    while(run) {
        if(Kit_GetPlayerState(player) == KIT_STOPPED) {
            run = false;
            continue;
        }

        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_EVENT_QUIT:
                    run = false;
                    break;
            }
        }

        // Refresh audio
        const int queued = SDL_GetAudioStreamQueued(audio_stream);
        if(queued < AUDIO_BUFFER_SIZE) {
            int need = AUDIO_BUFFER_SIZE - queued;

            while(need > 0) {
                const int ret = Kit_GetPlayerAudioData(player, queued, (unsigned char *)audio_buf, AUDIO_BUFFER_SIZE);
                need -= ret;
                if(ret > 0) {
                    SDL_PutAudioStreamData(audio_stream, audio_buf, ret);
                } else {
                    break;
                }
            }
        }

        // Refresh video texture and render it
        Kit_GetPlayerVideoSDLTexture(player, video_tex, NULL);
        SDL_RenderTexture(renderer, video_tex, NULL, NULL);

        // Refresh subtitle texture atlas and render subtitle frames from it
        // For subtitles, use screen size instead of video size for best quality
        if(subtitle_tex != NULL) {
            const int got = Kit_GetPlayerSubtitleSDLTexture(player, subtitle_tex, sources, targets, ATLAS_MAX);
            for(int i = 0; i < got; i++) {
                SDL_RenderTexture(renderer, subtitle_tex, &sources[i], &targets[i]);
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
    SDL_DestroyAudioStream(audio_stream);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
