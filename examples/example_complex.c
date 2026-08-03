#include <SDL3/SDL.h>
#include <kitchensink3/kitchensink.h>

#include "example_common.h"
#include <stdbool.h>
#include <stdio.h>

/*
 * Note! This example does not do proper error handling etc.
 * It is for example use only!
 */

#define AUDIO_BUFFER_SIZE (1024 * 32)
#define ATLAS_WIDTH 4096
#define ATLAS_HEIGHT 4096
#define ATLAS_MAX 1024

/**
 * @brief Prints the codec and format details of the player audio stream, if one is selected.
 *
 * @param player Player to query for the selected stream
 * @param player_info Player info to print
 */
void dump_audio_stream_info(const Kit_Player *player, const Kit_PlayerInfo *player_info) {
    if(Kit_GetPlayerAudioStream(player) >= 0) {
        fprintf(
            stderr,
            " * Audio: %s (%s), threads=%d, %dHz, %dch, %db, %s\n",
            player_info->audio_codec.name,
            player_info->audio_codec.description,
            player_info->video_codec.threads,
            player_info->audio_format.sample_rate,
            Kit_GetChannelLayoutCount(player_info->audio_format.layout),
            player_info->audio_format.bytes,
            player_info->audio_format.is_signed ? "signed" : "unsigned"
        );
    }
}

/**
 * @brief Prints the codec and format details of the player video stream, if one is selected.
 *
 * @param player Player to query for the selected stream
 * @param player_info Player info to print
 */
void dump_video_stream_info(const Kit_Player *player, const Kit_PlayerInfo *player_info) {
    if(Kit_GetPlayerVideoStream(player) >= 0) {
        fprintf(
            stderr,
            " * Video: %s (%s), threads=%d, %dx%d, hardware=%s\n",
            player_info->video_codec.name,
            player_info->video_codec.description,
            player_info->video_codec.threads,
            player_info->video_format.width,
            player_info->video_format.height,
            Kit_GetHardwareDecoderTypeString(player_info->video_format.hw_device_type)
        );
    }
}

/**
 * @brief Prints the codec details of the player subtitle stream, if one is selected.
 *
 * @param player Player to query for the selected stream
 * @param player_info Player info to print
 */
void dump_subtitle_stream_info(const Kit_Player *player, const Kit_PlayerInfo *player_info) {
    if(Kit_GetPlayerSubtitleStream(player) >= 0) {
        fprintf(
            stderr,
            " * Subtitle: %s (%s), threads=%d\n",
            player_info->subtitle_codec.name,
            player_info->subtitle_codec.description,
            player_info->video_codec.threads
        );
    }
}

/**
 * @brief Prints the player buffer states as a status line to the console.
 *
 * @param player Player to query for buffer states
 * @param tick Frame counter; the status line is only refreshed every 30th tick
 */
void render_buffer_bar(const Kit_Player *player, int tick) {
    if(tick % 30 != 0) // Restrict refresh rate
        return;
    unsigned int ao_len = 0, ao_max = 0, ai_len = 0, ai_max = 0;
    unsigned int vo_len = 0, vo_max = 0, vi_len = 0, vi_max = 0;
    unsigned int so_len = 0, so_max = 0, si_len = 0, si_max = 0;
    Kit_GetPlayerSubtitleBufferState(player, &so_len, &so_max, &si_len, &si_max);
    Kit_GetPlayerVideoBufferState(player, &vo_len, &vo_max, &vi_len, &vi_max);
    Kit_GetPlayerAudioBufferState(player, &ao_len, &ao_max, &ai_len, &ai_max);
    fprintf(
        stderr,
        "\rInput -> V:%3d/%3d A:%2d/%2d S:%2d/%2d, Output -> V:%d/%d A:%2d/%2d S:%4d/%4d",
        vi_len,
        vi_max,
        ai_len,
        ai_max,
        si_len,
        si_max,
        vo_len,
        vo_max,
        ao_len,
        ao_max,
        so_len,
        so_max
    );
    fflush(stderr);
}

/**
 * @brief Renders a simple progress bar to the bottom of the screen.
 *
 * @param renderer Renderer to draw with
 * @param percent Playback progress in range 0 .. 1
 */
