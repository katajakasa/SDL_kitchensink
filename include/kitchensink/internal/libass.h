#ifndef KITLIBASS_H
#define KITLIBASS_H

#ifndef USE_DYNAMIC_LIBASS

#include <ass/ass.h>

#else // USE_DYNAMIC_LIBASS

#include <stdint.h>
#include <stdarg.h>

typedef struct ass_library ASS_Library;
typedef struct ass_renderer ASS_Renderer;
typedef struct ass_track ASS_Track;

typedef struct ass_image {
    int w, h;
    int stride;
    unsigned char *bitmap;
    uint32_t color;
    int dst_x, dst_y;
    struct ass_image *next;
    enum {
        IMAGE_TYPE_CHARACTER,
        IMAGE_TYPE_OUTLINE,
        IMAGE_TYPE_SHADOW
    } type;
} ASS_Image;

typedef enum {
    ASS_HINTING_NONE = 0,
    ASS_HINTING_LIGHT,
    ASS_HINTING_NORMAL,
    ASS_HINTING_NATIVE
} ASS_Hinting;

ASS_Library* (*ass_library_init)(void);
void (*ass_library_done)(ASS_Library *priv);
void (*ass_process_codec_private)(ASS_Track *track, char *data, int size);
void (*ass_set_message_cb)(ASS_Library *priv, void (*msg_cb)(int level, const char *fmt, va_list args, void *data), void *data);
ASS_Renderer* (*ass_renderer_init)(ASS_Library *);
void (*ass_renderer_done)(ASS_Renderer *priv);
void (*ass_set_frame_size)(ASS_Renderer *priv, int w, int h);
void (*ass_set_hinting)(ASS_Renderer *priv, ASS_Hinting ht);
void (*ass_set_fonts)(ASS_Renderer *priv, const char *default_font, const char *default_family, int dfp, const char *config, int update);
ASS_Image* (*ass_render_frame)(ASS_Renderer *priv, ASS_Track *track, long long now, int *detect_change);
ASS_Track* (*ass_new_track)(ASS_Library *);
void (*ass_free_track)(ASS_Track *track);
void (*ass_process_data)(ASS_Track *track, char *data, int size);
void (*ass_add_font)(ASS_Library *library, char *name, char *data, int data_size);

int load_libass(void *handle);

#endif // USE_DYNAMIC_LIBASS

#endif // KITLIBASS_H
