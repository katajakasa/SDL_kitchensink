#include <assert.h>

#include <SDL_timer.h>
#include <libavformat/avformat.h>

#include "kitchensink2/internal/kitdemuxer.h"
#include "kitchensink2/internal/kitfaultinject.h"
#include "kitchensink2/internal/kitpacketbuffer.h"
#include "kitchensink2/internal/kitpackettag.h"
#include "kitchensink2/internal/utils/kitalloc.h"
#include "kitchensink2/kiterror.h"

void Kit_SendDemuxerEOFPacket(Kit_Demuxer *demuxer, Kit_BufferIndex index) {
    AVPacket *packet;
    if(!demuxer->buffers[index])
        return;
    // Use a local packet instead of the shared scratch packet -- this may get called from the API thread
    // (stream switch) while the demuxer thread is still sending its own EOF packets on exit.
    if((packet = av_packet_alloc()) == NULL)
        return;
    packet->opaque = Kit_CreatePacketTag(KIT_PACKET_TYPE_EOF, 0);
    Kit_WritePacketBuffer(demuxer->buffers[index], packet);
    av_packet_free(&packet);
}

/**
 * Sleep up to delay_ms in small slices, bailing out early if the demuxer gets aborted. This keeps
 * player stop/close/seek latency bounded by a single slice, no matter how large the configured
 * retry delay is. Returns false if the demuxer was aborted.
 */
static bool Kit_DemuxerRetryDelay(Kit_Demuxer *demuxer, unsigned int delay_ms) {
    while(delay_ms > 0) {
        if(SDL_AtomicGet(&demuxer->abort_requested))
            return false;
        const unsigned int slice = delay_ms < 10 ? delay_ms : 10;
        SDL_Delay(slice);
        delay_ms -= slice;
    }
    return !SDL_AtomicGet(&demuxer->abort_requested);
}

bool Kit_RunDemuxer(Kit_Demuxer *demuxer) {
    for(unsigned int attempt = 0;; attempt++) {
        const int ret =
            KIT_FAULT_WRAP_CODE("demux_read", av_read_frame(demuxer->src->format_ctx, demuxer->scratch_packet));
        if(ret >= 0)
            break;
        if(ret == AVERROR_EOF)
            return false;
        if(attempt >= (unsigned int)demuxer->read_attempts - 1)
            return false;
        // On abort, bail out through the EOF path. The EOF packets written by the demuxer thread
        // then land in already-aborted buffers and are simply dropped.
        if(!Kit_DemuxerRetryDelay(demuxer, demuxer->read_retry_delay))
            return false;
    }

    // Figure out if we are interested in this stream. If we are, write the packet to a buffer for decoder to pick up.
    // Note that Kit_WritePacketBuffer() may block if the buffer is full. It will also move the scratch_packet
    // references to its own buffer, leaving the scratch_buffer in a clean state.
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        if(demuxer->scratch_packet->stream_index == SDL_AtomicGet(&demuxer->stream_indexes[i])) {
            if(!Kit_WritePacketBuffer(demuxer->buffers[i], demuxer->scratch_packet))
                av_packet_unref(demuxer->scratch_packet);
            return true;
        }
    }

    // Packet does not belong to any stream we are interested in, so get rid of it.
    av_packet_unref(demuxer->scratch_packet);
    return true;
}

