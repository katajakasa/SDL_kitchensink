#include "kitchensink2/internal/audio/kitaudioutils.h"

#include <SDL_audio.h>

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

void Kit_FindAVChannelLayout(int channels, AVChannelLayout *layout) {
    switch(channels) {
        case 1:
            *layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        case 2:
            *layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        default:
            *layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    }
}

int Kit_FindChannelLayout(const AVChannelLayout *channel_layout) {
    if(channel_layout->nb_channels > 2)
        return 2;
    return channel_layout->nb_channels;
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