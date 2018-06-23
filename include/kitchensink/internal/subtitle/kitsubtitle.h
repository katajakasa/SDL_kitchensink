#ifndef KITSUBTITLE_H
#define KITSUBTITLE_H

#include <SDL2/SDL_render.h>

#include "kitchensink/kitconfig.h"
#include "kitchensink/kitformats.h"
#include "kitchensink/kitplayer.h"
#include "kitchensink/internal/kitdecoder.h"

KIT_LOCAL Kit_Decoder* Kit_CreateSubtitleDecoder(const Kit_Source *src, int stream_index, Kit_SubtitleFormat *format, int w, int h);
KIT_LOCAL int Kit_GetSubtitleDecoderData(Kit_Decoder *dec, SDL_Texture *texture, SDL_Rect *sources, SDL_Rect *targets, int limit);
KIT_LOCAL void Kit_SetSubtitleDecoderSize(Kit_Decoder *dec, int w, int h);

#endif // KITSUBTITLE_H
