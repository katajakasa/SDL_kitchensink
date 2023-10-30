#ifndef KITAUDIOPACKET_H
#define KITAUDIOPACKET_H

#include <stdlib.h>
#include "kitchensink/kitconfig.h"

typedef struct Kit_AudioPacket {
    double pts;
    unsigned char *data;
    size_t length;
    size_t left;
} Kit_AudioPacket;

KIT_LOCAL Kit_AudioPacket* Kit_CreateAudioPacket();
KIT_LOCAL void Kit_FreeAudioPacket(Kit_AudioPacket **packet);
KIT_LOCAL void Kit_SetAudioPacketData(Kit_AudioPacket *packet, unsigned char *data, size_t length, double pts);
KIT_LOCAL void Kit_MoveAudioPacketRefs(Kit_AudioPacket *dst, Kit_AudioPacket *src);
KIT_LOCAL void Kit_DelAudioPacketRefs(Kit_AudioPacket *packet);

#endif // KITAUDIOPACKET_H
