#ifndef KITPLAYER_H
#define KITPLAYER_H

/**
 * @brief Video/audio player functions
 *
 * @file kitplayer.h
 * @author Tuomas Virtanen
 * @date 2018-06-27
 */

#include "kitchensink2/kitcodec.h"
#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitformat.h"
#include "kitchensink2/kitsource.h"

#include <SDL_render.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Playback states
 */
typedef enum Kit_PlayerState
{
    KIT_STOPPED = 0, ///< Playback stopped or has not started yet.
    KIT_PLAYING,     ///< Playback started & player is actively decoding.
    KIT_PAUSED,      ///< Playback paused; player is actively decoding but no new data is given out.
    KIT_CLOSED,      ///< Playback is stopped and player is closing.
} Kit_PlayerState;

/**
 * @brief Player state container
 */
typedef struct Kit_Player Kit_Player;

/**
 * @brief Contains information about the streams selected for playback
 */
typedef struct Kit_PlayerInfo {
    Kit_Codec video_codec;                    ///< Video codec information
    Kit_Codec audio_codec;                    ///< Audio codec information
    Kit_Codec subtitle_codec;                 ///< Subtitle codec information
    Kit_VideoOutputFormat video_format;       ///< Information about the video output format
    Kit_AudioOutputFormat audio_format;       ///< Information about the audio output format
    Kit_SubtitleOutputFormat subtitle_format; ///< Information about the subtitle output format
} Kit_PlayerInfo;

/**
 * @brief Creates a new player from a source.
 *
 * Creates a new player from the given source. The source must be previously successfully
 * initialized by calling either Kit_CreateSourceFromUrl() or Kit_CreateSourceFromCustom(),
 * and it must not be used by any other player. Source must stay valid during the whole
 * playback (as in, don't close it while stuff is playing).
 *
 * It is possible to request for audio and video format conversions to be done automatically by
 * supplying requests via video_format_request and audio_format_request arguments. Conversions
 * are done on software, so they WILL add some cpu load! If you don't care what the output format is,
 * then just leave these to NULL, or feed the request objects but reset them to defaults with
 * Kit_ResetVideoFormatRequest() and Kit_ResetAudioFormatRequest().
 *'
 * Screen width and height are used for subtitle positioning, scaling and rendering resolution.
 * Ideally this should be precisely the size of your screen surface (in pixels).
 * Higher resolution leads to higher resolution text rendering. This MUST be set precisely
 * if you plan to use font hinting! If you don't care or don't have subtitles at all,
 * set both to video surface size or 0.
 *
 * For streams, either video and/or audio stream MUST be set! Either set the stream indexes manually,
 * or pick them automatically by using Kit_GetBestSourceStream().
 *
 * If hardware accelerated decoding has been enabled in Kit_Init(), then an automatic acquisition
 * of hardware decoder context is attempted. If acquiring a hardware decoder fails, we fall back to standard
 * software decoding.
 *
 * On success, this will return an initialized Kit_Player which can later be freed by Kit_ClosePlayer().
 * On error, NULL is returned and a more detailed error is available via Kit_GetError().
 *
 * For example:
 * ```
 * Kit_VideoFormatRequest v_req;
 * Kit_ResetVideoFormatRequest(&v_req);
 * v_req.hw_device_types = KIT_HWDEVICE_TYPE_VAAPI | KIT_HWDEVICE_TYPE_VDPAU;
 *
 * Kit_Player *player = Kit_CreatePlayer(
 *     src,
 *     Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
 *     Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO),
 *     Kit_GetBestSourceStream(src, KIT_STREAMTYPE_SUBTITLE),
 *     &v_req, NULL,
 *     1280, 720);
 * if(player == NULL) {
 *     fprintf(stderr, "Unable to create player: %s\n", Kit_GetError());
 *     return 1;
 * }
 * ```
 *
 * @param src Valid video/audio source
 * @param video_stream_index Video stream index or -1 if not wanted
 * @param audio_stream_index Audio stream index or -1 if not wanted
 * @param subtitle_stream_index Subtitle stream index or -1 if not wanted
 * @param video_format_request Video format request object or NULL.
 * @param audio_format_request Audio format request object or NULL.
 * @param screen_w Screen width in pixels
 * @param screen_h Screen height in pixels
 * @return Ínitialized Kit_Player or NULL
 */