void render_gui(SDL_Renderer *renderer, double percent) {
    // Get window size
    int size_w, size_h;
    SDL_RendererLogicalPresentation mode;
    SDL_GetRenderLogicalPresentation(renderer, &size_w, &size_h, &mode);

    // Render progress bar
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_FRect progress_border;
    progress_border.x = 28;
    progress_border.y = size_h - 61;
    progress_border.w = size_w - 57;
    progress_border.h = 22;
    SDL_RenderFillRect(renderer, &progress_border);

    SDL_SetRenderDrawColor(renderer, 155, 155, 155, 255);
    SDL_FRect progress_bottom;
    progress_bottom.x = 30;
    progress_bottom.y = size_h - 60;
    progress_bottom.w = size_w - 60;
    progress_bottom.h = 20;
    SDL_RenderFillRect(renderer, &progress_bottom);

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_FRect progress_top;
    progress_top.x = 30;
    progress_top.y = size_h - 60;
    progress_top.w = (size_w - 60) * percent;
    progress_top.h = 20;
    SDL_RenderFillRect(renderer, &progress_top);
}

/**
 * @brief Finds the largest size that fits inside the screen while keeping video aspect ratio.
 *
 * @param sw Screen width in pixels
 * @param sh Screen height in pixels
 * @param vw Video width in pixels
 * @param vh Video height in pixels
 * @param rw Filled with the resulting width
 * @param rh Filled with the resulting height
 */
void find_viewport_size(int sw, int sh, int vw, int vh, int *rw, int *rh) {
    const float r_x = (float)sw / (float)vw;
    const float r_y = (float)sh / (float)vh;
    const float r_t = r_x < r_y ? r_x : r_y;
    *rw = vw * r_t;
    *rh = vh * r_t;
}

