#ifndef KITPLAYER_H
#define KITPLAYER_H

/**
 * @brief Video/audio player functions
 *
 * @file kitplayer.h
 * @author Tuomas Virtanen
 * @date 2018-06-27
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/kitcodec.h"
#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitformat.h"
#include "kitchensink2/kitlib.h"
#include "kitchensink2/kitsource.h"

#include <SDL_render.h>
#include <stdbool.h>

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
 * @brief Video stream configuration, see Kit_PlayerConfig.
 */
typedef struct Kit_PlayerVideoConfig {
    int packet_buffer_size; ///< Input buffer, packets (default 64)
    int frame_buffer_size;  ///< Output buffer, frames (default 3)
    int early_threshold;    ///< Early sync threshold, ms (default 5)
    int late_threshold;     ///< Late sync threshold, ms (default 50)
} Kit_PlayerVideoConfig;

/**
 * @brief Audio stream configuration, see Kit_PlayerConfig.
 */
typedef struct Kit_PlayerAudioConfig {
    int packet_buffer_size; ///< Input buffer, packets (default 64)
    int frame_buffer_size;  ///< Output buffer, frames (default 64)
    int early_threshold;    ///< Early sync threshold, ms (default 30)
    int late_threshold;     ///< Late sync threshold, ms (default 50)
} Kit_PlayerAudioConfig;

/**
 * @brief Font hinting options. Used as values for Kit_PlayerConfig subtitle.font_hinting.
 */
typedef enum Kit_FontHinting
{
    KIT_FONT_HINTING_NONE = 0, ///< No hinting. This is recommended option
    KIT_FONT_HINTING_LIGHT,    ///< Light hinting. Use this if you need hinting
    KIT_FONT_HINTING_NORMAL,   ///< Not recommended, please see libass docs for details
    KIT_FONT_HINTING_NATIVE,   ///< Not recommended, please see libass docs for details
    KIT_FONT_HINTING_COUNT
} Kit_FontHinting;

/**
 * @brief Subtitle stream configuration, see Kit_PlayerConfig.
 */
typedef struct Kit_PlayerSubtitleConfig {
    int packet_buffer_size;       ///< Input buffer, packets (default 64)
    int frame_buffer_size;        ///< Output buffer, frames (default 64; bitmap subtitles only)
    Kit_FontHinting font_hinting; ///< Font hinting mode for libass (default KIT_FONT_HINTING_NONE)
} Kit_PlayerSubtitleConfig;

/**
 * @brief Demuxer configuration, see Kit_PlayerConfig.
 */
typedef struct Kit_PlayerDemuxerConfig {
    int read_attempts;    ///< Read attempts before treating a failure as EOF (default 3)
    int read_retry_delay; ///< Delay between read attempts, ms (default 10)
} Kit_PlayerDemuxerConfig;

/**
 * @brief Per-player configuration for Kit_CreatePlayer().
 *
 * Initialize with Kit_ResetPlayerConfig(), then override the fields you need.
 * All values are fixed at player creation; out-of-range values are clamped.
 *
 * CAUTION on the buffer sizes: the defaults are chosen so that the pipeline can re-prime
 * itself after a seek. Very small audio buffer sizes (a few packets/frames) can deadlock
 * post-seek playback: the audio side holds data until the video stream re-bases the shared
 * clock, and with too little audio buffer slack that backpressure stalls the shared demuxer
 * thread before video can decode its first frame. Prefer the defaults; if you must shrink,
 * keep the audio buffers at a couple dozen packets/frames or more.
 */
typedef struct Kit_PlayerConfig {
    int thread_count;            ///< FFmpeg threads per codec; 0 = autodetect (default 0). Applies to all decoders.
    Kit_PlayerVideoConfig video; ///< Video stream configuration
    Kit_PlayerAudioConfig audio; ///< Audio stream configuration
    Kit_PlayerSubtitleConfig subtitle; ///< Subtitle stream configuration
    Kit_PlayerDemuxerConfig demuxer;   ///< Demuxer configuration
} Kit_PlayerConfig;

/**
 * @brief Resets a player configuration to library defaults.
 *
 * @param config Configuration to reset. Must not be NULL.
 */
