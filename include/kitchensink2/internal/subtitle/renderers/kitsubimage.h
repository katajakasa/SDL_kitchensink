#ifndef KITSUBIMAGE_H
#define KITSUBIMAGE_H

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/subtitle/renderers/kitsubrenderer.h"
#include "kitchensink2/kitconfig.h"

KIT_LOCAL Kit_SubtitleRenderer *
Kit_CreateImageSubtitleRenderer(Kit_Decoder *dec, int video_w, int video_h, int screen_w, int screen_h);

#endif // KITSUBIMAGE_H
