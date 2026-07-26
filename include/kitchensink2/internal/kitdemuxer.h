#ifndef KITDEMUXER_H
#define KITDEMUXER_H

/**
 * @brief Reads packets from an AVFormatContext-backed Kit_Source and routes them into per-stream-type packet
 * buffers (video/audio/subtitle), for decoder threads to consume. Also handles seeking.
 *
 * @file kitdemuxer.h
 * @author Tuomas Virtanen
 * @copyright Tuomas Virtanen; MIT license (see LICENSE)
 */

#include "kitchensink2/internal/kitbufferindex.h"
#include "kitchensink2/internal/kitpacketbuffer.h"
#include "kitchensink2/internal/kittimer.h"
#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitsource.h"

#include <SDL_atomic.h>
#include <libavcodec/avcodec.h>
#include <stdbool.h>

/**
 * @brief Demuxer state: source, one packet buffer and stream index per stream type, and a scratch packet.
 */
typedef struct Kit_Demuxer {
    const Kit_Source *src;                          ///< Source being demuxed; not owned.
    Kit_PacketBuffer *buffers[KIT_INDEX_COUNT];      ///< Per-stream-type output packet buffers; NULL if unused.
    SDL_atomic_t stream_indexes[KIT_INDEX_COUNT];    ///< Per-stream-type source stream index; -1 if unused.
    AVPacket *scratch_packet;                        ///< Reusable packet used for reading/writing.
} Kit_Demuxer;

/**
 * @brief Creates a demuxer for a source, allocating a packet buffer for each requested stream index.
 *
 * @param src Source to demux from; must stay valid for the demuxer's lifetime.
 * @param video_index Video stream index to demux, or -1 to skip video.
 * @param audio_index Audio stream index to demux, or -1 to skip audio.
 * @param subtitle_index Subtitle stream index to demux, or -1 to skip subtitles.
 * @return New demuxer, or NULL on allocation failure (Kit_SetError() is called).
 */
KIT_LOCAL Kit_Demuxer *Kit_CreateDemuxer(const Kit_Source *src, int video_index, int audio_index, int subtitle_index);

/**
 * @brief Frees a demuxer's packet buffers, scratch packet and the struct itself.
 *
 * @param demuxer Pointer to the demuxer pointer; set to NULL after closing. No-op if NULL or already-NULL.
 */
KIT_LOCAL void Kit_CloseDemuxer(Kit_Demuxer **demuxer);

/**
 * @brief Reads and routes one packet from the source into the matching stream-type buffer.
 *
 * Transient read errors are retried (with a delay) up to the configured attempt limit, per the
 * KIT_HINT_DEMUXER_READ_* library hints. A genuine AVERROR_EOF is never retried. Packets for streams that
 * are not selected are dropped. Writing into a buffer may block if that buffer is currently full.
 *
 * @param demuxer Demuxer to run.
 * @return true if a packet was read (whether routed or dropped); false on EOF or after exhausting retries.
 */
KIT_LOCAL bool Kit_RunDemuxer(Kit_Demuxer *demuxer);

/**
 * @brief Gets the packet buffer for a given stream type.
 *
 * @param demuxer Demuxer to query; must not be NULL.
 * @param buffer_index Stream type to look up.
 * @return The packet buffer for that stream type (may be NULL if that stream type is unused).
 */
KIT_LOCAL Kit_PacketBuffer *Kit_GetDemuxerPacketBuffer(const Kit_Demuxer *demuxer, Kit_BufferIndex buffer_index);

/**
 * @brief Flushes all of the demuxer's packet buffers.
 *
 * @param demuxer Demuxer whose buffers to flush; no-op if NULL.
 */
KIT_LOCAL void Kit_ClearDemuxerBuffers(const Kit_Demuxer *demuxer);

/**
 * @brief Unblocks any full/empty waits on all of the demuxer's packet buffers.
 *
 * Only wakes blocked buffer operations; it does not stop the demuxer thread's run loop by itself.
 *
 * @param demuxer Demuxer to abort; no-op if NULL.
 */
KIT_LOCAL void Kit_AbortDemuxer(const Kit_Demuxer *demuxer);

/**
 * @brief Seeks the underlying format context and, on success, flushes buffers and injects a seek marker packet.
 *
 * On success, flushes all packet buffers, bumps @p timer's clock serial via Kit_IncreaseTimerSerial(), and
 * writes a seek-tagged packet (carrying the new serial) into every active buffer so decoder threads can
 * detect the seek and re-base their clocks. On failure, no state is changed and playback continues from the
 * old position; the clock serial is only bumped when the seek succeeds.
 *
 * @param demuxer Demuxer to seek.
 * @param timer Sync timer whose serial is bumped on a successful seek.
 * @param seek_target Target position, in AV_TIME_BASE units (passed through to avformat_seek_file()).
 * @return true if avformat_seek_file() succeeded, false otherwise.
 */
KIT_LOCAL bool Kit_DemuxerSeek(Kit_Demuxer *demuxer, Kit_Timer *timer, int64_t seek_target);

/**
 * @brief Flushes and reassigns the source stream index used for one stream type (e.g. on an audio track switch).
 *
 * @param demuxer Demuxer to update.
 * @param index Stream type whose index to change.
 * @param stream_index New source stream index to demux for that type.
 */
KIT_LOCAL void Kit_SetDemuxerStreamIndex(Kit_Demuxer *demuxer, Kit_BufferIndex index, int stream_index);

/**
 * @brief Writes an EOF-tagged sentinel packet into one stream type's buffer, signaling its decoder to drain.
 *
 * @param demuxer Demuxer to send from.
 * @param index Stream type whose buffer receives the EOF packet; no-op if that buffer doesn't exist.
 */
KIT_LOCAL void Kit_SendDemuxerEOFPacket(Kit_Demuxer *demuxer, Kit_BufferIndex index);

/**
 * @brief Gets the current fill level and capacity of one stream type's packet buffer.
 *
 * @param demuxer Demuxer to query; no-op if NULL or if that buffer doesn't exist.
 * @param buffer_index Stream type to query.
 * @param length Receives current buffered packet count; left untouched if NULL.
 * @param capacity Receives buffer capacity; left untouched if NULL.
 */
KIT_LOCAL void Kit_GetDemuxerBufferState(
    const Kit_Demuxer *demuxer, Kit_BufferIndex buffer_index, unsigned int *length, unsigned int *capacity
);

#endif // KITDEMUXER_H
