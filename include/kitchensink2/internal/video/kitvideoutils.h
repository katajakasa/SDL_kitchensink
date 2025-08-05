#ifndef KITVIDEOUTILS_H
#define KITVIDEOUTILS_H

#include "kitchensink/kitformat.h"

enum AVPixelFormat Kit_FindBestAVPixelFormat(enum AVPixelFormat fmt);
unsigned int Kit_FindSDLPixelFormat(enum AVPixelFormat fmt);
enum AVPixelFormat Kit_FindAVPixelFormat(unsigned int fmt);
Kit_HardwareDeviceType Kit_FindHWDeviceType(enum AVHWDeviceType type);

#endif // KITVIDEOUTILS_H
