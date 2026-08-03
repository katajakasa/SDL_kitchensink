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

// Optional whence flags for the seek callback. These are passed through from ffmpeg avio;
// defined here so that the example does not need the ffmpeg headers.
#define AVSEEK_SIZE 0x10000
#define AVSEEK_FORCE 0x20000

/**
 * @brief Read callback for the custom kitchensink source.
 *
 * @param userdata FILE handle to read from, as given to Kit_CreateSourceFromCustom()
 * @param buf Buffer to read data into
 * @param buf_size Maximum amount of bytes to read
 * @return Number of bytes read, or -1 on end of file
 */
int read_callback(void *userdata, uint8_t *buf, int buf_size) {
    FILE *fd = (FILE *)userdata;
    if(!feof(fd)) {
        return fread(buf, 1, buf_size, fd);
    }
    return -1;
}

/**
 * @brief Seek callback for the custom kitchensink source.
 *
 * Setting this makes the source seekable, so that e.g. Kit_PlayerSeek() works.
 *
 * @param userdata FILE handle to seek, as given to Kit_CreateSourceFromCustom()
 * @param offset Byte offset, relative to whence
 * @param whence SEEK_SET, SEEK_CUR or SEEK_END, possibly with AVSEEK_* flags set
 * @return New file position in bytes (or the file size for AVSEEK_SIZE), or -1 on error
 */
int64_t seek_callback(void *userdata, int64_t offset, int whence) {
    FILE *fd = (FILE *)userdata;

    // AVSEEK_SIZE asks for the total size of the file instead of seeking.
    if(whence & AVSEEK_SIZE) {
        const long current = ftell(fd);
        if(current < 0 || fseek(fd, 0, SEEK_END) != 0)
            return -1;
        const long size = ftell(fd);
        fseek(fd, current, SEEK_SET);
        return size;
    }

    // AVSEEK_FORCE only suggests that seeking is worth doing even if it is expensive;
    // for a local file it makes no difference, so just mask it off.
    if(fseek(fd, offset, whence & ~AVSEEK_FORCE) != 0)
        return -1;
    return ftell(fd);
}

int main(int argc, char *argv[]) {
    // Get filename to open
    const char *filename = get_filename_arg(argc, argv, "custom");

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

    // Open file with fopen. We then proceed to read this with our custom file handlers.
    FILE *fd = fopen(filename, "rb");
    if(fd == NULL) {
        fprintf(stderr, "Unable to open file '%s' for reading\n", filename);
        return 1;
    }

    // Open up the custom source. Declare read and seek callbacks, and transport FD in userdata.
    Kit_Source *src = Kit_CreateSourceFromCustom(read_callback, seek_callback, fd);
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
    Kit_PlayerInfo player_info;
    Kit_GetPlayerInfo(player, &player_info);

    // Make sure there is video in the file to play first.
    if(Kit_GetPlayerVideoStream(player) == -1) {
        fprintf(stderr, "File contains no video!\n");
        return 1;
    }

    // Init audio
    SDL_AudioSpec audio_spec;
    SDL_memset(&audio_spec, 0, sizeof(audio_spec));
    audio_spec.freq = player_info.audio_format.sample_rate;
    audio_spec.format = player_info.audio_format.format;
    audio_spec.channels = Kit_GetChannelLayoutCount(player_info.audio_format.layout);
    SDL_AudioStream *audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
    SDL_ResumeAudioStreamDevice(audio_stream);

    // Initialize video texture. This will probably end up as YV12 most of the time.
    SDL_Texture *video_tex = SDL_CreateTexture(
        renderer,
        player_info.video_format.format,
        SDL_TEXTUREACCESS_STATIC,
        player_info.video_format.width,
        player_info.video_format.height
    );
    if(video_tex == NULL) {
        fprintf(stderr, "Error while attempting to create a video texture\n");
        return 1;
    }
    SDL_SetTextureScaleMode(video_tex, SDL_SCALEMODE_LINEAR);

    // This is the subtitle texture atlas. This contains all the subtitle image fragments.
    SDL_Texture *subtitle_tex = SDL_CreateTexture(
        renderer, player_info.subtitle_format.format, SDL_TEXTUREACCESS_STATIC, ATLAS_WIDTH, ATLAS_HEIGHT
    );
    if(subtitle_tex == NULL) {
        fprintf(stderr, "Error while attempting to create a subtitle texture atlas\n");
        return 1;
    }
    SDL_SetTextureScaleMode(subtitle_tex, SDL_SCALEMODE_NEAREST); // Always nearest for atlas operations

    // Make sure subtitle texture is in correct blending mode
    SDL_SetTextureBlendMode(subtitle_tex, SDL_BLENDMODE_BLEND);

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
        renderer, player_info.video_format.width, player_info.video_format.height, SDL_LOGICAL_PRESENTATION_LETTERBOX
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
                case SDL_EVENT_KEY_UP:
                    if(event.key.key == SDLK_RIGHT)
                        Kit_PlayerSeek(player, Kit_GetPlayerPosition(player) + 10);
                    if(event.key.key == SDLK_LEFT)
                        Kit_PlayerSeek(player, Kit_GetPlayerPosition(player) - 10);
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
        const int got = Kit_GetPlayerSubtitleSDLTexture(player, subtitle_tex, sources, targets, ATLAS_MAX);
        for(int i = 0; i < got; i++) {
            SDL_RenderTexture(renderer, subtitle_tex, &sources[i], &targets[i]);
        }

        // Render to screen + wait for vsync
        SDL_RenderPresent(renderer);
    }

    Kit_ClosePlayer(player);
    Kit_CloseSource(src);
    fclose(fd);
    Kit_Quit();

    SDL_DestroyTexture(subtitle_tex);
    SDL_DestroyTexture(video_tex);
    SDL_DestroyAudioStream(audio_stream);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
