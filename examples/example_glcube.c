#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <kitchensink3/kitchensink.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "example_common.h"

/*
 * Note! This example does not do proper error handling etc.
 * It is for example use only!
 *
 * Shows how to pull raw RGBA video frames out of the player and upload them
 * into an OpenGL texture, which is then rendered on all six sides of a
 * spinning cube. Video only; audio and subtitle streams are ignored.
 *
 * Uses old style fixed-function OpenGL to keep the example short. Note that
 * video frames are rarely power-of-two sized, so this technically relies on
 * GL 2.0 era non-power-of-two texture support -- any driver from this
 * millennium will handle that fine.
 */

/**
 * @brief Draws a unit cube with the currently bound texture on all six faces.
 *
 * Each face maps the full texture, going counter-clockwise from its bottom-left
 * corner. The v coordinate is 0 at the top edge of each face, since the video
 * frame is uploaded top row first and thus sits "upside down" in GL texture space.
 */
static void draw_cube(void) {
    glBegin(GL_QUADS);
    // front
    glTexCoord2f(0, 1);
    glVertex3f(-1, -1, 1);
    glTexCoord2f(1, 1);
    glVertex3f(1, -1, 1);
    glTexCoord2f(1, 0);
    glVertex3f(1, 1, 1);
    glTexCoord2f(0, 0);
    glVertex3f(-1, 1, 1);
    // back
    glTexCoord2f(0, 1);
    glVertex3f(1, -1, -1);
    glTexCoord2f(1, 1);
    glVertex3f(-1, -1, -1);
    glTexCoord2f(1, 0);
    glVertex3f(-1, 1, -1);
    glTexCoord2f(0, 0);
    glVertex3f(1, 1, -1);
    // left
    glTexCoord2f(0, 1);
    glVertex3f(-1, -1, -1);
    glTexCoord2f(1, 1);
    glVertex3f(-1, -1, 1);
    glTexCoord2f(1, 0);
    glVertex3f(-1, 1, 1);
    glTexCoord2f(0, 0);
    glVertex3f(-1, 1, -1);
    // right
    glTexCoord2f(0, 1);
    glVertex3f(1, -1, 1);
    glTexCoord2f(1, 1);
    glVertex3f(1, -1, -1);
    glTexCoord2f(1, 0);
    glVertex3f(1, 1, -1);
    glTexCoord2f(0, 0);
    glVertex3f(1, 1, 1);
    // top
    glTexCoord2f(0, 1);
    glVertex3f(-1, 1, 1);
    glTexCoord2f(1, 1);
    glVertex3f(1, 1, 1);
    glTexCoord2f(1, 0);
    glVertex3f(1, 1, -1);
    glTexCoord2f(0, 0);
    glVertex3f(-1, 1, -1);
    // bottom
    glTexCoord2f(0, 1);
    glVertex3f(-1, -1, -1);
    glTexCoord2f(1, 1);
    glVertex3f(1, -1, -1);
    glTexCoord2f(1, 0);
    glVertex3f(1, -1, 1);
    glTexCoord2f(0, 0);
    glVertex3f(-1, -1, 1);
    glEnd();
}

/**
 * @brief Sets the viewport and a perspective projection for the given window size.
 *
 * Uses a 60 degree vertical field of view with the near plane at 0.1.
 *
 * @param screen_w Window width in pixels
 * @param screen_h Window height in pixels
 */
static void set_perspective(int screen_w, int screen_h) {
    const float aspect = (float)screen_w / (float)screen_h;
    const float top = 0.1f * tanf(60.0f * (float)M_PI / 360.0f);
    glViewport(0, 0, screen_w, screen_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-top * aspect, top * aspect, -top, top, 0.1, 100.0);
}

