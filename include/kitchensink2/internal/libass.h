#ifndef KITLIBASS_H
#define KITLIBASS_H

/**
 * @brief libass API surface used by SDL_kitchensink. When USE_DYNAMIC_LIBASS is defined, the
 * types and functions below are declared locally and the function pointers are resolved at
 * runtime via load_libass(); otherwise the real libass headers are used directly.
 *
 * @file libass.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#ifndef USE_DYNAMIC_LIBASS

#include <ass/ass.h>

#else // USE_DYNAMIC_LIBASS

#include <stdarg.h>
#include <stdint.h>

#include "kitchensink2/kitconfig.h"

/** @brief Opaque libass library handle (mirrors ass_library from libass). */
typedef struct ass_library ASS_Library;
/** @brief Opaque libass renderer handle (mirrors ass_renderer from libass). */
typedef struct ass_renderer ASS_Renderer;
/** @brief Opaque libass subtitle track handle (mirrors ass_track from libass). */
typedef struct ass_track ASS_Track;

/** @brief A single rendered subtitle bitmap image, as returned by ass_render_frame(). */
typedef struct ass_image {
    int w, h;
    int stride;
    unsigned char *bitmap;
    uint32_t color;
    int dst_x, dst_y;
    struct ass_image *next;
    enum
    {
        IMAGE_TYPE_CHARACTER,
        IMAGE_TYPE_OUTLINE,
        IMAGE_TYPE_SHADOW
    } type;
} ASS_Image;

/** @brief Font hinting mode passed to ass_set_hinting(). */
typedef enum
{
    ASS_HINTING_NONE = 0,
    ASS_HINTING_LIGHT,
    ASS_HINTING_NORMAL,
    ASS_HINTING_NATIVE
} ASS_Hinting;

/** @brief Function pointer for libass's ass_library_init(), resolved by load_libass(). */
extern KIT_LOCAL ASS_Library *(*ass_library_init)(void);
/** @brief Function pointer for libass's ass_library_done(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_library_done)(ASS_Library *priv);
/** @brief Function pointer for libass's ass_process_codec_private(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_process_codec_private)(ASS_Track *track, char *data, int size);
/** @brief Function pointer for libass's ass_set_message_cb(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_set_message_cb)(
    ASS_Library *priv, void (*msg_cb)(int level, const char *fmt, va_list args, void *data), void *data
);
/** @brief Function pointer for libass's ass_renderer_init(), resolved by load_libass(). */
extern KIT_LOCAL ASS_Renderer *(*ass_renderer_init)(ASS_Library *);
/** @brief Function pointer for libass's ass_renderer_done(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_renderer_done)(ASS_Renderer *priv);
/** @brief Function pointer for libass's ass_set_frame_size(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_set_frame_size)(ASS_Renderer *priv, int w, int h);
/** @brief Function pointer for libass's ass_set_hinting(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_set_hinting)(ASS_Renderer *priv, ASS_Hinting ht);
/** @brief Function pointer for libass's ass_set_fonts(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_set_fonts)(
    ASS_Renderer *priv, const char *default_font, const char *default_family, int dfp, const char *config, int update
);
/** @brief Function pointer for libass's ass_render_frame(), resolved by load_libass(). */
extern KIT_LOCAL ASS_Image *(*ass_render_frame)(
    ASS_Renderer *priv, ASS_Track *track, long long now, int *detect_change
);
/** @brief Function pointer for libass's ass_new_track(), resolved by load_libass(). */
extern KIT_LOCAL ASS_Track *(*ass_new_track)(ASS_Library *);
/** @brief Function pointer for libass's ass_free_track(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_free_track)(ASS_Track *track);
/** @brief Function pointer for libass's ass_process_data(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_process_data)(ASS_Track *track, char *data, int size);
/** @brief Function pointer for libass's ass_process_chunk(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_process_chunk)(
    ASS_Track *track, char *data, int size, long long timecode, long long duration
);
/** @brief Function pointer for libass's ass_add_font(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_add_font)(ASS_Library *library, char *name, char *data, int data_size);
/** @brief Function pointer for libass's ass_set_storage_size(), resolved by load_libass(). */
extern KIT_LOCAL void (*ass_set_storage_size)(ASS_Renderer *priv, int w, int h);

/**
 * @brief Loads all libass function pointers above from a dynamically loaded shared object handle.
 *
 * @param handle Shared object handle (as returned by SDL_LoadObject()) to resolve symbols from
 * @return 0 on success (all required symbols resolved), 1 if any required symbol failed to load
 */
KIT_LOCAL int load_libass(void *handle);

#endif // USE_DYNAMIC_LIBASS

// For compatibility
#ifndef ASS_FONTPROVIDER_AUTODETECT
#define ASS_FONTPROVIDER_AUTODETECT 1
#endif

#endif // KITLIBASS_H
