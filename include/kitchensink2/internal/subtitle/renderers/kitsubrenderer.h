#ifndef KITSUBRENDERER_H
#define KITSUBRENDERER_H

/**
 * @brief Generic subtitle renderer interface. A Kit_SubtitleRenderer is a codec-specific backend
 * (see kitsubass.h for text/libass, kitsubimage.h for bitmap subtitles) plumbed together via
 * a fixed set of callbacks, dispatched by the Kit_* wrapper functions below. All callbacks are
 * optional (may be NULL) and are simply skipped by the dispatchers when unset.
 *
 * @file kitsubrenderer.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <SDL_render.h>

#include "kitchensink2/internal/kitdecoder.h"
#include "kitchensink2/internal/subtitle/kitatlas.h"

/**
 * @brief Opaque subtitle renderer handle. See kitsubass.h / kitsubimage.h for concrete backends.
 */
typedef struct Kit_SubtitleRenderer Kit_SubtitleRenderer;

/// Feeds one decoded AVSubtitle frame (@p src) with its pts/start/end display window to the renderer.
typedef void (*renderer_render_cb)(Kit_SubtitleRenderer *ren, void *src, double pts, double start, double end);
/// Packs the renderer's currently active subtitles into @p atlas / @p texture for @p current_pts.
typedef int (*renderer_get_data_cb)(
    Kit_SubtitleRenderer *ren, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts
);
/// Retrieves the renderer's currently active subtitles as raw RGBA pixel buffers for @p current_pts.
typedef int (*renderer_get_raw_frames_cb)(
    Kit_SubtitleRenderer *renderer, unsigned char ***frames, SDL_Rect **sources, SDL_Rect **targets, double current_pts
);
/// Updates the renderer's output screen size used for subtitle positioning/scaling.
typedef void (*renderer_set_size_cb)(Kit_SubtitleRenderer *ren, int w, int h);
/// Flushes any buffered/queued subtitle data held by the renderer.
typedef void (*renderer_flush_cb)(Kit_SubtitleRenderer *ren);
/// Aborts any blocking buffer waits, used to unblock the renderer before thread shutdown.
typedef void (*renderer_abort_cb)(Kit_SubtitleRenderer *ren);
/// Releases renderer-specific resources; the renderer struct itself is freed by the caller.
typedef void (*renderer_close_cb)(Kit_SubtitleRenderer *ren);

/**
 * @brief Subtitle renderer instance: the owning decoder, backend-private userdata, and the
 * set of callbacks implementing the backend's behavior.
 */
struct Kit_SubtitleRenderer {
    Kit_Decoder *decoder;                         ///< Decoder that owns/drives this renderer
    void *userdata;                               ///< Backend-private state (e.g. Kit_ASSSubtitleRenderer)
    renderer_render_cb render_cb;                 ///< Subtitle rendering function callback
    renderer_get_data_cb get_data_cb;             ///< Subtitle data getter function callback
    renderer_set_size_cb set_size_cb;             ///< Screen size setter function callback
    renderer_flush_cb flush_cb;                   ///< Flush subtitle renderer buffers
    renderer_abort_cb abort_cb;                   ///< Abort buffer waits before thread shutdown
    renderer_close_cb close_cb;                   ///< Subtitle renderer close function callback
    renderer_get_raw_frames_cb get_raw_frames_cb; ///< Get raw frames function callback
};

/**
 * @brief Allocates a subtitle renderer and wires up its callbacks and backend userdata.
 *
 * @param decoder decoder that owns this renderer
 * @param render_cb callback to feed decoded subtitle frames, or NULL
 * @param get_data_cb callback to pack active subtitles into a texture atlas, or NULL
 * @param get_raw_frames_cb callback to retrieve active subtitles as raw pixel buffers, or NULL
 * @param set_size_cb callback to update output screen size, or NULL
 * @param flush_cb callback to flush buffered subtitle data, or NULL
 * @param abort_cb callback to abort blocking buffer waits, or NULL
 * @param close_cb callback to release backend-private resources, or NULL
 * @param userdata backend-private state passed back to all callbacks
 * @return newly created renderer, or NULL on allocation failure
 */
