#ifdef USE_DYNAMIC_LIBASS

#include <SDL_loadso.h>
#include "kitchensink/internal/libass.h"


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
void (*ass_process_chunk)(ASS_Track *track, char *data, int size, long long timecode, long long duration);
void (*ass_add_font)(ASS_Library *library, char *name, char *data, int data_size);
void (*ass_set_storage_size)(ASS_Renderer *priv, int w, int h);


int load_libass(void *handle) {
    ass_library_init = SDL_LoadFunction(handle, "ass_library_init");
    ass_library_done = SDL_LoadFunction(handle, "ass_library_done");
    ass_set_message_cb = SDL_LoadFunction(handle, "ass_set_message_cb");
    ass_renderer_init = SDL_LoadFunction(handle, "ass_renderer_init");
    ass_renderer_done = SDL_LoadFunction(handle, "ass_renderer_done");
    ass_set_frame_size = SDL_LoadFunction(handle, "ass_set_frame_size");
    ass_set_hinting = SDL_LoadFunction(handle, "ass_set_hinting");
    ass_set_fonts = SDL_LoadFunction(handle, "ass_set_fonts");
    ass_render_frame = SDL_LoadFunction(handle, "ass_render_frame");
    ass_new_track = SDL_LoadFunction(handle, "ass_new_track");
    ass_free_track = SDL_LoadFunction(handle, "ass_free_track");
    ass_process_data = SDL_LoadFunction(handle, "ass_process_data");
    ass_add_font = SDL_LoadFunction(handle, "ass_add_font");
    ass_process_codec_private = SDL_LoadFunction(handle, "ass_process_codec_private");
    ass_process_chunk = SDL_LoadFunction(handle, "ass_process_chunk");
    ass_set_storage_size = SDL_LoadFunction(handle, "ass_set_storage_size");
    return 0;
}

#endif // USE_DYNAMIC_LIBASS
