#ifndef KITCONTROL_H
#define KITCONTROL_H

#include "kitchensink/kitconfig.h"
#include "kitchensink/kitplayer.h"

#define KIT_CBUFFERSIZE 8

typedef enum Kit_ControlPacketType {
    KIT_CONTROL_SEEK,
    KIT_CONTROL_FLUSH
} Kit_ControlPacketType;

typedef struct Kit_ControlPacket {
    Kit_ControlPacketType type;
    double value1;
} Kit_ControlPacket;

KIT_LOCAL Kit_ControlPacket* _CreateControlPacket(Kit_ControlPacketType type, double value1);
KIT_LOCAL void _FreeControlPacket(void *ptr);
KIT_LOCAL void _HandleFlushCommand(Kit_Player *player, Kit_ControlPacket *packet);
KIT_LOCAL void _HandleSeekCommand(Kit_Player *player, Kit_ControlPacket *packet);

#endif // KITCONTROL_H