KIT_API Kit_Player *Kit_CreatePlayer(
    const Kit_Source *src,
    int video_stream_index,
    int audio_stream_index,
    int subtitle_stream_index,
    const Kit_VideoFormatRequest *video_format_request,
    const Kit_AudioFormatRequest *audio_format_request,
    int screen_w,
    int screen_h
);

/**
 * @brief Close previously initialized player
 *
 * Closes a previously initialized Kit_Player instance. Note that this does NOT free
 * the linked Kit_Source -- you must free it manually.
 *
 * @param player Player instance
 */
KIT_API void Kit_ClosePlayer(Kit_Player *player);

/**
 * @brief Sets the current screen size in pixels
 *
 * Call this to change the subtitle font rendering resolution if eg. your
 * video window size changes.
 *
 * This does nothing if subtitles are not in use or if subtitles are bitmaps.
 *
 * @param player Player instance
 * @param w New width in pixels
 * @param h New height in pixels
 */
KIT_API void Kit_SetPlayerScreenSize(Kit_Player *player, int w, int h);

/**
 * @brief Gets the current video stream index
 *
 * Returns the current video stream index or -1 if one is not selected.
 *
 * @param player Player instance
 * @return Video stream index or -1
 */
KIT_API int Kit_GetPlayerVideoStream(const Kit_Player *player);

/**
 * @brief Gets the current audio stream index
 *
 * Returns the current audio stream index or -1 if one is not selected.
 *
 * @param player Player instance
 * @return Audio stream index or -1
 */
KIT_API int Kit_GetPlayerAudioStream(const Kit_Player *player);

/**
 * @brief Gets the current subtitle stream index
 *
 * Returns the current subtitle stream index or -1 if one is not selected.
 *
 * @param player Player instance
 * @return Subtitle stream index or -1
 */
KIT_API int Kit_GetPlayerSubtitleStream(const Kit_Player *player);

/**
 * @brief Checks if buffers have a given fill rate
 *
 * Values are percentages between 0% and 100%. If value is -1, then that buffer will not be taken into account.
 *
 * Note that this function returns 1 if fill rate IS reached, and 0 if not.
 *
 * @param player Player instance
 * @param audio_input Audio input packet buffer fill rate (0-100) or -1
 * @param audio_output Audio output sample buffer fill rate (0-100) or -1
 * @param video_input Video input packet buffer fill rate (0-100) or -1
 * @param video_output Video output frame buffer fill rate (0-100) or -1
 * @return 0 for false, 1 for true.
 */
KIT_API int
Kit_HasBufferFillRate(const Kit_Player *player, int audio_input, int audio_output, int video_input, int video_output);

/**
 * @brief Waits for buffers to have a given fill rate
 *
 * Values are percentages between 0% and 100%. If value is -1, then that buffer will not be taken into account.
 *
 * Timeout argument is used to limit the wait time (in seconds).
 *
 * If the function encountered the timeout, return value is 1. Otherwise 0 (for success).
 *
 * @param player Player instance
 * @param audio_input Audio input packet buffer fill rate (0-100) or -1
 * @param audio_output Audio output sample buffer fill rate (0-100) or -1
 * @param video_input Video input packet buffer fill rate (0-100) or -1
 * @param video_output Video output frame buffer fill rate (0-100) or -1
 * @param timeout Operation timeout, in seconds.
 * @return 1 if failure, 0 if success.
 */
KIT_API int Kit_WaitBufferFillRate(
    const Kit_Player *player, int audio_input, int audio_output, int video_input, int video_output, double timeout
);

/** @brief Gets the player video buffering state
 *
 * Fetch buffering state for video stream (if a stream is selected). IT is safe to pass NULL as an argument.
 * Note that if fetch fails (stream is not set, etc.), the arguments will not be written to.
 *
 * @param player Player instance
 * @param frames_length Current size of the output buffer in frames
 * @param frames_size Current maximum size of the output buffer in frames
 * @param packets_length Current size of the input buffer in raw packets
 * @param packets_capacity Current maximum size of the input buffer in raw packets
 */
KIT_API void Kit_GetPlayerVideoBufferState(
    const Kit_Player *player,
    unsigned int *frames_length,
    unsigned int *frames_size,
    unsigned int *packets_length,
    unsigned int *packets_capacity
);