Kit_Demuxer *Kit_CreateDemuxer(
    const Kit_Source *src, int video_index, int audio_index, int subtitle_index, const Kit_PlayerConfig *config
) {
    Kit_Demuxer *demuxer = NULL;
    Kit_PacketBuffer *video_buf = NULL;
    Kit_PacketBuffer *audio_buf = NULL;
    Kit_PacketBuffer *subtitle_buf = NULL;
    AVPacket *scratch_packet;

    if((demuxer = Kit_Calloc(1, sizeof(Kit_Demuxer))) == NULL) {
        Kit_SetError("Unable to allocate demuxer");
        goto error_0;
    }
    if((scratch_packet = av_packet_alloc()) == NULL) {
        goto error_1;
    }
    if(video_index >= 0) {
        video_buf = Kit_CreatePacketBuffer(
            config->video.packet_buffer_size,
            (buf_obj_alloc)av_packet_alloc,
            (buf_obj_unref)av_packet_unref,
            (buf_obj_free)av_packet_free,
            (buf_obj_move)av_packet_move_ref,
            (buf_obj_ref)av_packet_ref
        );
        if(video_buf == NULL) {
            Kit_SetError("Unable to allocate video packet buffer");
            goto error_2;
        }
    }
    if(audio_index >= 0) {
        audio_buf = Kit_CreatePacketBuffer(
            config->audio.packet_buffer_size,
            (buf_obj_alloc)av_packet_alloc,
            (buf_obj_unref)av_packet_unref,
            (buf_obj_free)av_packet_free,
            (buf_obj_move)av_packet_move_ref,
            (buf_obj_ref)av_packet_ref
        );
        if(audio_buf == NULL) {
            Kit_SetError("Unable to allocate audio packet buffer");
            goto error_3;
        }
    }
    if(subtitle_index >= 0) {
        subtitle_buf = Kit_CreatePacketBuffer(
            config->subtitle.packet_buffer_size,
            (buf_obj_alloc)av_packet_alloc,
            (buf_obj_unref)av_packet_unref,
            (buf_obj_free)av_packet_free,
            (buf_obj_move)av_packet_move_ref,
            (buf_obj_ref)av_packet_ref
        );
        if(subtitle_buf == NULL) {
            Kit_SetError("Unable to allocate subtitle packet buffer");
            goto error_4;
        }
    }

    demuxer->src = src;
    demuxer->scratch_packet = scratch_packet;
    demuxer->read_attempts = config->demuxer.read_attempts;
    demuxer->read_retry_delay = config->demuxer.read_retry_delay;
    demuxer->buffers[KIT_VIDEO_INDEX] = video_buf;
    demuxer->buffers[KIT_AUDIO_INDEX] = audio_buf;
    demuxer->buffers[KIT_SUBTITLE_INDEX] = subtitle_buf;
    SDL_AtomicSet(&demuxer->stream_indexes[KIT_VIDEO_INDEX], video_index);
    SDL_AtomicSet(&demuxer->stream_indexes[KIT_AUDIO_INDEX], audio_index);
    SDL_AtomicSet(&demuxer->stream_indexes[KIT_SUBTITLE_INDEX], subtitle_index);
    return demuxer;

error_4:
    Kit_FreePacketBuffer(&audio_buf);
error_3:
    Kit_FreePacketBuffer(&video_buf);
error_2:
    av_packet_free(&scratch_packet);
error_1:
    free(demuxer);
error_0:
    return NULL;
}

void Kit_ClearDemuxerBuffers(Kit_Demuxer *demuxer) {
    if(!demuxer)
        return;
    SDL_AtomicSet(&demuxer->abort_requested, 0);
    for(int i = 0; i < KIT_INDEX_COUNT; i++)
        Kit_FlushPacketBuffer(demuxer->buffers[i]);
}

void Kit_SetDemuxerStreamIndex(Kit_Demuxer *demuxer, Kit_BufferIndex index, int stream_index) {
    Kit_FlushPacketBuffer(demuxer->buffers[index]);
    SDL_AtomicSet(&demuxer->stream_indexes[index], stream_index);
}

void Kit_AbortDemuxer(Kit_Demuxer *demuxer) {
    if(!demuxer)
        return;
    SDL_AtomicSet(&demuxer->abort_requested, 1);
    for(int i = 0; i < KIT_INDEX_COUNT; i++)
        Kit_AbortPacketBuffer(demuxer->buffers[i]);
}

Kit_PacketBuffer *Kit_GetDemuxerPacketBuffer(const Kit_Demuxer *demuxer, Kit_BufferIndex buffer_index) {
    assert(demuxer);
    return demuxer->buffers[buffer_index];
}

void Kit_CloseDemuxer(Kit_Demuxer **ref) {
    if(!ref || !*ref)
        return;

    Kit_Demuxer *demuxer = *ref;
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_FreePacketBuffer(&demuxer->buffers[i]);
        SDL_AtomicSet(&demuxer->stream_indexes[i], -1);
    }
    av_packet_free(&demuxer->scratch_packet);
    free(demuxer);
    *ref = NULL;
}

static void Kit_SendSeekPacket(Kit_Demuxer *demuxer, unsigned int seek_serial) {
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        if(!demuxer->buffers[i])
            continue;
        demuxer->scratch_packet->opaque = Kit_CreatePacketTag(KIT_PACKET_TYPE_SEEK, seek_serial);
        Kit_FlushPacketBuffer(demuxer->buffers[i]);
        Kit_WritePacketBuffer(demuxer->buffers[i], demuxer->scratch_packet);
    }
}

bool Kit_DemuxerSeek(Kit_Demuxer *demuxer, Kit_Timer *timer, const int64_t seek_target) {
    const int ret = KIT_FAULT_WRAP_CODE(
        "demux_seek", avformat_seek_file(demuxer->src->format_ctx, -1, INT64_MIN, seek_target, INT64_MAX, 0)
    );
    if(ret >= 0) {
        Kit_ClearDemuxerBuffers(demuxer);
        Kit_SendSeekPacket(demuxer, Kit_IncreaseTimerSerial(timer));
        return true;
    }
    return false;
}

void Kit_GetDemuxerBufferState(
    const Kit_Demuxer *demuxer, Kit_BufferIndex buffer_index, unsigned int *length, unsigned int *capacity
) {
    Kit_PacketBuffer *buffer;
    if(!demuxer || !(buffer = demuxer->buffers[buffer_index]))
        return;
    if(length != NULL)
        *length = Kit_GetPacketBufferLength(buffer);
    if(capacity != NULL)
        *capacity = Kit_GetPacketBufferCapacity(buffer);
}