KIT_API void Kit_ResetPlayerConfig(Kit_PlayerConfig *config);

/**
 * @brief Creates a new player from a source.
 *
 * Creates a new player from the given source. The source must be previously successfully
 * initialized by calling either Kit_CreateSourceFromUrl() or Kit_CreateSourceFromCustom(),
 * and it must not be used by any other player. Source must stay valid during the whole
 * playback (as in, don't close it while stuff is playing).
 *
 * All tuning is passed via the config argument; see Kit_PlayerConfig for the available fields
 * and their defaults. Pass NULL to use defaults for everything. The config is copied at creation
 * (with out-of-range values clamped), so the same config object can be reused or discarded.
 *
 * It is possible to request for audio and video format conversions to be done automatically by
 * supplying requests via video_format_request and audio_format_request arguments. Conversions
 * are done on software, so they WILL add some cpu load! If you don't care what the output format is,
 * then just leave these to NULL, or feed the request objects but reset them to defaults with
 * Kit_ResetVideoFormatRequest() and Kit_ResetAudioFormatRequest().
 *
 * Screen width and height are used for subtitle positioning, scaling and rendering resolution.
 * Ideally this should be precisely the size of your screen surface (in pixels).
 * Higher resolution leads to higher resolution text rendering. This MUST be set precisely
 * if you plan to use font hinting! If you don't care or don't have subtitles at all,
 * set both to video surface size or 0. Unlike the config, the screen size is not fixed at
 * creation: it can be changed later with Kit_SetPlayerScreenSize().
 *
 * Stream indexes can be set manually, or picked automatically by using Kit_GetBestSourceStream().
 * Any stream can be left out with -1. Normally you want at least a video and/or an audio stream;
 * a player with every index set to -1 is valid, but idle (it has nothing to decode). A subtitle
 * stream requires a video stream to attach to, and text-based subtitle formats (SRT/ASS/SSA)
 * additionally require the library to be initialized with the KIT_INIT_ASS flag -- player
 * creation fails otherwise.
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
 * Kit_PlayerConfig config;
 * Kit_ResetPlayerConfig(&config);
 * config.video.frame_buffer_size = 1;
 *
 * Kit_Player *player = Kit_CreatePlayer(
 *     src,
 *     Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
 *     Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO),
 *     Kit_GetBestSourceStream(src, KIT_STREAMTYPE_SUBTITLE),
 *     &v_req, NULL,
 *     1280, 720,
 *     &config);
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
 * @param config Player configuration or NULL for defaults.
 * @return Initialized Kit_Player or NULL
 */
KIT_API Kit_Player *Kit_CreatePlayer(
    const Kit_Source *src,
    int video_stream_index,
    int audio_stream_index,
    int subtitle_stream_index,
    const Kit_VideoFormatRequest *video_format_request,
    const Kit_AudioFormatRequest *audio_format_request,
    int screen_w,
    int screen_h,
    const Kit_PlayerConfig *config
);

/**
 * @brief Close previously initialized player
 *
 * Closes a previously initialized Kit_Player instance. Note that this does NOT free
 * the linked Kit_Source -- you must free it manually.
 *
 * This must not be called while any other thread is still calling functions on the same player;
 * after this returns, the player pointer is invalid.
 *
 * @param player Player instance
 */
KIT_API void Kit_ClosePlayer(Kit_Player *player);

/**
 * @brief Sets the current screen size in pixels
 *
 * Call this to change the subtitle rendering resolution if eg. your
 * video window size changes. For text subtitles this changes the font rendering resolution;
 * for bitmap subtitles this changes the scaling of the rendered subtitle rectangles.
 *
 * This does nothing if subtitles are not in use.
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
 * @param audio_output Audio output frame buffer fill rate (0-100) or -1
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
 * @param audio_output Audio output frame buffer fill rate (0-100) or -1
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
 * Fetch buffering state for video stream (if a stream is selected). Any of the output pointers may be NULL.
 * If the player has no such stream, all values are reported as 0.
 *
 * @param player Player instance
 * @param frames_length Current size of the output buffer in decoded frames
 * @param frames_capacity Current maximum size of the output buffer in decoded frames
 * @param packets_length Current size of the input buffer in raw packets
 * @param packets_capacity Current maximum size of the input buffer in raw packets
 * @return true if the player has a video stream, false otherwise
 */
