#ifndef KITSUBASS_H
#define KITSUBASS_H

#include "kitchensink/internal/kitdecoder.h"
#include "kitchensink/internal/subtitle/renderers/kitsubrenderer.h"
#include "kitchensink/kitconfig.h"

KIT_LOCAL Kit_SubtitleRenderer *Kit_CreateASSSubtitleRenderer(
    const AVFormatContext *format_ctx, Kit_Decoder *dec, int video_w, int video_h, int screen_w, int screen_h
);

#endif // KITSUBASS_H