int main(int argc, char *argv[]) {
    // Get filename to open
    const char *filename = get_filename_arg(argc, argv, "complex");

    // Init SDL
    initialize_sdl(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    // Create a resizable window and an accelerated, vsynced renderer.
    SDL_Window *window = create_window(filename, 1280, 720, 0);
    SDL_Renderer *renderer = create_renderer(window);

    // Initialize Kitchensink with network and libass support.
    const int err = Kit_Init(KIT_INIT_NETWORK | KIT_INIT_ASS | KIT_INIT_HW_DECODE);
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

    // Print stream information
    Kit_SourceStreamInfo source_info;
    fprintf(stderr, "Source streams:\n");
    for(int i = 0; i < Kit_GetSourceStreamCount(src); i++) {
        if(Kit_GetSourceStreamInfo(src, &source_info, i)) {
            fprintf(stderr, "Unable to fetch stream #%d information: %s.\n", i, Kit_GetError());
            return 1;
        }
        fprintf(stderr, " * Stream #%d: %s\n", i, Kit_GetKitStreamTypeString(source_info.type));
    }

    // Select which hardware decoders the player may use; this only matters since we
    // initialized Kitchensink with KIT_INIT_HW_DECODE. KIT_HWDEVICE_TYPE_ALL is the
    // default and allows any of them. To force pure software decoding, use
    // KIT_HWDEVICE_TYPE_NONE, and to allow only specific backends, combine them,
    // e.g. KIT_HWDEVICE_TYPE_VAAPI | KIT_HWDEVICE_TYPE_VDPAU.
    Kit_VideoFormatRequest video_request;
    Kit_ResetVideoFormatRequest(&video_request);
    video_request.hw_device_types = KIT_HWDEVICE_TYPE_ALL;

    // Set up the player configuration.
    Kit_PlayerConfig config;
    Kit_ResetPlayerConfig(&config);

    // Reduce buffering to use less memory
    // Note! Some video files may require larger buffers!
    config.video.frame_buffer_size = 1;

    // Create the player. Pick best streams available, and set subtitle
    // rendering resolution to screen resolution.
    Kit_Player *player = Kit_CreatePlayer(
        src,
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_SUBTITLE),
        &video_request,
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

    fprintf(stderr, "Media information:\n");
    dump_audio_stream_info(player, &player_info);
    dump_video_stream_info(player, &player_info);
    dump_subtitle_stream_info(player, &player_info);

    int num, den;
    if(Kit_GetPlayerAspectRatio(player, &num, &den) == 0) {
        fprintf(stderr, "Aspect ratio: %d:%d\n", num, den);
    }
    fprintf(stderr, "Duration: %f seconds\n", Kit_GetPlayerDuration(player));

    // Init audio. Note that audio_stream is reopened later if the user switches audio streams,
    // so it cannot be const here.
    SDL_AudioSpec audio_spec;
    SDL_memset(&audio_spec, 0, sizeof(audio_spec));
    audio_spec.freq = player_info.audio_format.sample_rate;
    audio_spec.format = player_info.audio_format.format;
    audio_spec.channels = Kit_GetChannelLayoutCount(player_info.audio_format.layout);
    SDL_AudioStream *audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
    SDL_ResumeAudioStreamDevice(audio_stream);

    // Print some format info
    fprintf(stderr, "Texture type: %s\n", Kit_GetSDLPixelFormatString(player_info.video_format.format));
    fprintf(stderr, "Audio format: %s\n", Kit_GetSDLAudioFormatString(player_info.audio_format.format));
    fprintf(stderr, "Audio layout: %s\n", Kit_GetChannelLayoutString(player_info.audio_format.layout));
    fprintf(stderr, "Subtitle format: %s\n", Kit_GetSDLPixelFormatString(player_info.subtitle_format.format));
    fflush(stderr);

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

    // Playback temporary data buffers
    char audio_buf[AUDIO_BUFFER_SIZE];
    SDL_Rect sources[ATLAS_MAX];
    SDL_Rect targets[ATLAS_MAX];
    int mouse_x = 0;
    int mouse_y = 0;
    int size_w = 0;
    int size_h = 0;
    int screen_w = 0;
    int screen_h = 0;
    int tick = 0;
    bool fullscreen = false;
    SDL_Rect video_area = {0, 0, 0, 0};

    // Get movie area size
    SDL_GetWindowSize(window, &screen_w, &screen_h);
    find_viewport_size(
        screen_w, screen_h, player_info.video_format.width, player_info.video_format.height, &size_w, &size_h
    );
    SDL_SetRenderLogicalPresentation(renderer, size_w, size_h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    Kit_SetPlayerScreenSize(player, size_w, size_h);

    // Start playback
    Kit_PlayerPlay(player);

    // Run until the user quits. When playback ends, the window goes black but stays open --
    // clicking the progress bar seeks, which restarts playback from the clicked position.
    bool run = true;
    while(run) {
        const bool stopped = Kit_GetPlayerState(player) == KIT_STOPPED;

        // Check for events
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            SDL_ConvertEventToRenderCoordinates(renderer, &event);
            switch(event.type) {
                case SDL_EVENT_KEY_UP:
                    if(event.key.key == SDLK_ESCAPE) {
                        run = false;
                    } else if(event.key.key == SDLK_S) {
                        const int current_index = Kit_GetPlayerStream(player, KIT_STREAMTYPE_SUBTITLE);
                        const int next_index = Kit_GetNextSourceStream(src, KIT_STREAMTYPE_SUBTITLE, current_index, 1);
                        if(Kit_SetPlayerStream(player, KIT_STREAMTYPE_SUBTITLE, next_index) != 0) {
                            fprintf(
                                stderr, "\33[2K\rFailed to set subtitle stream %d: %s\n", next_index, Kit_GetError()
                            );
                        } else {
                            fprintf(stderr, "\33[2K\rSetting subtitle stream %d\n", next_index);
                        }
                        fflush(stderr);
                    } else if(event.key.key == SDLK_V) {
                        const int current_index = Kit_GetPlayerStream(player, KIT_STREAMTYPE_VIDEO);
                        const int next_index = Kit_GetNextSourceStream(src, KIT_STREAMTYPE_VIDEO, current_index, 1);
                        if(Kit_SetPlayerStream(player, KIT_STREAMTYPE_VIDEO, next_index) != 0) {
                            fprintf(stderr, "\33[2K\rFailed to set video stream %d: %s\n", next_index, Kit_GetError());
                        } else {
                            fprintf(stderr, "\33[2K\rSetting video stream %d\n", next_index);
                        }
                        fflush(stderr);
                    } else if(event.key.key == SDLK_A) {
                        const int current_index = Kit_GetPlayerStream(player, KIT_STREAMTYPE_AUDIO);
                        const int next_index = Kit_GetNextSourceStream(src, KIT_STREAMTYPE_AUDIO, current_index, 1);
                        if(Kit_SetPlayerStream(player, KIT_STREAMTYPE_AUDIO, next_index) != 0) {
                            fprintf(stderr, "\33[2K\rFailed to set audio stream %d: %s\n", next_index, Kit_GetError());
                        } else {
                            fprintf(stderr, "\33[2K\rSetting audio stream %d\n", next_index);
                            Kit_GetPlayerInfo(player, &player_info);
                            SDL_memset(&audio_spec, 0, sizeof(audio_spec));
                            audio_spec.freq = player_info.audio_format.sample_rate;
                            audio_spec.format = player_info.audio_format.format;
                            audio_spec.channels = Kit_GetChannelLayoutCount(player_info.audio_format.layout);
                            SDL_DestroyAudioStream(audio_stream);
                            audio_stream =
                                SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
                            dump_audio_stream_info(player, &player_info);
                            SDL_ResumeAudioStreamDevice(audio_stream);
                        }
                        fflush(stderr);
                    }
                    break;

                case SDL_EVENT_KEY_DOWN: {
                    // Find alt+enter
                    const bool *state = SDL_GetKeyboardState(NULL);
                    if(state[SDL_SCANCODE_RETURN] && state[SDL_SCANCODE_LALT]) {
                        if(!fullscreen) {
                            SDL_SetWindowFullscreen(window, true);
                        } else {
                            SDL_SetWindowFullscreen(window, false);
                        }
                        fullscreen = !fullscreen;
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_MOTION:
                    mouse_x = event.motion.x;
                    mouse_y = event.motion.y;
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                    SDL_GetWindowSize(window, &screen_w, &screen_h);
                    find_viewport_size(
                        screen_w,
                        screen_h,
                        player_info.video_format.width,
                        player_info.video_format.height,
                        &size_w,
                        &size_h
                    );
                    SDL_SetRenderLogicalPresentation(renderer, size_w, size_h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
                    Kit_SetPlayerScreenSize(player, size_w, size_h);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    // Handle user clicking the progress bar
                    if(mouse_x >= 30 && mouse_x <= size_w - 30 && mouse_y >= size_h - 60 && mouse_y <= size_h - 40) {
                        const double pos = ((double)mouse_x - 30) / ((double)size_w - 60);
                        const double m_time = Kit_GetPlayerDuration(player) * pos;
                        if(Kit_PlayerSeek(player, m_time) != 0) {
                            fprintf(stderr, "%s\n", Kit_GetError());
                        }
                        SDL_ClearAudioStream(audio_stream);
                    } else {
                        // Handle pause
                        if(Kit_GetPlayerState(player) == KIT_PAUSED) {
                            Kit_PlayerPlay(player);
                        } else {
                            Kit_PlayerPause(player);
                        }
                    }
                    break;

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

        // Clear window first, in case of weirdly sized frames.
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        // Refresh the video texture and render it. Do not that Kit_GetPlayerVideoData does not change the texture
        // or the video area coords if there is no new video data! In that case, you should just use the old content.
        // A stopped player renders nothing, leaving the cleared (black) window.
        if(!stopped) {
            Kit_GetPlayerVideoSDLTexture(player, video_tex, &video_area);
            SDL_FRect video_area_f;
            SDL_RectToFRect(&video_area, &video_area_f);
            SDL_RenderTexture(renderer, video_tex, &video_area_f, NULL);

            // Refresh subtitle texture atlas and render subtitle frames from it
            // For subtitles, use screen size instead of video size for best quality
            if(subtitle_tex != NULL) {
                const int got = Kit_GetPlayerSubtitleSDLTexture(player, subtitle_tex, sources, targets, ATLAS_MAX);
                for(int i = 0; i < got; i++) {
                    SDL_FRect src_rect, dst_rect;
                    SDL_RectToFRect(&sources[i], &src_rect);
                    SDL_RectToFRect(&targets[i], &dst_rect);
                    SDL_RenderTexture(renderer, subtitle_tex, &src_rect, &dst_rect);
                }
            }
        }

        // Enable GUI if mouse is hovering over the bottom third of the screen
        if(mouse_y >= ((size_h / 3) * 2)) {
            const double percent = Kit_GetPlayerPosition(player) / Kit_GetPlayerDuration(player);
            render_gui(renderer, percent);
        }

        // Render to screen + wait for vsync
        SDL_RenderPresent(renderer);

        // Fetch buffering status and render the nice status to console
        render_buffer_bar(player, tick++);
    }

    // Ensure newline after status line.
    fprintf(stderr, "\n");

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
