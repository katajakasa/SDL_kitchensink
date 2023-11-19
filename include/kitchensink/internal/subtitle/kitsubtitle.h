#ifndef KITSUBTITLE_H
#define KITSUBTITLE_H

#include <SDL_render.h>

#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/kittimer.h"
#include "kitchensink/kitconfig.h"
#include "kitchensink/kitsource.h"

KIT_LOCAL Kit_Decoder *Kit_CreateSubtitleDecoder(
    const Kit_Source *src,
    Kit_Timer *sync_timer,
    int stream_index,
    int video_w,
    int video_h,
    int screen_w,
    int screen_h
);
KIT_LOCAL void Kit_GetSubtitleDecoderTexture(const Kit_Decoder *dec, SDL_Texture *texture, double sync_ts);
KIT_LOCAL void Kit_SetSubtitleDecoderSize(const Kit_Decoder *dec, int w, int h);
KIT_LOCAL int Kit_GetSubtitleDecoderInfo(const Kit_Decoder *dec, SDL_Rect *sources, SDL_Rect *targets, int limit);
KIT_LOCAL int Kit_GetSubtitleDecoderOutputFormat(const Kit_Decoder *dec, Kit_SubtitleOutputFormat *output);

#endif // KITSUBTITLE_H