KIT_API bool Kit_GetPlayerVideoBufferState(
    const Kit_Player *player,
    unsigned int *frames_length,
    unsigned int *frames_capacity,
    unsigned int *packets_length,
    unsigned int *packets_capacity
);

/** @brief Gets the player audio buffering state
 *
 * Fetch buffering state for audio stream (if a stream is selected). Any of the output pointers may be NULL.
 * If the player has no such stream, all values are reported as 0.
 *
 * @param player Player instance
 * @param frames_length Current size of the output buffer in decoded audio frames
 * @param frames_capacity Current maximum size of the output buffer in decoded audio frames
 * @param packets_length Current size of the input buffer in raw packets
 * @param packets_capacity Current maximum size of the input buffer in raw packets
 * @return true if the player has a audio stream, false otherwise
 */
KIT_API bool Kit_GetPlayerAudioBufferState(
    const Kit_Player *player,
    unsigned int *frames_length,
    unsigned int *frames_capacity,
    unsigned int *packets_length,
    unsigned int *packets_capacity
);

/** @brief Gets the player subtitle buffering state
 *
 * Fetch buffering state for subtitle stream (if a stream is selected). Any of the output pointers may be NULL.
 * If the player has no such stream, all values are reported as 0.
 *
 * @param player Player instance
 * @param items_length Current size of the output buffer in subtitle elements
 * @param items_capacity Current maximum size of the output buffer in subtitle elements
 * @param packets_length Current size of the input buffer in raw packets
 * @param packets_capacity Current maximum size of the input buffer in raw packets
 * @return true if the player has a subtitle stream, false otherwise
 */
KIT_API bool Kit_GetPlayerSubtitleBufferState(
    const Kit_Player *player,
    unsigned int *items_length,
    unsigned int *items_capacity,
    unsigned int *packets_length,
    unsigned int *packets_capacity
);

/**
 * @brief Creates an SDL texture suitable for player video output
 *
 * The texture is created with the pixel format the player outputs (see Kit_GetPlayerInfo()) and
 * SDL_TEXTUREACCESS_STATIC access, and linear scale mode is set on SDL 2.0.12 or newer. Any w or h
 * argument <= 0 falls back to the player's video output dimensions for that axis.
 *
 * The caller owns the texture and must destroy it with SDL_DestroyTexture().
 *
 * @param player Player instance with a video stream
 * @param renderer Renderer to create the texture for
 * @param w Texture width in pixels, or <= 0 for the player's video output width
 * @param h Texture height in pixels, or <= 0 for the player's video output height
 * @return New texture on success, NULL on error (see Kit_GetError())
 */
