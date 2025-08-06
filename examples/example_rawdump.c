#include <SDL.h>
#include <kitchensink2/kitchensink.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * This is an example how one would read raw data from SDL_Kitchensink.
 * This could technically be extended to use vulkan or opengl or whatever as a backend.
 *
 * Note that in real life this example makes no sense, since we read frames and save them to disk
 * in sync, which is slow. Normally you would just use ffmpeg directly for this purpose.
 */

#define MAX_FILESIZE 256

typedef struct __attribute__((__packed__)) tga_header {
    uint8_t id;
    uint8_t colormap;
    uint8_t type;
    uint8_t colormap_spec[5];
    uint16_t origin_x;
    uint16_t origin_y;
    uint16_t width;
    uint16_t height;
    uint8_t depth;
    uint8_t descriptor;
} tga_header;

bool write_tga(const char *filename, const unsigned char *src, uint16_t w, uint16_t h) {
    FILE *fp = fopen(filename, "wb");
    if(fp == NULL) {
        return false;
    }

    // TGA Header
    const tga_header header = {
        .id = 0,
        .colormap = 0,
        .type = 2,
        .colormap_spec = {0, 0, 0, 0, 0},
        .origin_x = 0,
        .origin_y = 0,
        .width = w,
        .height = h,
        .depth = 24,
        .descriptor = 0,
    };
    fwrite(&header, sizeof(tga_header), 1, fp);

    // Image data
    const unsigned char *d = 0;
    for(int y = h - 1; y >= 0; y--) {
        for(int x = 0; x < w; x++) {
            d = src + (y * w + x) * 3;
            fwrite(d + 2, 1, 1, fp);
            fwrite(d + 1, 1, 1, fp);
            fwrite(d + 0, 1, 1, fp);
        }
    }

    fclose(fp);
    return true;
}

int main(int argc, char *argv[]) {
    int err = 0;
    const char *filename = NULL;
    const char *output_dir = NULL;
    int frame_index = 0;
    char file_name[MAX_FILESIZE];
    bool run = true;
    Kit_Source *src = NULL;
    Kit_Player *player = NULL;

    // Get filename to open
    if(argc != 3) {
        fprintf(stderr, "Usage: rawdump <filename> <outputdir>\n");
        return 0;
    }
    filename = argv[1];
    output_dir = argv[2];

    // Initialize Kitchensink with hardware decode and libass support.
    err = Kit_Init(KIT_INIT_ASS | KIT_INIT_HW_DECODE);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize Kitchensink: %s", Kit_GetError());
        return 1;
    }

    // Open up the sourcefile. This can be a local file, network url, ...
    src = Kit_CreateSourceFromUrl(filename);
    if(src == NULL) {
        fprintf(stderr, "Unable to load file '%s': %s\n", filename, Kit_GetError());
        return 1;
    }

    // Always output RGB888 so that we can easily process it.
    Kit_VideoFormatRequest v_req;
    Kit_ResetVideoFormatRequest(&v_req);
    v_req.format = SDL_PIXELFORMAT_RGB24;

    // Create the player. Pick best video and subtitle streams, and set subtitle rendering resolution.
    player = Kit_CreatePlayer(
        src,
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
        -1,
        Kit_GetBestSourceStream(src, KIT_STREAMTYPE_SUBTITLE),
        &v_req,
        NULL,
        1280,
        720
    );
    if(player == NULL) {
        fprintf(stderr, "Unable to create player: %s\n", Kit_GetError());
        return 1;
    }

    // Print some information
    Kit_PlayerInfo pinfo;
    Kit_GetPlayerInfo(player, &pinfo);

    // Make sure there is video in the file to play first.
    if(Kit_GetPlayerVideoStream(player) == -1) {
        fprintf(stderr, "File contains no video!\n");
        return 1;
    }

    // Start playback
    Kit_PlayerPlay(player);

    while(run) {
        if(Kit_GetPlayerState(player) == KIT_STOPPED) {
            run = false;
            continue;
        }

        // Lock the video output and fetch the frame data pointers. Data contains the video data, and line_size contains
        // the raw byte sizes of the buffers. When reading RGBA data,
        unsigned char **frame_data;
        unsigned char **subtitle_data;
        int *frame_line_size;
        SDL_Rect *source_rects;
        SDL_Rect *target_rects;
        SDL_Rect area;
        if(Kit_LockPlayerVideoRawFrame(player, &frame_data, &frame_line_size, &area) == 0) {
            // Since we are rendering on top of the image frame, the screen size is the same as the frame size.
            Kit_SetPlayerScreenSize(player, area.w, area.h);

            // Use SDL_Surfaces for simple blitting. Since we know that the source data is RGB24 - as we told Kit
            // to convert it - we can just declare the data as RGB24 here.
            SDL_Surface *pic = SDL_CreateRGBSurfaceWithFormatFrom(frame_data[0], area.w, area.h, 24, frame_line_size[0], SDL_PIXELFORMAT_RGB24);

            // Fetch and render subtitles on top of the image frame
            int subtitle_frames = Kit_GetPlayerSubtitleRawFrames(player, &subtitle_data, &source_rects, &target_rects);
            if(subtitle_frames > 0) {
                for (int i = 0; i < subtitle_frames; i++) {
                    SDL_Rect *s = &source_rects[i];
                    SDL_Rect *t = &target_rects[i];
                    SDL_Surface *frame = SDL_CreateRGBSurfaceWithFormatFrom(
                        subtitle_data[i], s->w, s->h, 32, s->w * 4, SDL_PIXELFORMAT_RGBA32);
                    SDL_BlitScaled(frame, s, pic, t);
                    SDL_FreeSurface(frame);
                }
            }

            // Write out the frame as TGA
            snprintf(file_name, MAX_FILESIZE, "%sframe_%d.tga", output_dir, frame_index);
            write_tga(file_name, frame_data[0], area.w, area.h);
            fprintf(stderr, "Got frame %d: %d x %d, %d subtitle frames, saved to %s\n", frame_index, area.w, area.h, subtitle_frames, file_name);

            // We are done with data. Unlock the video output. This invalidates the data and line_size pointers!
            SDL_FreeSurface(pic);
            Kit_UnlockPlayerVideoRawFrame(player);
            frame_index++;
        }
    }

    Kit_ClosePlayer(player);
    Kit_CloseSource(src);
    Kit_Quit();
    return 0;
}
