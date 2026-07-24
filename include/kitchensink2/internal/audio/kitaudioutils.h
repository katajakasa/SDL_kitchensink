#ifndef KITAUDIOUTILS_H
#define KITAUDIOUTILS_H

#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitformat.h"

#include <libavutil/channel_layout.h>

KIT_LOCAL enum AVSampleFormat Kit_FindAVSampleFormat(int format);
KIT_LOCAL void Kit_FindAVChannelLayout(Kit_AudioChannelLayout layout, AVChannelLayout *out);
KIT_LOCAL Kit_AudioChannelLayout Kit_FindChannelLayout(const AVChannelLayout *channel_layout);
KIT_LOCAL int Kit_FindBytes(enum AVSampleFormat fmt);
KIT_LOCAL int Kit_FindSDLSampleFormat(enum AVSampleFormat fmt);
KIT_LOCAL int Kit_FindSignedness(enum AVSampleFormat fmt);

#endif // KITAUDIOUTILS_H
