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

#include "kitchensink3/internal/kitbufferindex.h"
#include "kitchensink3/internal/kitpacketbuffer.h"
#include "kitchensink3/internal/kittimer.h"
#include "kitchensink3/kitconfig.h"
#include "kitchensink3/kitplayer.h"
#include "kitchensink3/kitsource.h"

#include <SDL3/SDL_atomic.h>
#include <libavcodec/avcodec.h>
#include <stdbool.h>

/**
 * @brief Demuxer state: source, one packet buffer and stream index per stream type, and a scratch packet.
 */
typedef struct Kit_Demuxer {
    const Kit_Source *src;                        ///< Source being demuxed; not owned.
    Kit_PacketBuffer *buffers[KIT_INDEX_COUNT];   ///< Per-stream-type output packet buffers; NULL if unused.
    SDL_AtomicInt stream_indexes[KIT_INDEX_COUNT]; ///< Per-stream-type source stream index; -1 if unused.
    SDL_AtomicInt abort_requested;                 ///< Breaks the read-retry delay in Kit_RunDemuxer() on abort.
    AVPacket *scratch_packet;                     ///< Reusable packet used for reading/writing.
    int read_attempts;                            ///< Read attempts before a failure is treated as EOF.
    int read_retry_delay;                         ///< Delay between read attempts, in milliseconds.
} Kit_Demuxer;

/**
 * @brief Creates a demuxer for a source, allocating a packet buffer for each requested stream index.
 *
 * @param src Source to demux from; must stay valid for the demuxer's lifetime.
 * @param video_index Video stream index to demux, or -1 to skip video.
 * @param audio_index Audio stream index to demux, or -1 to skip audio.
 * @param subtitle_index Subtitle stream index to demux, or -1 to skip subtitles.
 * @param config Player configuration to copy buffer sizes and read-retry settings from; not retained.
 * @return New demuxer, or NULL on allocation failure (Kit_SetError() is called).
 */
KIT_LOCAL Kit_Demuxer *Kit_CreateDemuxer(
    const Kit_Source *src, int video_index, int audio_index, int subtitle_index, const Kit_PlayerConfig *config
);

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
 * demuxer_read_* fields of Kit_PlayerConfig. A genuine AVERROR_EOF is never retried. Packets for streams that
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
 * @brief Flushes all of the demuxer's packet buffers and clears the demuxer's abort state.
 *
 * @param demuxer Demuxer whose buffers to flush; no-op if NULL.
 */
KIT_LOCAL void Kit_ClearDemuxerBuffers(Kit_Demuxer *demuxer);

/**
 * @brief Unblocks any full/empty waits on all of the demuxer's packet buffers, and breaks an ongoing
 * read-retry delay in Kit_RunDemuxer().
 *
 * Only wakes blocked buffer operations and retry waits; it does not stop the demuxer thread's run loop
 * by itself. The abort state is cleared by Kit_ClearDemuxerBuffers().
 *
 * @param demuxer Demuxer to abort; no-op if NULL.
 */
KIT_LOCAL void Kit_AbortDemuxer(Kit_Demuxer *demuxer);

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
