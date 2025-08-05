#include "kitchensink2/internal/subtitle/kitsubtitlepacket.h"

Kit_SubtitlePacket *Kit_CreateSubtitlePacket() {
    return calloc(1, sizeof(Kit_SubtitlePacket));
}

void Kit_FreeSubtitlePacket(Kit_SubtitlePacket **ref) {
    if(!ref || !*ref)
        return;
    Kit_SubtitlePacket *packet = *ref;
    SDL_FreeSurface(packet->surface);
    free(packet);
    *ref = NULL;
}

void Kit_SetSubtitlePacketData(
    Kit_SubtitlePacket *packet,
    bool clear,
    double pts_start,
    double pts_end,
    int pos_x,
    int pos_y,
    SDL_Surface *surface
) {
    if(packet->surface)
        SDL_FreeSurface(packet->surface);
    packet->pts_start = pts_start;
    packet->pts_end = pts_end;
    packet->x = pos_x;
    packet->y = pos_y;
    packet->surface = surface;
    packet->clear = clear;
}

void Kit_MoveSubtitlePacketRefs(Kit_SubtitlePacket *dst, Kit_SubtitlePacket *src) {
    if(dst->surface)
        SDL_FreeSurface(dst->surface);
    dst->pts_start = src->pts_start;
    dst->pts_end = src->pts_end;
    dst->x = src->x;
    dst->y = src->y;
    dst->surface = src->surface;
    dst->clear = src->clear;
    memset(src, 0, sizeof(Kit_SubtitlePacket));
}

void Kit_CreateSubtitlePacketRef(Kit_SubtitlePacket *dst, Kit_SubtitlePacket *src) {
}

void Kit_DelSubtitlePacketRefs(Kit_SubtitlePacket *packet, bool free_surface) {
    if(packet->surface && free_surface)
        SDL_FreeSurface(packet->surface);
    memset(packet, 0, sizeof(Kit_SubtitlePacket));
}
