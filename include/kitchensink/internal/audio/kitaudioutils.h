#ifndef KITAUDIOUTILS_H
#define KITAUDIOUTILS_H

#include "kitchensink/kitconfig.h"
#include <libavcodec/avcodec.h>

KIT_LOCAL enum AVSampleFormat Kit_FindAVSampleFormat(int format);
KIT_LOCAL void Kit_FindAVChannelLayout(int channels, AVChannelLayout *layout);
KIT_LOCAL int Kit_FindChannelLayout(const AVChannelLayout *channel_layout);
KIT_LOCAL int Kit_FindBytes(enum AVSampleFormat fmt);
KIT_LOCAL int Kit_FindSDLSampleFormat(enum AVSampleFormat fmt);
KIT_LOCAL int Kit_FindSignedness(enum AVSampleFormat fmt);

#endif // KITAUDIOUTILS_H
