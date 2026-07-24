#ifndef KITPACKETTAG_H
#define KITPACKETTAG_H

// Each AVPacket and AVFrame has a tag in the opaque handle.
typedef enum Kit_PacketType
{
    KIT_PACKET_TYPE_DATA = 0,
    KIT_PACKET_TYPE_SEEK = 1,
    KIT_PACKET_TYPE_EOF = 2,
} Kit_PacketType;

typedef union Kit_PacketTag {
    void *opaque;
    struct {
        unsigned int type : 2;
        unsigned int serial : 30;
    } bits;
} Kit_PacketTag;

static inline void *Kit_CreatePacketTag(Kit_PacketType type, unsigned int serial) {
    Kit_PacketTag tag = {0};
    tag.bits.type = type;
    tag.bits.serial = serial;
    return tag.opaque;
}

static inline Kit_PacketType Kit_GetPacketType(void *opaque) {
    const Kit_PacketTag tag = {.opaque = opaque};
    return (Kit_PacketType)tag.bits.type;
}

static inline unsigned int Kit_GetPacketSerial(void *opaque) {
    const Kit_PacketTag tag = {.opaque = opaque};
    return tag.bits.serial;
}

#endif // KITPACKETTAG_H