KIT_LOCAL Kit_SubtitleRenderer *Kit_CreateSubtitleRenderer(
    Kit_Decoder *decoder,
    renderer_render_cb render_cb,
    renderer_get_data_cb get_data_cb,
    renderer_get_raw_frames_cb get_raw_frames_cb,
    renderer_set_size_cb set_size_cb,
    renderer_flush_cb flush_cb,
    renderer_abort_cb abort_cb,
    renderer_close_cb close_cb,
    void *userdata
);

/**
 * @brief Dispatches a decoded subtitle frame to the renderer's render callback, if set.
 *
 * @param renderer renderer to dispatch to; no-op if NULL
 * @param src decoded subtitle frame (backend-specific pointer, e.g. AVSubtitle*)
 * @param pts base presentation timestamp of the packet, in seconds
 * @param start subtitle display start offset relative to pts, in seconds
 * @param end subtitle display end offset relative to pts, in seconds
 */
KIT_LOCAL void
Kit_RunSubtitleRenderer(Kit_SubtitleRenderer *renderer, void *src, double pts, double start, double end);

/**
 * @brief Dispatches to the renderer's get_data callback to pack active subtitles into a texture.
 *
 * @param renderer renderer to query; returns 0 if NULL
 * @param atlas texture atlas that receives the packed subtitle items
 * @param texture destination texture that backs the atlas's packed regions
 * @param current_pts current playback timestamp used to select which subtitles are visible
 * @return callback's return value, or 0 if renderer or its callback is NULL
 */
KIT_LOCAL int Kit_GetSubtitleRendererSDLTexture(
    Kit_SubtitleRenderer *renderer, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts
);

/**
 * @brief Dispatches to the renderer's get_raw_frames callback to fetch active subtitles as raw
 * RGBA pixel buffers.
 *
 * @param renderer renderer to query; returns 0 if NULL
 * @param frames receives a pointer to the renderer-owned array of raw pixel buffer pointers
 * @param sources receives a pointer to the renderer-owned array of source rects
 * @param targets receives a pointer to the renderer-owned array of target rects
 * @param current_pts current playback timestamp used to select which subtitles are visible
 * @return number of frames available, or 0 if renderer or its callback is NULL
 */
KIT_LOCAL int Kit_GetSubtitleRendererRawFrames(
    Kit_SubtitleRenderer *renderer, unsigned char ***frames, SDL_Rect **sources, SDL_Rect **targets, double current_pts
);

/**
 * @brief Dispatches to the renderer's set_size callback to update the output screen size.
 *
 * @param renderer renderer to update; no-op if NULL
 * @param w new output width
 * @param h new output height
 */
KIT_LOCAL void Kit_SetSubtitleRendererSize(Kit_SubtitleRenderer *renderer, int w, int h);

/**
 * @brief Dispatches to the renderer's flush callback to discard buffered subtitle data.
 *
 * @param renderer renderer to flush
 */
KIT_LOCAL void Kit_FlushSubtitleRendererBuffers(Kit_SubtitleRenderer *renderer);

/**
 * @brief Dispatches to the renderer's abort callback to unblock any blocking buffer waits.
 *
 * @param renderer renderer to abort
 */
KIT_LOCAL void Kit_AbortSubtitleRenderer(Kit_SubtitleRenderer *renderer);

/**
 * @brief Dispatches to the renderer's close callback to release backend resources, then frees
 * the renderer struct itself.
 *
 * @param renderer renderer to close; no-op if NULL
 */
KIT_LOCAL void Kit_CloseSubtitleRenderer(Kit_SubtitleRenderer *renderer);

#endif // KITSUBRENDERER_H
