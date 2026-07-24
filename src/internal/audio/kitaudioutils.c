#include "kitchensink2/internal/audio/kitaudioutils.h"

#include <SDL_audio.h>
#include <libavutil/samplefmt.h>

enum AVSampleFormat Kit_FindAVSampleFormat(int format)
{
    switch(format) {
        case AUDIO_U8:
            return AV_SAMPLE_FMT_U8;
        case AUDIO_S16SYS:
            return AV_SAMPLE_FMT_S16;
        case AUDIO_S32SYS:
            return AV_SAMPLE_FMT_S32;
        default:
            return AV_SAMPLE_FMT_NONE;
    }
}

void Kit_FindAVChannelLayout(Kit_AudioChannelLayout layout, AVChannelLayout *out) {
    switch(layout) {
        case KIT_LAYOUT_MONO:
            *out = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
            break;
        case KIT_LAYOUT_2POINT1:
            *out = (AVChannelLayout)AV_CHANNEL_LAYOUT_2POINT1;
            break;
        case KIT_LAYOUT_QUAD:
            *out = (AVChannelLayout)AV_CHANNEL_LAYOUT_QUAD;
            break;
        case KIT_LAYOUT_5POINT1:
            *out = (AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1_BACK;
            break;
        case KIT_LAYOUT_6POINT1:
            *out = (AVChannelLayout)AV_CHANNEL_LAYOUT_6POINT1;
            break;
        case KIT_LAYOUT_7POINT1:
            *out = (AVChannelLayout)AV_CHANNEL_LAYOUT_7POINT1;
            break;
        case KIT_LAYOUT_STEREO:
        default:
            *out = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
            break;
    }
}

Kit_AudioChannelLayout Kit_FindChannelLayout(const AVChannelLayout *channel_layout) {
    const int channels = channel_layout->nb_channels;
    if(channels >= 8)
        return KIT_LAYOUT_7POINT1;
    if(channels == 7)
        return KIT_LAYOUT_6POINT1;
    if(channels == 6)
        return KIT_LAYOUT_5POINT1;
    if(channels >= 4)
        return KIT_LAYOUT_QUAD;
    if(channels == 3)
        return KIT_LAYOUT_2POINT1;
    if(channels == 1)
        return KIT_LAYOUT_MONO;
    return KIT_LAYOUT_STEREO;
}

int Kit_FindBytes(enum AVSampleFormat fmt) {
    switch(fmt) {
        case AV_SAMPLE_FMT_U8P:
        case AV_SAMPLE_FMT_U8:
            return 1;
        case AV_SAMPLE_FMT_S32P:
        case AV_SAMPLE_FMT_S32:
            return 4;
        default:
            return 2;
    }
}

int Kit_FindSDLSampleFormat(enum AVSampleFormat fmt) {
    switch(fmt) {
        case AV_SAMPLE_FMT_U8P:
        case AV_SAMPLE_FMT_U8:
            return AUDIO_U8;
        case AV_SAMPLE_FMT_S32P:
        case AV_SAMPLE_FMT_S32:
            return AUDIO_S32SYS;
        default:
            return AUDIO_S16SYS;
    }
}

int Kit_FindSignedness(enum AVSampleFormat fmt) {
    switch(fmt) {
        case AV_SAMPLE_FMT_U8P:
        case AV_SAMPLE_FMT_U8:
            return 0;
        default:
            return 1;
    }
}