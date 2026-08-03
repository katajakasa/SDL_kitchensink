#ifndef KITATLAS_H
#define KITATLAS_H

/**
 * @brief A simple shelf-packing texture atlas used to pack subtitle bitmaps into a single output
 * texture, so that many small subtitle rects can be drawn with a single SDL texture.
 *
 * @file kitatlas.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>

#include "kitchensink3/kitconfig.h"

/**
 * @brief A single packed item: where its pixels live on the cache surface/texture (source),
 * and where it should be drawn on the output surface (target).
 */
typedef struct Kit_TextureAtlasItem {
    SDL_Rect source;  //< Source coordinates on cache surface
    SDL_FRect target; //< Target coordinates on output surface
} Kit_TextureAtlasItem;

/**
 * @brief A horizontal strip of the atlas used to pack items of similar height together.
 */
typedef struct Kit_Shelf {
    uint16_t width;  ///< Width currently used on this shelf
    uint16_t height; ///< Height of this shelf (height of the tallest item placed on it)
    uint16_t count;  ///< Number of items currently placed on this shelf
} Kit_Shelf;

/**
 * @brief Texture atlas state: a fixed-capacity list of packed items plus the shelves used to
 * lay them out. Does not own any SDL_Texture; callers supply the destination texture per call.
 */
typedef struct Kit_TextureAtlas {
    int cur_items;               //< Current items count
    int max_items;               //< Maximum items count
    int max_shelves;             //< Maximum shelf count
    int w;                       //< Current atlas width
    int h;                       //< Current atlas height
    Kit_TextureAtlasItem *items; //< Cached items
    Kit_Shelf *shelves;          //< Atlas shelves
} Kit_TextureAtlas;

/**
 * @brief Allocates a new, empty texture atlas with a fixed item/shelf capacity (1024/256).
 *
 * @return newly allocated atlas, or NULL on allocation failure
 */
KIT_LOCAL Kit_TextureAtlas *Kit_CreateAtlas(void);

/**
 * @brief Frees an atlas and its item/shelf storage.
 *
 * @param atlas atlas to free; must not be NULL
 */
KIT_LOCAL void Kit_FreeAtlas(Kit_TextureAtlas *atlas);

/**
 * @brief Resets the atlas to empty, clearing all packed items and shelves without freeing memory.
 *
 * @param atlas atlas to clear
 */
KIT_LOCAL void Kit_ClearAtlasContent(Kit_TextureAtlas *atlas);

/**
 * @brief Queries the given texture's size and updates the atlas's tracked width/height if it
 * changed. Callers are expected to clear the atlas content when this indicates a size change.
 *
 * @param atlas atlas whose tracked width/height is updated
 * @param texture texture to query the size of
 */
KIT_LOCAL void Kit_CheckAtlasTextureSize(Kit_TextureAtlas *atlas, SDL_Texture *texture);

/**
 * @brief Copies up to @p limit packed items' source and/or target rects out of the atlas.
 *
 * @param atlas atlas to read from
 * @param sources destination array for source rects, or NULL to skip
 * @param targets destination array for target rects, or NULL to skip
 * @param limit maximum number of items to copy
 * @return number of items actually copied (min of current item count and limit)
 */
KIT_LOCAL int Kit_GetAtlasItems(const Kit_TextureAtlas *atlas, SDL_FRect *sources, SDL_FRect *targets, int limit);

/**
 * @brief Packs a surface into the atlas: finds free space, uploads the surface pixels into
 * that region of @p texture via SDL_UpdateTexture, and records the item with the given target
 * rect. The atlas does not take ownership of @p surface or @p texture.
 *
 * @param atlas atlas to add the item to
 * @param texture destination texture that backs the atlas's packed regions
 * @param surface source pixel surface to copy into the atlas
 * @param target output rect where this item should be drawn
 * @return 0 on success, -1 if the atlas is full or no free slot could be found for the surface
 */
KIT_LOCAL int
Kit_AddAtlasItem(Kit_TextureAtlas *atlas, SDL_Texture *texture, const SDL_Surface *surface, const SDL_FRect *target);

#endif // KITATLAS_H
