#ifndef KITPLAYER_H
#define KITPLAYER_H

#include "kitchensink/kitsource.h"
#include "kitchensink/kitconfig.h"

#include <SDL2/SDL_render.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_surface.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KIT_CODECMAX 16
#define KIT_CODECNAMEMAX 128

typedef enum Kit_PlayerState {
    KIT_STOPPED = 0, ///< Playback stopped or has not started yet.
    KIT_PLAYING, ///< Playback started & player is actively decoding.
    KIT_PAUSED, ///< Playback paused; player is actively decoding but no new data is given out.
    KIT_CLOSED ///< Playback is stopped and player is closing.
} Kit_PlayerState;

typedef struct Kit_AudioFormat {
    int stream_idx; ///< Stream index
    bool is_enabled; ///< Is stream enabled
    unsigned int format; ///< SDL Audio Format
    bool is_signed; ///< Signedness
    int bytes; ///< Bytes per sample per channel
    int samplerate; ///< Sampling rate
    int channels; ///< Channels
} Kit_AudioFormat;

typedef struct Kit_VideoFormat {
    int stream_idx; ///< Stream index
    bool is_enabled; ///< Is stream enabled
    unsigned int format; ///< SDL Pixel Format
    int width; ///< Width in pixels
    int height; ///< Height in pixels
} Kit_VideoFormat;

typedef struct Kit_SubtitleFormat {
    int stream_idx; ///< Stream index
    bool is_enabled; ///< Is stream enabled
} Kit_SubtitleFormat;

typedef struct Kit_Player {
    // Local state
    Kit_PlayerState state; ///< Playback state
    Kit_VideoFormat vformat; ///< Video format information
    Kit_AudioFormat aformat; ///< Audio format information
    Kit_SubtitleFormat sformat; ///< Subtitle format information

    // Synchronization
    double clock_sync; ///< Clock sync point
    double pause_start; ///< Timestamp of pause beginning
    double vclock_pos; ///< Video stream last pts

    // Threading
    SDL_Thread *dec_thread; ///< Decoder thread
    SDL_mutex *vmutex; ///< Video stream buffer lock
    SDL_mutex *amutex; ///< Audio stream buffer lock
    SDL_mutex *smutex; ///< Subtitle stream buffer lock
    SDL_mutex *cmutex; ///< Control stream buffer lock

    // Buffers
    void *abuffer; ///< Audio stream buffer
    void *vbuffer; ///< Video stream buffer
    void *sbuffer; ///< Subtitle stream buffer
    void *cbuffer; ///< Control stream buffer

    // FFmpeg internal state
    void *vcodec_ctx; ///< FFmpeg: Video codec context
    void *acodec_ctx; ///< FFmpeg: Audio codec context
    void *scodec_ctx; ///< FFmpeg: Subtitle codec context
    void *tmp_vframe; ///< FFmpeg: Preallocated temporary video frame
    void *tmp_aframe; ///< FFmpeg: Preallocated temporary audio frame
    void *tmp_sframe; ///< FFmpeg: Preallocated temporary subtitle frame
    void *swr; ///< FFmpeg: Audio resampler
    void *sws; ///< FFmpeg: Video converter

    // libass
    void *ass_renderer;
    void *ass_track;

    // Other
    uint8_t seek_flag;
    const Kit_Source *src; ///< Reference to Audio/Video source
} Kit_Player;

typedef struct Kit_PlayerInfo {
    char acodec[KIT_CODECMAX]; ///< Audio codec short name, eg "ogg", "mp3"
    char acodec_name[KIT_CODECNAMEMAX]; ///< Audio codec long, more descriptive name
    char vcodec[KIT_CODECMAX]; ///< Video codec short name, eg. "x264"
    char vcodec_name[KIT_CODECNAMEMAX]; ///< Video codec long, more descriptive name
    char scodec[KIT_CODECMAX]; ///< Subtitle codec short name, eg. "ass"
    char scodec_name[KIT_CODECNAMEMAX]; ///< Subtitle codec long, more descriptive name
    Kit_VideoFormat video; ///< Video format information
    Kit_AudioFormat audio; ///< Audio format information
    Kit_SubtitleFormat subtitle; ///< Subtitle format information
} Kit_PlayerInfo;

KIT_API Kit_Player* Kit_CreatePlayer(const Kit_Source *src);
KIT_API void Kit_ClosePlayer(Kit_Player *player);

KIT_API int Kit_UpdatePlayer(Kit_Player *player);
KIT_API int Kit_GetVideoData(Kit_Player *player, SDL_Texture *texture);
KIT_API int Kit_GetSubtitleData(Kit_Player *player, SDL_Renderer *renderer);
KIT_API int Kit_GetAudioData(Kit_Player *player, unsigned char *buffer, int length, int cur_buf_len);
KIT_API void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info);

KIT_API Kit_PlayerState Kit_GetPlayerState(const Kit_Player *player);
KIT_API void Kit_PlayerPlay(Kit_Player *player);
KIT_API void Kit_PlayerStop(Kit_Player *player);
KIT_API void Kit_PlayerPause(Kit_Player *player);

KIT_API int Kit_PlayerSeek(Kit_Player *player, double time);
KIT_API double Kit_GetPlayerDuration(const Kit_Player *player);
KIT_API double Kit_GetPlayerPosition(const Kit_Player *player);

#ifdef __cplusplus
}
#endif

#endif // KITPLAYER_H