/** @brief Gets the player audio buffering state
 *
 * Fetch buffering state for audio stream (if a stream is selected). IT is safe to pass NULL as an argument.
 * Note that if fetch fails (stream is not set, etc.), the arguments will not be written to.
 *
 * @param player Player instance
 * @param samples_length Current size of the output buffer in samples
 * @param samples_size Current maximum size of the output buffer in samples
 * @param packets_length Current size of the input buffer in raw packets
 * @param packets_capacity Current maximum size of the input buffer in raw packets
 */
KIT_API void Kit_GetPlayerAudioBufferState(
    const Kit_Player *player,
    unsigned int *samples_length,
    unsigned int *samples_size,
    unsigned int *packets_length,
    unsigned int *packets_capacity
);

/** @brief Gets the player subtitle buffering state
 *
 * Fetch buffering state for subtitle stream (if a stream is selected). IT is safe to pass NULL as an argument.
 * Note that if fetch fails (stream is not set, etc.), the arguments will not be written to.
 *
 * @param player Player instance
 * @param items_length Current size of the output buffer in subtitle elements
 * @param items_size Current maximum size of the output buffer in subtitle elements
 * @param packets_length Current size of the input buffer in raw packets
 * @param packets_capacity Current maximum size of the input buffer in raw packets
 */
KIT_API void Kit_GetPlayerSubtitleBufferState(
    const Kit_Player *player,
    unsigned int *items_length,
    unsigned int *items_size,
    unsigned int *packets_length,
    unsigned int *packets_capacity
);

/**
 * @brief Fetches a new video frame from the player
 *
 * Note that the output texture must be previously allocated and valid.
 *
 * It is important to select the correct texture format and size. If you pick a different
 * texture format or size from what the decoder outputs, then the decoder will attempt to convert
 * the frames to fit the texture. This will slow down the decoder a *lot* so if possible,
 * pick the texture format from what Kit_GetPlayerInfo() outputs.
 *
 * Access flag for the texture *MUST* always be SDL_TEXTUREACCESS_STATIC! Anything else will lead to
 * undefined behaviour.
 *
 * Area argument can be given to acquire the current video frame content area. Note that this may change
 * if you have video that changes frame size on the fly. If you don't care, feed it NULL.
 *
 * This function will do nothing if player playback has not been started.
 *
 * @param player Player instance
 * @param texture A previously allocated texture
 * @param area Rendered video surface area or NULL.
 * @return 0 on success, 1 on error
 */
KIT_API int Kit_GetPlayerVideoSDLTexture(const Kit_Player *player, SDL_Texture *texture, SDL_Rect *area);

/**
 * @brief Locks the player video output for reading.
 *
 * This is used with Kit_UnlockPlayerVideoRawFrame() to fetch raw video frames from the player.
 *
 * When this function is called, the video decoder checks if there are frames available and if frame read
 * happens in sync. If both conditions succeed, then data, line_size and area pointers are filled and the function
 * returns 0. If either of the conditions fail, this function will return 1. Note that if this function succeeds,
 * then Kit_UnlockPlayerVideoRawFrame() must be called to clean up!
 *
 * Note that data and line_size pointer values depend on what sort of video data you are fetching.
 * Data contains an array of pointers of the actual pixel data, while line_size contains the widths
 * of the picture rows in bytes. If the output is RGB data, then only data[0] and line_size[0] are set.
 * If the output is YUV, then each component is split into the three first indexes (data[0] to data[2],
 * line_size[0] to line_size[2]). The line_size and data pointers match the ffmpeg AVFrame fields exactly,
 * so you can refer to ffmpeg documentation here.
 *
 * Area argument can be given to acquire the current video frame content area. Note that this may change
 * if you have video that changes frame size on the fly. If you don't care, feed it NULL.
 *
 * This function will do nothing if player playback has not been started.
 *
 * For example:
 * ```
 * unsigned char **data;
 * int *line_size;
 * SDL_Rect rect;
 * if(Kit_LockPlayerVideoRaw(player, &data, &line_size, &rect) == 0) {
 *     // Do something with the data here.
 *     Kit_UnlockPlayerVideoRaw(player);
 * }
 * ```
 *
 * @param player Player instance
 * @param data Video data pointers or NULL
 * @param line_size Video data line size pointers or NULL.
 * @param area Rendered video surface area or NULL
 * @return 0 on success, 1 on error
 */
KIT_API int
Kit_LockPlayerVideoRawFrame(const Kit_Player *player, unsigned char ***data, int **line_size, SDL_Rect *area);

/**
 * @brief Unlocks the player video output.
 *
 * This is used with Kit_LockPlayerVideoRawFrame() to fetch raw video frames from the player. This function must be
 * called after you are done with the data pointers.
 *
 * @param player Player instance
 */
