#ifndef KITHELPERS_H
#define KITHELPERS_H

#include <stdbool.h>
#include <libavformat/avformat.h>
#include "kitchensink/kitconfig.h"

KIT_LOCAL double Kit_GetSystemTime();
KIT_LOCAL bool Kit_StreamIsFontAttachment(const AVStream *stream);

KIT_LOCAL int min2(int a, int b);

#endif // KITHELPERS_H
