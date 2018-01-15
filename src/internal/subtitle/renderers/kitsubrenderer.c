#include <stdlib.h>
#include <assert.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/subtitle/renderers/kitsubrenderer.h"


Kit_SubtitleRenderer* Kit_CreateSubtitleRenderer() {
    // Allocate renderer and make sure allocation was a success
    Kit_SubtitleRenderer *ren = calloc(1, sizeof(Kit_SubtitleRenderer));
    if(ren == NULL) {
        Kit_SetError("Unable to allocate kit subtitle renderer");
        return NULL;
    }
    return ren;
}

int Kit_RunSubtitleRenderer(Kit_SubtitleRenderer *ren, void *src, double start_pts, void *surface) {
    if(ren == NULL)
        return 1;
    if(ren->ren_render != NULL)
        return ren->ren_render(ren, src, start_pts, surface);
    return 1;
}

void Kit_CloseSubtitleRenderer(Kit_SubtitleRenderer *ren) {
    if(ren == NULL)
        return;
    if(ren->ren_close != NULL)
        ren->ren_close(ren);
    free(ren);
}
