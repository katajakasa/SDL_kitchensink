#ifndef KITSUBTITLEPACKET_H
#define KITSUBTITLEPACKET_H

/**
 * @brief A single decoded subtitle bitmap (or a "clear screen" marker) as it is passed through a
 * Kit_PacketBuffer between the subtitle renderer's decode side and its output/read side.
 *
 * @file kitsubtitlepacket.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <SDL3/SDL_surface.h>
#include <stdbool.h>

#include "kitchensink3/kitconfig.h"

/**
 * @brief One subtitle bitmap with its display window and position, or a screen-clear marker.
 * When @c clear is true the packet requests that previously shown subtitles be cleared, and
 * @c surface may be NULL. The packet owns @c surface once set (see Kit_SetSubtitlePacketData()).
 */
typedef struct Kit_SubtitlePacket {
    double pts_start;     ///< Presentation timestamp (seconds) when this subtitle becomes visible
    double pts_end;       ///< Presentation timestamp (seconds) when this subtitle should disappear
    int x;                ///< X position of the subtitle bitmap on the video frame
    int y;                ///< Y position of the subtitle bitmap on the video frame
    bool clear;           ///< If true, request clearing of previously displayed subtitles
    SDL_Surface *surface; ///< Subtitle bitmap surface, or NULL for a plain clear/empty packet
} Kit_SubtitlePacket;

/**
 * @brief Allocates a new, zeroed subtitle packet.
 *
 * @return newly allocated packet, or NULL on allocation failure
 */
KIT_LOCAL Kit_SubtitlePacket *Kit_CreateSubtitlePacket(void);

/**
 * @brief Frees a subtitle packet, releasing its surface, and nulls out the caller's pointer.
 *
 * @param packet pointer to the packet pointer to free; no-op if NULL or already-NULL pointee
 */
KIT_LOCAL void Kit_FreeSubtitlePacket(Kit_SubtitlePacket **packet);

/**
 * @brief Sets a subtitle packet's fields, freeing any surface it previously owned.
 *
 * @param packet packet to update
 * @param clear whether this packet requests clearing previously shown subtitles
 * @param pts_start presentation timestamp (seconds) when the subtitle becomes visible
 * @param pts_end presentation timestamp (seconds) when the subtitle should disappear
 * @param pos_x x position of the subtitle bitmap
 * @param pos_y y position of the subtitle bitmap
 * @param surface subtitle bitmap surface; ownership is transferred to the packet, or NULL
 */
KIT_LOCAL void Kit_SetSubtitlePacketData(
    Kit_SubtitlePacket *packet,
    bool clear,
    double pts_start,
    double pts_end,
    int pos_x,
    int pos_y,
    SDL_Surface *surface
);

/**
 * @brief Moves ownership of all fields (including the surface) from @p src to @p dst.
 *
 * Any surface previously owned by @p dst is freed first. After the move, @p src is zeroed out
 * and no longer owns anything. Used as the packet buffer's move callback.
 *
 * @param dst destination packet that receives ownership of src's contents
 * @param src source packet whose contents are moved out and which is zeroed afterwards
 */
KIT_LOCAL void Kit_MoveSubtitlePacketRefs(Kit_SubtitlePacket *dst, Kit_SubtitlePacket *src);

/**
 * @brief Releases a subtitle packet's contents and resets it to a zeroed, empty state.
 *
 * @param packet packet to clear
 * @param free_surface if true, frees the packet's surface before zeroing; if false, the
 * surface pointer is dropped without freeing it (ownership must already have been transferred
 * elsewhere, e.g. via Kit_MoveSubtitlePacketRefs())
 */
KIT_LOCAL void Kit_DelSubtitlePacketRefs(Kit_SubtitlePacket *packet, bool free_surface);

#endif // KITSUBTITLEPACKET_H