KIT_API SDL_Texture *Kit_CreatePlayerVideoSDLTexture(const Kit_Player *player, SDL_Renderer *renderer, int w, int h);

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
 * @return 0 if the texture was updated; 1 if no new frame was available, playback is
 *         stopped or paused, or no video stream is selected.
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
 * then Kit_UnlockPlayerVideoRawFrame() must be called to clean up! On failure, Kit_UnlockPlayerVideoRawFrame()
 * must NOT be called.
 *
 * The player video output stays locked until Kit_UnlockPlayerVideoRawFrame() is called; the data pointers
 * are only valid inside this window, and stream switching waits for the lock to be released. Keep the
 * lock window short.
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
 * if(Kit_LockPlayerVideoRawFrame(player, &data, &line_size, &rect) == 0) {
 *     // Do something with the data here.
 *     Kit_UnlockPlayerVideoRawFrame(player);
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
 * @brief Creates an SDL texture suitable for use as the player subtitle atlas
 *
 * The texture is created with the pixel format the player outputs (see Kit_GetPlayerInfo()) and
 * SDL_TEXTUREACCESS_STATIC access, blend mode is set to SDL_BLENDMODE_BLEND, and nearest scale mode
 * is set on SDL 2.0.12 or newer. Any w or h argument <= 0 falls back to 4096, clamped to the
 * renderer's maximum texture size for that axis.
 *
 * The caller owns the texture and must destroy it with SDL_DestroyTexture().
 *
 * @param player Player instance with a subtitle stream
 * @param renderer Renderer to create the texture for
 * @param w Texture width in pixels, or <= 0 for the default (4096 clamped to renderer maximum)
 * @param h Texture height in pixels, or <= 0 for the default (4096 clamped to renderer maximum)
 * @return New texture on success, NULL on error (see Kit_GetError())
 */
KIT_API SDL_Texture *
Kit_CreatePlayerSubtitleSDLTexture(const Kit_Player *player, SDL_Renderer *renderer, int w, int h);

/**
 * @brief Fetches subtitle data from the player
 *
 * Output texture will be used as a texture atlas for the subtitle fragments.
 *
 * Note that the output texture must be previously allocated and valid. Make sure to have large
 * enough a texture for the rendering resolution you picked! If your rendering resolution is 4k,
 * then make sure to have texture sized 4096x4096 etc. This gives the texture room to handle the
 * worst case subtitle textures. Subtitle fragments that do not fit the atlas texture are dropped.
 *
 * Note that the texture format for the atlas texture *MUST* be SDL_PIXELFORMAT_RGBA32, the access
 * flag *MUST* be set to SDL_TEXTUREACCESS_STATIC and the scale mode *MUST* be nearest for correct
 * rendering. Using any other format will lead to undefined behaviour. The easiest way to get a correct
 * texture is to create it with Kit_CreatePlayerSubtitleSDLTexture().
 *
 * This function will do nothing if player playback has not been started. If the player is
 * paused, the atlas texture is not updated, but the currently visible rectangles are still
 * returned.
 *
 * For example:
 * ```
 * SDL_Rect sources[256];
 * SDL_Rect targets[256];
 * int got = Kit_GetPlayerSubtitleSDLTexture(player, subtitle_tex, sources, targets, 256);
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
 * @return Number of subtitle rectangles to render (may be 0)
 */
KIT_API int Kit_GetPlayerSubtitleSDLTexture(
    const Kit_Player *player, SDL_Texture *texture, SDL_Rect *sources, SDL_Rect *targets, int limit
);

/**
 * @brief Fetches raw subtitle frames from the player
 *
 * When called, this function will set the pointers for the items and targets lists of frame data and target
 * rectangles. The pointers will be valid until the next time this function is called, the subtitle stream
 * is switched or closed, or the player is closed.
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
 * @return Number of subtitle frames to render (may be 0)
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
 * The "backend_buffer_size" argument should be set to the amount of audio currently queued in the
 * backend (hardware) buffers. If your backend is SDL2, this can be provided by
 * SDL_GetQueuedAudioSize(). This information is used to supply silence if the backend queue is about
 * to run empty while the decoder momentarily has no audio data to give, protecting against audible
 * underruns. If you don't have this value, a large value (e.g. SIZE_MAX) disables the silence
 * padding, while 0 always enables it whenever the decoder has no data.
 *
 * This function will do nothing if player playback has not been started.
 *
 * This function is safe to call from an SDL audio callback.
 *
 * @param player Player instance
 * @param backend_buffer_size Amount of data currently queued to the driver/hw device.
 * @param buffer Buffer to read into
 * @param length Maximum length of the buffer
 * @return Amount of data (in bytes) that was read; 0 if no data was available
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
 * Note that this is not a plain read: when playback has reached the end of the source, this
 * call also settles the player into the KIT_STOPPED state (joining the finished background
 * threads). Poll it during playback to detect end of media.
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
 * Note that after calling this, Kit_GetPlayerPosition() will return 0.
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
 * If the player has already stopped, the playback will be restarted from the seek position.
 * If the player is paused, it will start from the seek position when playback is continued.
 *
 * Out-of-range targets are silently clamped to [0, duration] and the call still reports
 * success. Note also that the actual seek operation runs asynchronously on the demuxer
 * thread; if it fails there, playback simply continues from the old position.
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
 * Returns the position of the playback in seconds. If playback has not yet started,
 * or playback has been stopped, this will return 0.
 *
 * @param player Player instance
 * @return Position in seconds
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
 * Note that subtitle streams require an open video stream to render against.
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
