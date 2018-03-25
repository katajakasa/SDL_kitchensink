#ifndef KITSUBRENDERER_H
#define KITSUBRENDERER_H

#include "kitchensink/kitsource.h"
#include "kitchensink/kitformats.h"

typedef struct Kit_SubtitleRenderer Kit_SubtitleRenderer;
//typedef struct Kit_SubtitlePacket Kit_SubtitlePacket;

typedef Kit_SubtitlePacket* (*ren_render_cb)(Kit_SubtitleRenderer *ren, void *src, double start_pts, double end_pts);
typedef void (*ren_close_cb)(Kit_SubtitleRenderer *ren);

struct Kit_SubtitleRenderer {
    void *userdata;
    ren_render_cb ren_render;    ///< Subtitle rendering function callback
    ren_close_cb ren_close;      ///< Subtitle renderer close function callback
};

KIT_LOCAL Kit_SubtitleRenderer* Kit_CreateSubtitleRenderer();
KIT_LOCAL Kit_SubtitlePacket* Kit_RunSubtitleRenderer(Kit_SubtitleRenderer *ren, void *src, double start_pts, double end_pts);
KIT_LOCAL void Kit_CloseSubtitleRenderer(Kit_SubtitleRenderer *ren);

#endif // KITSUBRENDERER_H
