#include <libavcodec/avcodec.h>

#include "kitchensink/internal/audio/kitaudiopacket.h"


Kit_AudioPacket* Kit_CreateAudioPacket() {
    return calloc(1, sizeof(Kit_AudioPacket));
}

void Kit_FreeAudioPacket(Kit_AudioPacket **ref) {
    if (!ref || !*ref)
        return;
    Kit_AudioPacket *packet = *ref;
    if(packet->data)
        av_freep(&packet->data);
    free(packet);
    *ref = NULL;
}

void Kit_SetAudioPacketData(Kit_AudioPacket *packet, unsigned char *data, size_t length, double pts) {
    if(packet->data)
        av_freep(&packet->data);
    packet->data = data;
    packet->length = length;
    packet->left = length;
    packet->pts = pts;
}

void Kit_MoveAudioPacketRefs(Kit_AudioPacket *dst, Kit_AudioPacket *src) {
    if(dst->data)
        av_freep(&dst->data);
    dst->data = src->data;
    dst->length = src->length;
    dst->left = src->left;
    dst->pts = src->pts;
    memset(src, 0, sizeof(Kit_AudioPacket));
}

void Kit_DelAudioPacketRefs(Kit_AudioPacket *packet) {
    if(packet->data)
        av_freep(&packet->data);
    memset(packet, 0, sizeof(Kit_AudioPacket));
}
