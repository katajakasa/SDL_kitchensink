#include <assert.h>

#include "kitchensink/internal/subtitle/kitatlas.h"
#include "kitchensink/internal/utils/kitlog.h"

static int min(int a, int b) {
    if(a < b)
        return a;
    return b;
}


Kit_TextureAtlas* Kit_CreateAtlas(int w, int h) {
    assert(w >= 1024);
    assert(h >= 1024);

    Kit_TextureAtlas *atlas = calloc(1, sizeof(Kit_TextureAtlas));
    if(atlas == NULL) {
        goto exit_0;
    }
    atlas->cur_items = 0;
    atlas->max_items = 1024;
    atlas->w = w;
    atlas->h = h;

    atlas->items = calloc(atlas->max_items, sizeof(Kit_TextureAtlasItem));
    if(atlas->items == NULL) {
        goto exit_1;
    }

    memset(atlas->shelf, 0, sizeof(atlas->shelf));
    return atlas;

exit_1:
    free(atlas);
exit_0:
    return NULL;
}

void Kit_ResetAtlasContent(Kit_TextureAtlas *atlas) {
    // Clear shelves & allocations on items
    Kit_TextureAtlasItem *item;
    memset(atlas->shelf, 0, sizeof(atlas->shelf));
    for(int i = 0; i < atlas->cur_items; i++) {
        item = &atlas->items[i];
        item->cur_shelf = -1;
        item->cur_slot = -1;
        item->is_copied = false;
    }
}

void Kit_ClearAtlasContent(Kit_TextureAtlas *atlas) {
    Kit_TextureAtlasItem *item;
    for(int i = 0; i < atlas->cur_items; i++) {
        item = &atlas->items[i];
        SDL_FreeSurface(item->surface);
    }
    atlas->cur_items = 0;
    memset(atlas->shelf, 0, sizeof(atlas->shelf));
}

void Kit_FreeAtlas(Kit_TextureAtlas *atlas) {
    assert(atlas != NULL);

    Kit_ClearAtlasContent(atlas);
    free(atlas->items);
    free(atlas);
}

void Kit_SetItemAllocation(Kit_TextureAtlasItem *item, int shelf, int slot, int x, int y) {
    item->cur_shelf = shelf;
    item->cur_slot = slot;
    item->source.x = x;
    item->source.y = y;
    item->source.w = item->surface->w;
    item->source.h = item->surface->h;
    //LOG("Writing x,y=(%d, %d) w,h=(%d, %d) on shelf %d slot %d\n",
    //    x, y, item->surface->w, item->surface->h, shelf, slot);
}

int Kit_FindFreeAtlasSlot(Kit_TextureAtlas *atlas, Kit_TextureAtlasItem *item) {
    int shelf_w, shelf_h;
    int total_remaining_h = atlas->h;
    int total_reserved_h = 0;

    // First, try to look for a good, existing shelf
    int best_shelf_idx = -1;
    int best_shelf_h = 0;
    
    // Try to find a good shelf to put this item in
    int shelf_idx;
    for(shelf_idx = 0; shelf_idx < MAX_SHELVES; shelf_idx++) {
        shelf_w = atlas->shelf[shelf_idx][0];
        shelf_h = atlas->shelf[shelf_idx][1];
        if(shelf_h == 0) {
            break;
        }
        total_remaining_h -= shelf_h;
        total_reserved_h += shelf_h;

        // If the item fits, check if the space is better than previous one
        if(item->surface->w <= (atlas->w - shelf_w) && item->surface->h <= shelf_h) {
            if(shelf_h > best_shelf_h) {
                best_shelf_h = shelf_h;
                best_shelf_idx = shelf_idx;
            }
        }
    }

    // If existing shelf found, put the item there. Otherwise create a new shelf.
    if(best_shelf_idx != -1) {
        Kit_SetItemAllocation(
            item,
            best_shelf_idx,
            atlas->shelf[best_shelf_idx][2],
            atlas->shelf[best_shelf_idx][0],
            total_reserved_h);
        atlas->shelf[best_shelf_idx][0] += item->surface->w;
        atlas->shelf[best_shelf_idx][2] += 1;
        return 0;
    } else if(total_remaining_h > item->surface->h) {
        atlas->shelf[shelf_idx][0] = item->surface->w;
        atlas->shelf[shelf_idx][1] = item->surface->h;
        atlas->shelf[shelf_idx][2] = 1;
        Kit_SetItemAllocation(
            item,
            shelf_idx,
            0,
            0,
            total_reserved_h);
        return 0;
    }

    return 1; // Can't fit!
}

