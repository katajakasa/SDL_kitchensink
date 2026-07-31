#ifndef KITLIB_H
#define KITLIB_H

/**
 * @brief Library initialization and deinitialization functionality
 *
 * @file kitlib.h
 * @author Tuomas Virtanen
 * @date 2018-06-25
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/kitconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Font hinting options. Used as values for Kit_SetHint(KIT_HINT_FONT_HINTING, ...).
 */
enum
{
    KIT_FONT_HINTING_NONE = 0, ///< No hinting. This is recommended option
    KIT_FONT_HINTING_LIGHT,    ///< Light hinting. Use this if you need hinting
    KIT_FONT_HINTING_NORMAL,   ///< Not recommended, please see libass docs for details
    KIT_FONT_HINTING_NATIVE,   ///< Not recommended, please see libass docs for details
    KIT_FONT_HINTING_COUNT
};

/**
 * @brief SDL_kitchensink library version container
 */
typedef struct Kit_Version {
    unsigned char major; ///< Major version number, raising this signifies API breakage
    unsigned char minor; ///< Minor version number, small/internal changes
    unsigned char patch; ///< Patch version number, bugfixes etc.
} Kit_Version;

/**
 * @brief Library hint types. Used as keys for Kit_SetHint().
 *
 * Note that all of these must be set *before* player initialization for them to take effect!
 *
 * CAUTION on the buffer size hints: the defaults are chosen so that the pipeline can re-prime
 * itself after a seek. Very small audio buffer sizes (a few packets/frames) can deadlock
 * post-seek playback: the audio side holds data until the video stream re-bases the shared
 * clock, and with too little audio buffer slack that backpressure stalls the shared demuxer
 * thread before video can decode its first frame. Prefer the defaults; if you must shrink,
 * keep the audio buffers at a couple dozen packets/frames or more.
 */
typedef enum Kit_HintType
{
    KIT_HINT_FONT_HINTING,            ///< Set font hinting mode (currently used for libass)
    KIT_HINT_THREAD_COUNT,            ///< Set thread count for ffmpeg (default: 0 for autodetect)
    KIT_HINT_VIDEO_BUFFER_PACKETS,    ///< Video input buffer packets (default: 16)
    KIT_HINT_AUDIO_BUFFER_PACKETS,    ///< Audio input buffer packets (default: 64)
    KIT_HINT_SUBTITLE_BUFFER_PACKETS, ///< Subtitle input buffer packets (default: 64)
    KIT_HINT_VIDEO_BUFFER_FRAMES,     ///< Video output buffer frames (default: 2)
    KIT_HINT_AUDIO_BUFFER_FRAMES,     ///< Audio output buffer frames (default: 64)
    KIT_HINT_SUBTITLE_BUFFER_FRAMES,  ///< Subtitle output buffer size (default: 64; bitmap subtitles only)
    KIT_HINT_VIDEO_LATE_THRESHOLD,    ///< Late threshold for video frames in milliseconds (default: 50ms)
    KIT_HINT_VIDEO_EARLY_THRESHOLD,   ///< Early threshold for video frames in milliseconds (default: 5ms)
    KIT_HINT_AUDIO_LATE_THRESHOLD,    ///< Late threshold for audio frames in milliseconds (default: 50ms)
    KIT_HINT_AUDIO_EARLY_THRESHOLD,   ///< Early threshold for audio frames in milliseconds (default: 30ms)
    KIT_HINT_DEMUXER_READ_ATTEMPTS,   ///< Read attempts before a failing read is treated as end-of-stream (default: 3)
    KIT_HINT_DEMUXER_READ_RETRY_DELAY, ///< Delay between read retry attempts in milliseconds (default: 10ms)
} Kit_HintType;

/**
 * @brief Library initialization options, please see Kit_Init()
 */
enum
{
    KIT_INIT_NETWORK = 0x1, ///< Initialise ffmpeg network support
    KIT_INIT_ASS = 0x2,     ///< Initialize libass support (library must be linked statically or loadable dynamically)
    KIT_INIT_HW_DECODE = 0x4, ///< Enable hardware decoding
};

/**
 * @brief Initialize SDL_kitchensink library.
 *
 * This MUST be run before doing anything. After you are done using the library, you should use Kit_Quit() to
 * deinitialize everything. Otherwise there might be resource leaks.
 *
 * Following flags can be used to initialize subsystems:
 * - `KIT_INIT_NETWORK` for ffmpeg network support (playback from the internet, for example)
 * - `KIT_INIT_ASS` for libass subtitles (text and ass/ssa subtitle support). NOTE: this flag is
 *   *required* for playing text-based subtitle streams (SRT/ASS/SSA) -- without it, creating a
 *   player with such a subtitle stream selected fails. Bitmap subtitles work without it.
 * - `KIT_INIT_HW_DECODE` to enable hardware decoding capabilities. Hardware is picked automatically.
 *
 * Note that if this function fails, the failure reason should be available via Kit_GetError().
 *
 * For example:
 * ```
 * if(Kit_Init(KIT_INIT_NETWORK|KIT_INIT_ASS|KIT_INIT_HW_DECODE) != 0) {
 *     fprintf(stderr, "Error: %s\n", Kit_GetError());
 *     return 1;
 * }
 * ```
 *
 * @param flags Library initialization flags
 * @return Returns 0 on success, 1 on failure.
 */
KIT_API int Kit_Init(unsigned int flags);

/**
 * @brief Deinitializes SDL_kitchensink
 *
 * Note that any calls to library functions after this will cause undefined behaviour!
 */
KIT_API void Kit_Quit();

/**
 * @brief Sets a library-wide hint
 *
 * This can be used to set hints on how the library should behave. See Kit_HintType
 * for all the options.
 *
 * Out-of-range values are silently clamped to the valid range (e.g. buffer sizes have a
 * minimum of 1). Unknown hint types are ignored.
 *
 * @param type Hint type (refer to Kit_HintType for options)
 * @param value Value for the hint
 */
KIT_API void Kit_SetHint(Kit_HintType type, int value);

/**
 * @brief Gets a previously set or default hint value
 *
 * @param type Hint type (refer to Kit_HintType for options)
 * @return Hint value, or 0 for unknown hint types
 */
KIT_API int Kit_GetHint(Kit_HintType type);

/**
 * @brief Can be used to fetch the version of the linked SDL_kitchensink library
 *
 * @param version Allocated Kit_Version
 */
KIT_API void Kit_GetVersion(Kit_Version *version);

#ifdef __cplusplus
}
#endif

#endif // KITLIB_H
