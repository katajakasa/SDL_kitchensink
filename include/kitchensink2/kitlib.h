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
 * @brief SDL_kitchensink library version container
 */
typedef struct Kit_Version {
    unsigned char major; ///< Major version number, raising this signifies API breakage
    unsigned char minor; ///< Minor version number, small/internal changes
    unsigned char patch; ///< Patch version number, bugfixes etc.
} Kit_Version;

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
 * @brief Can be used to fetch the version of the linked SDL_kitchensink library
 *
 * @param version Allocated Kit_Version
 */
KIT_API void Kit_GetVersion(Kit_Version *version);

#ifdef __cplusplus
}
#endif

#endif // KITLIB_H