int main(int argc, char *argv[]) {
    const int screen_w = 1280, screen_h = 720;

    // Get filename to open
    const char *filename = get_filename_arg(argc, argv, "glcube");

    // Init SDL with an OpenGL context. SDL's default GL attributes give us a
    // compatibility context, which is all the fixed-function pipeline needs.
    initialize_sdl(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *window = create_window(filename, screen_w, screen_h, SDL_WINDOW_OPENGL);
    SDL_GLContext gl_ctx = create_gl_context(window);

    // Initialize Kitchensink with network support. No libass needed, since we
    // don't render subtitles in this example.
    const int err = Kit_Init(KIT_INIT_NETWORK);
    if(err != 0) {
        fprintf(stderr, "Unable to initialize Kitchensink: %s", Kit_GetError());
        return 1;
    }

    // Open the source file.
    Kit_Source *src = Kit_CreateSourceFromUrl(filename);
    if(src == NULL) {
        fprintf(stderr, "Unable to load file '%s': %s\n", filename, Kit_GetError());
        return 1;
    }

    // Request RGBA32 video output from the player. This guarantees that the raw
    // frames we lock later are tightly packed RGBA pixels in a single plane,
    // which can be fed to glTexSubImage2D as GL_RGBA / GL_UNSIGNED_BYTE with no
    // conversion on our side. Note that if the decoder does not output RGB
    // natively (most video is YUV), the player converts in software, which
    // costs some performance.
    Kit_VideoFormatRequest video_request;
    Kit_ResetVideoFormatRequest(&video_request);
    video_request.format = SDL_PIXELFORMAT_RGBA32;

    // Set up default configs for the player
    Kit_PlayerConfig config;
    Kit_ResetPlayerConfig(&config);

    // Create the player with the best video stream. Audio and subtitle streams
    // are disabled by passing -1. The player clock still runs normally, so
    // video frames come out in sync even without an audio device.
    Kit_Player *player = Kit_CreatePlayer(
        src, Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO), -1, -1, &video_request, NULL, 0, 0, &config
    );
    if(player == NULL) {
        fprintf(stderr, "Unable to create player: %s\n", Kit_GetError());
        return 1;
    }
    if(Kit_GetPlayerVideoStream(player) == -1) {
        fprintf(stderr, "File contains no video!\n");
        return 1;
    }

    // Fetch the output format so we know how large a texture the video needs.
    Kit_PlayerInfo player_info;
    Kit_GetPlayerInfo(player, &player_info);
    const int video_w = player_info.video_format.width;
    const int video_h = player_info.video_format.height;

    // Create the video texture. It is allocated once here at video size; each
    // frame is then streamed into it with glTexSubImage2D in the main loop.
    GLuint video_tex;
    glGenTextures(1, &video_tex);
    glBindTexture(GL_TEXTURE_2D, video_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, video_w, video_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    set_perspective(screen_w, screen_h);

    // Start playback
    Kit_PlayerPlay(player);

    bool run = true;
    while(run) {
        if(Kit_GetPlayerState(player) == KIT_STOPPED) {
            run = false;
            continue;
        }

        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_EVENT_QUIT:
                    run = false;
                    break;
                case SDL_EVENT_KEY_UP:
                    if(event.key.key == SDLK_RIGHT)
                        Kit_PlayerSeek(player, Kit_GetPlayerPosition(player) + 10);
                    if(event.key.key == SDLK_LEFT)
                        Kit_PlayerSeek(player, Kit_GetPlayerPosition(player) - 10);
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    set_perspective(event.window.data1, event.window.data2);
                    break;
                default:;
            }
        }

        // Try to lock the current raw video frame. This succeeds (returns 0)
        // only when a new frame is available and due for display; otherwise we
        // simply keep rendering the previous texture contents. On success the
        // data pointers are only valid until the unlock call, so we upload the
        // pixels to the GL texture right away and unlock immediately.
        unsigned char **data;
        int *line_size;
        if(Kit_LockPlayerVideoRawFrame(player, &data, &line_size, NULL) == 0) {
            // RGBA output is a single plane, so only data[0] and line_size[0]
            // are set. line_size[0] is the frame row stride in bytes, which may
            // be larger than width * 4 due to padding; GL_UNPACK_ROW_LENGTH
            // (in pixels) tells GL how to skip it.
            glPixelStorei(GL_UNPACK_ROW_LENGTH, line_size[0] / 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, video_w, video_h, GL_RGBA, GL_UNSIGNED_BYTE, data[0]);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            Kit_UnlockPlayerVideoRawFrame(player);
        }

        // Render the cube, rotation driven by wall-clock time.
        const float t = SDL_GetTicks() / 1000.0f;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -4.0f); // Push the cube away from the camera.
        glRotatef(t * 30.0f, 1.0f, 0.0f, 0.0f);
        glRotatef(t * 45.0f, 0.0f, 1.0f, 0.0f);
        draw_cube();

        // Show the results + wait for vsync
        SDL_GL_SwapWindow(window);
    }

    Kit_ClosePlayer(player);
    Kit_CloseSource(src);
    Kit_Quit();

    glDeleteTextures(1, &video_tex);
    SDL_GL_DestroyContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
