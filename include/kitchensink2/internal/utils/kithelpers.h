#ifndef KITHELPERS_H
#define KITHELPERS_H

#include "kitchensink2/kitconfig.h"
#include <libavformat/avformat.h>
#include <stdbool.h>

KIT_LOCAL double Kit_GetSystemTime();
KIT_LOCAL bool Kit_StreamIsFontAttachment(const AVStream *stream);

KIT_LOCAL int Kit_max(int a, int b);
KIT_LOCAL int Kit_min(int a, int b);
KIT_LOCAL int Kit_clamp(int v, int min, int max);

#endif // KITHELPERS_H
