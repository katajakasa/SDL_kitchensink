#ifndef KITVIDEO_H
#define KITVIDEO_H

#include <SDL_render.h>

#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/kittimer.h"
#include "kitchensink/kitconfig.h"
#include "kitchensink/kitsource.h"

KIT_LOCAL Kit_Decoder *Kit_CreateVideoDecoder(const Kit_Source *src, Kit_Timer *sync_timer, int stream_index);
KIT_LOCAL int Kit_GetVideoDecoderData(Kit_Decoder *dec, SDL_Texture *texture, SDL_Rect *area);
int Kit_GetVideoDecoderOutputFormat(const Kit_Decoder *dec, Kit_VideoOutputFormat *output);

#endif // KITVIDEO_H
