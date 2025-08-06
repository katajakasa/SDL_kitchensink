#ifndef KITAUDIO_H
#define KITAUDIO_H

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/kittimer.h"
#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitsource.h"

KIT_LOCAL Kit_Decoder *Kit_CreateAudioDecoder(
    const Kit_Source *src, const Kit_AudioFormatRequest *format_request, Kit_Timer *sync_timer, int stream_index
);
KIT_LOCAL int Kit_GetAudioDecoderData(Kit_Decoder *dec, size_t backend_buffer_size, unsigned char *buf, size_t len);
KIT_LOCAL int Kit_GetAudioDecoderOutputFormat(const Kit_Decoder *dec, Kit_AudioOutputFormat *output);

#endif // KITAUDIO_H
