#include <SDL3/SDL.h>
#include <kitchensink3/kitchensink.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

#include "example_common.h"

/*
 * Note! This example does not do proper error handling etc.
 * It is for example use only!
 */

#define AUDIO_BUFFER_SIZE (1024 * 64)

int main(int argc, char *argv[]) {
    // Get filename to open
    const char *filename = get_filename_arg(argc, argv, "audio");

    // Init SDL
    initialize_sdl(SDL_INIT_AUDIO);

    const int err = Kit_Init(KIT_INIT_NETWORK);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize Kitchensink: %s", Kit_GetError());
        return 1;
    }

    // Open up the sourcefile.
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

    // Request a fixed audio output format from the player: signed 16bit stereo at 48kHz,
    // regardless of what the source file contains. This is useful when the audio backend
    // wants its input in one known format. Note that if the source format differs, the
    // player converts in software, which costs some performance. Any field can also be
    // left at its Kit_ResetAudioFormatRequest() default to keep the source value.
    Kit_AudioFormatRequest audio_request;
    Kit_ResetAudioFormatRequest(&audio_request);
    audio_request.format = SDL_AUDIO_S16;
    audio_request.is_signed = 1;
    audio_request.bytes = 2;
    audio_request.sample_rate = 48000;
    audio_request.layout = KIT_LAYOUT_STEREO;

    // Set up the player configuration.
    Kit_PlayerConfig config;
    Kit_ResetPlayerConfig(&config);

    // Set input and output buffering to reduce latency
    config.audio.frame_buffer_size = 24;
    config.audio.packet_buffer_size = 32;

    // Loosen up sync thresholds -- this should help with stuttering in network streams.
    config.audio.early_threshold = 30;
    config.audio.late_threshold = 100;

    // Create the player. No video, pick best audio stream, no subtitles, no screen
    Kit_Player *player = Kit_CreatePlayer(
        src, -1, Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO), -1, NULL, &audio_request, 0, 0, &config
    );
    if(player == NULL) {
        fprintf(stderr, "Unable to create player: %s\n", Kit_GetError());
        return 1;
    }

    // Print some information
    Kit_PlayerInfo player_info;
    Kit_GetPlayerInfo(player, &player_info);

    // Make sure there is audio in the file to play first.
    if(Kit_GetPlayerAudioStream(player) == -1) {
        fprintf(stderr, "File contains no audio!\n");
        return 1;
    }

    fprintf(stderr, "Media information:\n");
    fprintf(
        stderr,
        " * Audio: %s (%s), %dHz, %dch, %db, %s\n",
        player_info.audio_codec.name,
        player_info.audio_codec.description,
        player_info.audio_format.sample_rate,
        Kit_GetChannelLayoutCount(player_info.audio_format.layout),
        player_info.audio_format.bytes,
        player_info.audio_format.is_signed ? "signed" : "unsigned"
    );

    // Init audio
    SDL_AudioSpec audio_spec;
    SDL_memset(&audio_spec, 0, sizeof(audio_spec));
    audio_spec.freq = player_info.audio_format.sample_rate;
    audio_spec.format = player_info.audio_format.format;
    audio_spec.channels = Kit_GetChannelLayoutCount(player_info.audio_format.layout);
    SDL_AudioStream *audio_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
    SDL_ResumeAudioStreamDevice(audio_stream);

    // Flush output just in case
    fflush(stderr);

    // Start playback
    Kit_PlayerPlay(player);

    // Audio playback buffer
    char audio_buf[AUDIO_BUFFER_SIZE];

    bool is_buffering = false;
    bool run = true;
    while(run) {
        if(Kit_GetPlayerState(player) == KIT_STOPPED) {
            run = false;
            continue;
        }

        // Handle SDL events so that the application reacts to input.
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_EVENT_QUIT:
                    run = false;
                    break;
            }
        }

        // If audio buffers fall below given threshold, pause playback and start buffering.
        if(!is_buffering) {
            if(!Kit_HasBufferFillRate(player, -1, 10, -1, -1)) {
                Kit_PlayerPause(player);
                is_buffering = true;
            }
        } else {
            if(Kit_WaitBufferFillRate(player, 50, 50, -1, -1, 1.0) == 1) {
                fprintf(stderr, "Buffering ...\n");
            } else {
                is_buffering = false;
                Kit_PlayerPlay(player);
            }
        }

        // Fetch as many audio samples as the decoder is willing to give.
        int queued, ret;
        do {
            queued = SDL_GetAudioStreamQueued(audio_stream);
            ret = Kit_GetPlayerAudioData(player, UINT_MAX, (unsigned char *)audio_buf, AUDIO_BUFFER_SIZE - queued);
            if(ret > 0) {
                SDL_PutAudioStreamData(audio_stream, audio_buf, ret);
            }
        } while(ret > 0 && queued < AUDIO_BUFFER_SIZE);

        SDL_Delay(1);
    }

    Kit_ClosePlayer(player);
    Kit_CloseSource(src);
    Kit_Quit();

    SDL_DestroyAudioStream(audio_stream);
    SDL_Quit();
    return 0;
}
