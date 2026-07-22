// Headless decode benchmark. Decodes for a fixed wall-clock time and reports throughput.
// The late-drop threshold is disabled so the delivered-frame rate equals true decode throughput.
// Modes: raw = read frames via the raw lock API, tex = upload frames to an SDL texture (hidden window).
#include <SDL.h>
#include <kitchensink2/kitchensink.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *hw_name(unsigned int type) {
    switch(type) {
        case KIT_HWDEVICE_TYPE_NONE:
            return "NONE (software)";
        case KIT_HWDEVICE_TYPE_VDPAU:
            return "VDPAU";
        case KIT_HWDEVICE_TYPE_CUDA:
            return "CUDA";
        case KIT_HWDEVICE_TYPE_VAAPI:
            return "VAAPI";
        case KIT_HWDEVICE_TYPE_QSV:
            return "QSV";
        case KIT_HWDEVICE_TYPE_DRM:
            return "DRM";
        default:
            return "OTHER";
    }
}

int main(int argc, char *argv[]) {
    if(argc != 5) {
        fprintf(stderr, "Usage: bench <filename> <seconds> <hw|sw> <raw|tex>\n");
        return 1;
    }
    const int run_seconds = atoi(argv[2]);
    const bool use_hw = strcmp(argv[3], "hw") == 0;
    const bool use_tex = strcmp(argv[4], "tex") == 0;
    unsigned char audio_buf[32768];
    unsigned char **frame_data;
    int *frame_line_size;
    SDL_Rect area;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    int video_frames = 0;
    size_t audio_bytes = 0;

    if(Kit_Init(KIT_INIT_HW_DECODE) != 0) {
        fprintf(stderr, "Kit_Init failed: %s\n", Kit_GetError());
        return 1;
    }
    // Never drop frames as "late", so every decoded frame is delivered to us.
    Kit_SetHint(KIT_HINT_VIDEO_LATE_THRESHOLD, 3600000);

    Kit_Source *src = Kit_CreateSourceFromUrl(argv[1]);
    if(src == NULL) {
        fprintf(stderr, "Unable to load '%s': %s\n", argv[1], Kit_GetError());
        return 1;
    }
    Kit_VideoFormatRequest v_req;
    Kit_ResetVideoFormatRequest(&v_req);
    if(!use_hw)
        v_req.hw_device_types = KIT_HWDEVICE_TYPE_NONE;
    Kit_Player *player = Kit_CreatePlayer(
        src,
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO),
        -1,
        &v_req,
        NULL,
        1280,
        720
    );
    if(player == NULL) {
        fprintf(stderr, "Unable to create player: %s\n", Kit_GetError());
        return 1;
    }

    Kit_PlayerInfo info;
    Kit_GetPlayerInfo(player, &info);
    printf("video codec: %s (%s)\n", info.video_codec.name, info.video_codec.description);
    printf("audio codec: %s (%s)\n", info.audio_codec.name, info.audio_codec.description);
    printf("hw device:   %s\n", hw_name(info.video_format.hw_device_type));
    printf("output fmt:  %s\n", SDL_GetPixelFormatName(info.video_format.format));

    if(use_tex) {
        if(SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        window = SDL_CreateWindow("bench", 0, 0, 320, 240, SDL_WINDOW_HIDDEN);
        renderer = SDL_CreateRenderer(window, -1, 0);
        texture = SDL_CreateTexture(
            renderer,
            info.video_format.format,
            SDL_TEXTUREACCESS_STREAMING,
            info.video_format.width,
            info.video_format.height
        );
        if(texture == NULL) {
            fprintf(stderr, "Unable to create texture: %s\n", SDL_GetError());
            return 1;
        }
    }

    Kit_PlayerPlay(player);
    const Uint32 start = SDL_GetTicks();
    while(SDL_GetTicks() - start < (Uint32)(run_seconds * 1000)) {
        if(Kit_GetPlayerState(player) == KIT_STOPPED)
            break;
        if(use_tex) {
            if(Kit_GetPlayerVideoSDLTexture(player, texture, &area) == 0)
                video_frames++;
        } else {
            if(Kit_LockPlayerVideoRawFrame(player, &frame_data, &frame_line_size, &area) == 0) {
                video_frames++;
                Kit_UnlockPlayerVideoRawFrame(player);
            }
        }
        audio_bytes += Kit_GetPlayerAudioData(player, sizeof(audio_buf), audio_buf, sizeof(audio_buf));
        SDL_Delay(1);
    }
    const double elapsed = (SDL_GetTicks() - start) / 1000.0;

    printf(
        "decoded %d video frames in %.1f s (%.1f fps), %zu audio bytes\n",
        video_frames,
        elapsed,
        video_frames / elapsed,
        audio_bytes
    );
    Kit_ClosePlayer(player);
    Kit_CloseSource(src);
    if(texture != NULL)
        SDL_DestroyTexture(texture);
    if(renderer != NULL)
        SDL_DestroyRenderer(renderer);
    if(window != NULL)
        SDL_DestroyWindow(window);
    Kit_Quit();
    return 0;
}
