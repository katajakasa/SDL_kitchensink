#ifndef KITAUDIO_H
#define KITAUDIO_H

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <libavformat/avformat.h>

#include "kitchensink/kitconfig.h"
#include "kitchensink/kitformats.h"
#include "kitchensink/kitplayer.h"
#include "kitchensink/internal/kitringbuffer.h"

#define KIT_ABUFFERSIZE 64

typedef struct Kit_AudioPacket {
    double pts;
    size_t original_size;
    Kit_RingBuffer *rb;
} Kit_AudioPacket;

KIT_LOCAL Kit_AudioPacket* _CreateAudioPacket(const char* data, size_t len, double pts);
KIT_LOCAL void _FreeAudioPacket(void *ptr);
KIT_LOCAL enum AVSampleFormat _FindAVSampleFormat(int format);
KIT_LOCAL unsigned int _FindAVChannelLayout(int channels);
KIT_LOCAL void _FindAudioFormat(enum AVSampleFormat fmt, int *bytes, bool *is_signed, unsigned int *format);
KIT_LOCAL void _HandleAudioPacket(Kit_Player *player, AVPacket *packet);

#endif // KITAUDIO_H
