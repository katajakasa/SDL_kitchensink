#ifndef KITBUFFERINDEX
#define KITBUFFERINDEX

/**
 * @brief Indices identifying the per-stream-type buffer/track slot within player-internal arrays.
 *
 * @file kitbufferindex.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

/**
 * @brief Stream type index used to select among the video/audio/subtitle buffers of a player.
 */
typedef enum KitBufferIndex
{
    KIT_VIDEO_INDEX = 0,
    KIT_AUDIO_INDEX,
    KIT_SUBTITLE_INDEX,
    KIT_INDEX_COUNT
} Kit_BufferIndex;

#endif // KITBUFFERINDEX