KIT_API void Kit_UnlockPlayerVideoRawFrame(const Kit_Player *player);

/**
 * @brief Fetches subtitle data from the player
 *
 * Output texture will be used as a texture atlas for the subtitle fragments.
 *
 * Note that the output texture must be previously allocated and valid. Make sure to have large
 * enough a texture for the rendering resolution you picked! If your rendering resolution if 4k,
 * then make sure to have texture sized 4096x4096 etc. This gives the texture room to handle the
 * worst case subtitle textures. If your resolution is too small, this function will return
 * value -1. At that point you can replace your current texture with a bigger one on the fly.
 *
 * Note that the texture format for the atlas texture *MUST* be SDL_PIXELFORMAT_RGBA32 and
 * the access flag *MUST* be set to SDL_TEXTUREACCESS_STATIC for correct rendering.
 * Using any other format will lead to undefined behaviour. Also, make sure to set scaling quality
 * to 0 or "nearest" before creating the texture -- otherwise you get artifacts
 * (see SDL_HINT_RENDER_SCALE_QUALITY).
 *
 * This function will do nothing if player playback has not been started.
 *
 * For example:
 * ```
 * SDL_Rect sources[256];
 * SDL_Rect targets[256];
 * int got = Kit_GetPlayerSubtitleData(player, subtitle_tex, sources, targets, 256);
 * for(int i = 0; i < got; i++) {
 *     SDL_RenderCopy(renderer, subtitle_tex, &sources[i], &targets[i]);
 * }
 * ```
 *
 * @param player Player instance
 * @param texture A previously allocated texture
 * @param sources List of source rectangles to copy from
 * @param targets List of target rectangles to render
 * @param limit Defines the maximum size of your rectangle lists
 * @return Number of sources or <0 on error
 */
KIT_API int Kit_GetPlayerSubtitleSDLTexture(
    const Kit_Player *player, SDL_Texture *texture, SDL_Rect *sources, SDL_Rect *targets, int limit
);

/**
 * @brief Fetches raw subtitle frames from the player
 *
 * When called, this function will set the pointers for the items and targets lists of frame data and target
 * rectangles. The pointers will be valid until the next time this function is called OR until the player is closed.
 *
 * Each source rectangle represents the size of the source data, and each target rectangle will have the width
 * and height of the final subtitle block, and the x and y coordinates of where it should be rendered.
 * Note that you may need to scale from source size to target size when rendering!
 *
 * This function will do nothing if player playback has not been started.
 *
 * Output frames will always be in RGBA8888 format!
 *
 * For example:
 * ```
 * unsigned char **subtitle_data;
 * SDL_Rect *subtitle_rects;
 * int subtitle_frames = Kit_GetPlayerSubtitleRawFrames(player, &subtitle_data, &subtitle_rects);
 * for (int i = 0; i < subtitle_frames; i++) {
 *     unsigned char *data = subtitle_data[i];
 *     SDL_Rect *dst_rect = &subtitle_rects[i];
 *     // Do something with the data
 * }
 * ```
 *
 * @param player Player instance
 * @param items Subtitle frame RGBA8888 item pointers
 * @param sources List of source rectangles to render
 * @param targets List of target rectangles to render
 * @return Number of sources or <0 on error
 */
KIT_API int Kit_GetPlayerSubtitleRawFrames(
    const Kit_Player *player, unsigned char ***items, SDL_Rect **sources, SDL_Rect **targets
);

/**
 * @brief Fetches audio data from the player
 * This function will attempt to read the maximum amount of data requested by the length
 * argument. If there is less data available than requested, try to read maximum currently available.
 * Note that the output buffer must be previously allocated.
 *
 * Audio data format can be acquired by calling Kit_GetPlayerInfo().
 *
 * The "backend_buffer_size" argument should be set to the size of backend (hardware) audio buffers.
 * If your backend is SDL2, this can be provided by SDL_GetQueuedAudioSize(). This information is used
 * to supply silence, if the output is almost empty and video stream has no audio data to give.
 * If you don't have this value or just don't care, just set it to UINT_MAX or some other large value.
 *
 * This function will do nothing if player playback has not been started.
 *
 * @param player Player instance
 * @param backend_buffer_size Amount of data currently queued to the driver/hw device.
 * @param buffer Buffer to read into
 * @param length Maximum length of the buffer
 * @return Amount of data that was read, <0 on error.
 */
