#ifndef KITPLAYER_H
#define KITPLAYER_H

#include "kitchensink/kitsource.h"
#include "kitchensink/kitconfig.h"
#include "kitchensink/kitformat.h"
#include "kitchensink/kitcodec.h"

#include <SDL2/SDL_render.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_mutex.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Kit_PlayerState {
    KIT_STOPPED = 0, ///< Playback stopped or has not started yet.
    KIT_PLAYING,     ///< Playback started & player is actively decoding.
    KIT_PAUSED,      ///< Playback paused; player is actively decoding but no new data is given out.
    KIT_CLOSED,      ///< Playback is stopped and player is closing.
} Kit_PlayerState;

typedef struct Kit_Player {
    Kit_PlayerState state;   ///< Playback state
    void *decoders[3];       ///< Decoder contexts
    SDL_Thread *dec_thread;  ///< Decoder thread
    SDL_mutex *dec_lock;     ///< Decoder lock
    const Kit_Source *src;   ///< Reference to Audio/Video source
    double pause_started;    ///< Temporary flag for handling pauses
} Kit_Player;

typedef struct Kit_PlayerStreamInfo {
    Kit_Codec codec;
    Kit_OutputFormat output;
} Kit_PlayerStreamInfo;

typedef struct Kit_PlayerInfo {
    Kit_PlayerStreamInfo video;
    Kit_PlayerStreamInfo audio;
    Kit_PlayerStreamInfo subtitle;
} Kit_PlayerInfo;

KIT_API Kit_Player* Kit_CreatePlayer(const Kit_Source *src,
                                     int video_stream_index,
                                     int audio_stream_index,
                                     int subtitle_stream_index,
                                     int screen_w,
                                     int screen_h);
KIT_API void Kit_ClosePlayer(Kit_Player *player);

KIT_API void Kit_SetPlayerScreenSize(Kit_Player *player, int w, int h);
KIT_API int Kit_GetPlayerVideoStream(const Kit_Player *player);
KIT_API int Kit_GetPlayerAudioStream(const Kit_Player *player);
KIT_API int Kit_GetPlayerSubtitleStream(const Kit_Player *player);

KIT_API int Kit_UpdatePlayer(Kit_Player *player);
KIT_API int Kit_GetPlayerVideoData(Kit_Player *player, SDL_Texture *texture);
KIT_API int Kit_GetPlayerSubtitleData(Kit_Player *player,
                                      SDL_Texture *texture,
                                      SDL_Rect *sources,
                                      SDL_Rect *targets,
                                      int limit);
KIT_API int Kit_GetPlayerAudioData(Kit_Player *player, unsigned char *buffer, int length);
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
