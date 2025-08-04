#ifndef KITVIDEO_H
#define KITVIDEO_H

#include <SDL_render.h>

#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/kittimer.h"
#include "kitchensink/kitconfig.h"
#include "kitchensink/kitsource.h"

KIT_LOCAL Kit_Decoder *Kit_CreateVideoDecoder(const Kit_Source *src, const Kit_VideoFormatRequest *format_request, Kit_Timer *sync_timer, int stream_index);
KIT_LOCAL int Kit_GetVideoDecoderSDLTexture(Kit_Decoder *dec, SDL_Texture *texture, SDL_Rect *area);
KIT_LOCAL int Kit_LockVideoDecoderRaw(Kit_Decoder *decoder, unsigned char ***data, int **line_size, SDL_Rect *area);
KIT_LOCAL void Kit_UnlockVideoDecoderRaw(Kit_Decoder *decoder);
KIT_LOCAL int Kit_GetVideoDecoderOutputFormat(const Kit_Decoder *dec, Kit_VideoOutputFormat *output);

#endif // KITVIDEO_H
