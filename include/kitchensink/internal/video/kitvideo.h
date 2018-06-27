#ifndef KITVIDEO_H
#define KITVIDEO_H

#include <SDL_render.h>

#include "kitchensink/kitconfig.h"
#include "kitchensink/kitsource.h"
#include "kitchensink/internal/kitdecoder.h"

KIT_LOCAL Kit_Decoder* Kit_CreateVideoDecoder(const Kit_Source *src, int stream_index);
KIT_LOCAL int Kit_GetVideoDecoderData(Kit_Decoder *dec, SDL_Texture *texture);

#endif // KITVIDEO_H
