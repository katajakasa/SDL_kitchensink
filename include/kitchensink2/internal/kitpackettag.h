#ifndef KITPACKETTAG_H
#define KITPACKETTAG_H

/**
 * @brief Packs a packet/frame type and a seek serial number into a pointer-sized opaque tag,
 * so they can be stashed in the `opaque` field of an AVPacket/AVFrame without extra allocation.
 *
 * @file kitpackettag.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

// Each AVPacket and AVFrame has a tag in the opaque handle.
/**
 * @brief Kind of data carried by a tagged AVPacket/AVFrame.
 */
typedef enum Kit_PacketType
{
    KIT_PACKET_TYPE_DATA = 0,
    KIT_PACKET_TYPE_SEEK = 1,
    KIT_PACKET_TYPE_EOF = 2,
} Kit_PacketType;

/**
 * @brief Bit-packed view of an opaque tag pointer: a 2-bit Kit_PacketType and a 30-bit serial.
 */
typedef union Kit_PacketTag {
    void *opaque;
    struct {
        unsigned int type : 2;
        unsigned int serial : 30;
    } bits;
} Kit_PacketTag;

/**
 * @brief Builds an opaque tag pointer encoding a packet type and seek serial.
 *
 * @param type Packet type to encode (2 bits)
 * @param serial Seek serial to encode (low 30 bits used; higher bits are truncated)
 * @return Opaque pointer value suitable for storing in an AVPacket/AVFrame's opaque field
 */
static inline void *Kit_CreatePacketTag(Kit_PacketType type, unsigned int serial) {
    Kit_PacketTag tag = {0};
    tag.bits.type = type;
    tag.bits.serial = serial;
    return tag.opaque;
}

/**
 * @brief Extracts the packet type from an opaque tag previously built with Kit_CreatePacketTag().
 *
 * @param opaque Opaque tag pointer
 * @return Decoded packet type
 */
static inline Kit_PacketType Kit_GetPacketType(void *opaque) {
    const Kit_PacketTag tag = {.opaque = opaque};
    return (Kit_PacketType)tag.bits.type;
}

/**
 * @brief Extracts the seek serial from an opaque tag previously built with Kit_CreatePacketTag().
 *
 * @param opaque Opaque tag pointer
 * @return Decoded 30-bit serial value
 */
static inline unsigned int Kit_GetPacketSerial(void *opaque) {
    const Kit_PacketTag tag = {.opaque = opaque};
    return tag.bits.serial;
}

#endif // KITPACKETTAG_H