int Kit_AllocateAtlasSurfaces(Kit_TextureAtlas *atlas, bool halt_on_errors) {
    int ret = 0;
    Kit_TextureAtlasItem *item;
    for(int i = 0; i < atlas->cur_items; i++) {
        item = &atlas->items[i];
        if(item->cur_shelf > -1) // Stop if already allocated
            continue;
        if(Kit_FindFreeAtlasSlot(atlas, item) != 0) { // If nothing else helps, create a new slot
            ret = 1;
            if(halt_on_errors)
                goto exit_0;
        }
    }

exit_0:
    return ret;
}

void Kit_BlitAtlasSurfaces(Kit_TextureAtlas *atlas, SDL_Texture *texture) {
    Kit_TextureAtlasItem *item;
    for(int i = 0; i < atlas->cur_items; i++) {
        item = &atlas->items[i];
        if(!item->is_copied) { // Copy if not yet copied
            SDL_UpdateTexture(texture, &item->source, item->surface->pixels, item->surface->pitch);
            item->is_copied = true;
        }
    }
}

int Kit_UpdateAtlasTexture(Kit_TextureAtlas *atlas, SDL_Texture *texture) {
    assert(atlas != NULL);
    assert(texture != NULL);

    // Check if texture size has changed
    int texture_w, texture_h;
    if(SDL_QueryTexture(texture, NULL, NULL, &texture_w, &texture_h) == 0) {
        if(texture_w != atlas->w || texture_h != atlas->h) {
            Kit_ResetAtlasContent(atlas);
        }
        atlas->w = texture_w;
        atlas->h = texture_h;
    }

    if(Kit_AllocateAtlasSurfaces(atlas, true) != 0) {
        LOG("WARNING! FULL ATLAS, CANNOT MAKE MORE ROOM!\n");
    }

    // Blit unblitted surfaces to texture
    Kit_BlitAtlasSurfaces(atlas, texture);
    return 0;
}

int Kit_GetAtlasItems(const Kit_TextureAtlas *atlas, SDL_Rect *sources, SDL_Rect *targets, int limit) {
    int count = 0;
    for(int i = 0; i < min(atlas->cur_items, limit); i++) {
        Kit_TextureAtlasItem *item = &atlas->items[i];
        
        if(sources != NULL)
            memcpy(&sources[count], &item->source, sizeof(SDL_Rect));
        if(targets != NULL)
            memcpy(&targets[count], &item->target, sizeof(SDL_Rect));

        count++;
    }
    return count;
}

int Kit_AddAtlasItem(Kit_TextureAtlas *atlas, SDL_Surface *surface, SDL_Rect *target) {
    assert(atlas != NULL);
    assert(surface != NULL);
    assert(target != NULL);

    if(atlas->cur_items >= atlas->max_items) {
        SDL_FreeSurface(surface);
        return -1; // Cannot fit, buffer full!
    }

    Kit_TextureAtlasItem *item = &atlas->items[atlas->cur_items];
    item->cur_shelf = -1;
    item->cur_slot = -1;
    item->is_copied = false;
    item->surface = surface;
    memset(&item->source, 0, sizeof(SDL_Rect));
    memcpy(&item->target, target, sizeof(SDL_Rect));
    return atlas->cur_items++;
}

void Kit_FreeAtlasItem(Kit_TextureAtlas *atlas, int item_id) {
    assert(item_id >= 0);
    SDL_FreeSurface(atlas->items[item_id].surface);
    memmove(&atlas->items[item_id],
            &atlas->items[item_id+1],
            (atlas->max_items - item_id - 1) * sizeof(Kit_TextureAtlasItem));
    atlas->cur_items--;
}
