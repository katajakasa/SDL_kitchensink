#ifndef KITVIDEOUTILS_H
#define KITVIDEOUTILS_H

enum AVPixelFormat Kit_FindBestAVPixelFormat(enum AVPixelFormat fmt);
unsigned int Kit_FindSDLPixelFormat(enum AVPixelFormat fmt);
enum AVPixelFormat Kit_FindAVPixelFormat(unsigned int fmt);

#endif // KITVIDEOUTILS_H
