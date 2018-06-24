#ifndef KITAUDIO_H
#define KITAUDIO_H

#include "kitchensink/kitconfig.h"
#include "kitchensink/kitsource.h"
#include "kitchensink/internal/kitdecoder.h"

KIT_LOCAL Kit_Decoder* Kit_CreateAudioDecoder(const Kit_Source *src, int stream_index);
KIT_LOCAL int Kit_GetAudioDecoderData(Kit_Decoder *dec, unsigned char *buf, int len);

#endif // KITAUDIO_H
