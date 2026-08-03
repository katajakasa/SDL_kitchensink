#ifdef USE_DYNAMIC_LIBASS

#include "kitchensink3/internal/libass.h"
#include <SDL3/SDL_loadso.h>

ASS_Library *(*ass_library_init)(void);
void (*ass_library_done)(ASS_Library *priv);
void (*ass_process_codec_private)(ASS_Track *track, char *data, int size);
void (*ass_set_message_cb)(
    ASS_Library *priv, void (*msg_cb)(int level, const char *fmt, va_list args, void *data), void *data
);
ASS_Renderer *(*ass_renderer_init)(ASS_Library *);
void (*ass_renderer_done)(ASS_Renderer *priv);
void (*ass_set_frame_size)(ASS_Renderer *priv, int w, int h);
void (*ass_set_hinting)(ASS_Renderer *priv, ASS_Hinting ht);
void (*ass_set_fonts)(
    ASS_Renderer *priv, const char *default_font, const char *default_family, int dfp, const char *config, int update
);
ASS_Image *(*ass_render_frame)(ASS_Renderer *priv, ASS_Track *track, long long now, int *detect_change);
ASS_Track *(*ass_new_track)(ASS_Library *);
void (*ass_free_track)(ASS_Track *track);
void (*ass_process_data)(ASS_Track *track, char *data, int size);
void (*ass_process_chunk)(ASS_Track *track, char *data, int size, long long timecode, long long duration);
void (*ass_add_font)(ASS_Library *library, char *name, char *data, int data_size);
void (*ass_set_storage_size)(ASS_Renderer *priv, int w, int h);

#define KIT_LOAD_SYM(handle, name) name = (typeof(name))SDL_LoadFunction((handle), #name)

int load_libass(SDL_SharedObject *handle) {
    KIT_LOAD_SYM(handle, ass_library_init);
    KIT_LOAD_SYM(handle, ass_library_done);
    KIT_LOAD_SYM(handle, ass_set_message_cb);
    KIT_LOAD_SYM(handle, ass_renderer_init);
    KIT_LOAD_SYM(handle, ass_renderer_done);
    KIT_LOAD_SYM(handle, ass_set_frame_size);
    KIT_LOAD_SYM(handle, ass_set_hinting);
    KIT_LOAD_SYM(handle, ass_set_fonts);
    KIT_LOAD_SYM(handle, ass_render_frame);
    KIT_LOAD_SYM(handle, ass_new_track);
    KIT_LOAD_SYM(handle, ass_free_track);
    KIT_LOAD_SYM(handle, ass_process_data);
    KIT_LOAD_SYM(handle, ass_add_font);
    KIT_LOAD_SYM(handle, ass_process_codec_private);
    KIT_LOAD_SYM(handle, ass_process_chunk);
    KIT_LOAD_SYM(handle, ass_set_storage_size);

    // Check that all required functions were loaded
    if(ass_library_init == NULL || ass_library_done == NULL || ass_set_message_cb == NULL ||
       ass_renderer_init == NULL || ass_renderer_done == NULL || ass_set_frame_size == NULL ||
       ass_set_hinting == NULL || ass_set_fonts == NULL || ass_render_frame == NULL || ass_new_track == NULL ||
       ass_free_track == NULL || ass_process_data == NULL || ass_add_font == NULL ||
       ass_process_codec_private == NULL || ass_process_chunk == NULL || ass_set_storage_size == NULL) {
        return 1;
    }
    return 0;
}

#endif // USE_DYNAMIC_LIBASS
