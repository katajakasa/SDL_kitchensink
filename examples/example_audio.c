#include <SDL.h>
#include <limits.h>
#include <kitchensink/kitchensink.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * Note! This example does not do proper error handling etc.
 * It is for example use only!
 */

#define AUDIO_BUFFER_SIZE (1024 * 256)
#define AUDIO_BUFFER_PACKETS 16
#define AUDIO_BUFFER_FRAMES 32
#define AUDIO_PREBUFFER_FRAMES 8

int main(int argc, char *argv[]) {
    int err = 0, ret = 0;
    const char *filename = NULL;

    // Events
    bool run = true;

    // Kitchensink
    Kit_Source *src = NULL;
    Kit_Player *player = NULL;

    // Audio playback
    SDL_AudioSpec wanted_spec, audio_spec;
    SDL_AudioDeviceID audio_dev;
    char audio_buf[AUDIO_BUFFER_SIZE];

    // Get filename to open
    if(argc != 2) {
        fprintf(stderr, "Usage: audio <filename>\n");
        return 0;
    }
    filename = argv[1];

    // Init SDL
    err = SDL_Init(SDL_INIT_AUDIO);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize SDL!\n");
        return 1;
    }

    err = Kit_Init(KIT_INIT_NETWORK);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize Kitchensink: %s", Kit_GetError());
        return 1;
    }

    // Set input and output buffering to reduce latency
    Kit_SetHint(KIT_HINT_AUDIO_BUFFER_FRAMES, AUDIO_BUFFER_PACKETS);
    Kit_SetHint(KIT_HINT_AUDIO_BUFFER_PACKETS, AUDIO_BUFFER_FRAMES);

    // Open up the sourcefile.
    src = Kit_CreateSourceFromUrl(filename);
    if(src == NULL) {
        fprintf(stderr, "Unable to load file '%s': %s\n", filename, Kit_GetError());
        return 1;
    }

    // Print stream information
    Kit_SourceStreamInfo source_info;
    fprintf(stderr, "Source streams:\n");
    for(int i = 0; i < Kit_GetSourceStreamCount(src); i++) {
        err = Kit_GetSourceStreamInfo(src, &source_info, i);
        if(err) {
            fprintf(stderr, "Unable to fetch stream #%d information: %s.\n", i, Kit_GetError());
            return 1;
        }
        fprintf(stderr, " * Stream #%d: %s\n", i, Kit_GetKitStreamTypeString(source_info.type));
    }

    // Create the player. No video, pick best audio stream, no subtitles, no screen
    player = Kit_CreatePlayer(src, -1, Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO), -1, NULL, NULL, 0, 0);
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
        player_info.audio_format.channels,
        player_info.audio_format.bytes,
        player_info.audio_format.is_signed ? "signed" : "unsigned"
    );

    // Init audio
    SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));
    wanted_spec.freq = player_info.audio_format.sample_rate;
    wanted_spec.format = player_info.audio_format.format;
    wanted_spec.channels = player_info.audio_format.channels;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &audio_spec, 0);
    SDL_PauseAudioDevice(audio_dev, 0);

    // Flush output just in case
    fflush(stderr);

    // Start playback
    Kit_PlayerPlay(player);

    unsigned int buffer_now = 0, buffer_prev = 0;
    bool initial_buffering_done = false;
    while(run) {
        if(Kit_GetPlayerState(player) == KIT_STOPPED) {
            run = false;
            continue;
        }

        // Handle SDL events so that the application reacts to input.
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_QUIT:
                    run = false;
                    break;
            }
        }

        // When application starts, wait for audio to be buffered. This prevents stutter in network streams.
        if(!initial_buffering_done) {
            Kit_GetPlayerAudioBufferState(player, &buffer_now, NULL, NULL, NULL);
            if (buffer_now != buffer_prev) {
                fprintf(stderr, "Buffering, %d / %d ...\n", buffer_now, AUDIO_PREBUFFER_FRAMES);
                buffer_prev = buffer_now;
            }
            if (buffer_now < AUDIO_PREBUFFER_FRAMES) {
                SDL_Delay(100);
                continue;
            }
            initial_buffering_done = true;
        }

        // Refresh audio
        int queued = SDL_GetQueuedAudioSize(audio_dev);
        if(queued < AUDIO_BUFFER_SIZE) {
            ret = Kit_GetPlayerAudioData(player, UINT_MAX, (unsigned char *)audio_buf, AUDIO_BUFFER_SIZE - queued);
            if(ret > 0) {
                SDL_QueueAudio(audio_dev, audio_buf, ret);
            }
        }

        SDL_Delay(1);
    }

    Kit_ClosePlayer(player);
    Kit_CloseSource(src);
    Kit_Quit();

    SDL_CloseAudioDevice(audio_dev);
    SDL_Quit();
    return 0;
}