KIT_API int
Kit_GetPlayerAudioData(const Kit_Player *player, size_t backend_buffer_size, unsigned char *buffer, size_t length);

/**
 * @brief Fetches information about the currently selected streams
 *
 * This function should be used to fetch codec information and output format data from the player
 * before creating textures and setting up audio outputs.
 *
 * @param player Player instance
 * @param info A previously allocated Kit_PlayerInfo instance
 */
KIT_API void Kit_GetPlayerInfo(const Kit_Player *player, Kit_PlayerInfo *info);

/**
 * @brief Returns the current state of the player
 *
 * @param player Player instance
 * @return Current state of the player, see Kit_PlayerState
 */
KIT_API Kit_PlayerState Kit_GetPlayerState(Kit_Player *player);

/**
 * @brief Starts playback
 *
 * State shifts:
 * - If player is already playing, will do nothing.
 * - If player is paused, will resume playback.
 * - If player is stopped, will begin playback (and background decoding).
 *
 * @param player Player instance
 */
KIT_API void Kit_PlayerPlay(Kit_Player *player);

/**
 * @brief Stops playback
 *
 * State shifts:
 * - If player is already stopped, will do nothing.
 * - If player is paused, will stop playback.
 * - If player is started, will stop playback (and background decoding).
 *
 * @param player Player instance
 */
KIT_API void Kit_PlayerStop(Kit_Player *player);

/**
 * @brief Pauses playback
 *
 * State shifts:
 * - If player is already paused, will do nothing.
 * - If player is stopped, will do nothing.
 * - If player is started, will pause playback (and background decoding).
 *
 * @param player Player instance
 */
KIT_API void Kit_PlayerPause(Kit_Player *player);

/**
 * @brief Seek to timestamp
 *
 * Rewinds or forwards video/audio playback to the given timestamp (in seconds).
 *
 * This may not work for network or custom sources!
 *
 * @param player Player instance
 * @param time Timestamp to seek to in seconds
 * @return 0 on success, 1 on failure.
 */
KIT_API int Kit_PlayerSeek(Kit_Player *player, double time);

/**
 * @brief Get the duration of the source
 *
 * Returns the duration of the source in seconds
 *
 * @param player Player instance
 * @return Duration
 */
KIT_API double Kit_GetPlayerDuration(const Kit_Player *player);

/**
 * @brief Get the current position of the playback
 *
 * Returns the position of the playback in seconds
 *
 * @param player Player instance
 * @return Position
 */
KIT_API double Kit_GetPlayerPosition(const Kit_Player *player);

/**
 * @brief Get the player aspect ratio, if playing video.
 *
 * Sets numerator and denominator if it is possible to get a valid aspect ratio.
 * If valid values were found, then 0 is returned. Otherwise 1 is returned, and num
 * and den parameters are not changed.
 *
 * Aspect ratio may change during the playback of the video. This function will attempt
 * to first get the aspect ratio of the current frame. If that is not set, then decoder
 * and finally demuxer data will be tried.
 *
 * @param player Player instance
 * @param num Numerator
 * @param den Denominator
 * @return 0 if got valid values, 1 otherwise.
 */
KIT_API int Kit_GetPlayerAspectRatio(const Kit_Player *player, int *num, int *den);

/**
 * @brief Closes a stream for specified stream type.
 *
 * @param player Player instance
 * @param type Stream to close
 * @return 0 on success, 1 on failure.
 */
KIT_API int Kit_ClosePlayerStream(Kit_Player *player, Kit_StreamType type);

/**
 * @brief Selects stream index for specified stream type.
 *
 * This allows switching streams during or outside playback. If stream switching fails for some reason,
 * 1 will be returned and old stream will continue to be used.
 *
 * Setting index to -1 will close the stream completely.
 *
 * @param player Player instance
 * @param type Stream to switch
 * @param index Index to use (list can be queried from the source)
 * @return 0 on success, 1 on failure.
 */
KIT_API int Kit_SetPlayerStream(Kit_Player *player, Kit_StreamType type, int index);

/**
 * @brief Returns the current index of the specified stream type
 *
 * @param player Player instance
 * @param type Stream to check
 * @return Stream index or -1 on error or if stream is not set
 */
KIT_API int Kit_GetPlayerStream(const Kit_Player *player, Kit_StreamType type);

#ifdef __cplusplus
}
#endif

#endif // KITPLAYER_H
