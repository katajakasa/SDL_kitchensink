#include <assert.h>

#include <libavformat/avformat.h>
#include <SDL_timer.h>

#include "kitchensink/kiterror.h"
#include "kitchensink/internal/kitpacketbuffer.h"
#include "kitchensink/internal/kitdemuxer.h"


bool Kit_RunDemuxer(Kit_Demuxer *demuxer) {
    if(av_read_frame(demuxer->src->format_ctx, demuxer->scratch_packet) < 0) {
        return false;
    }

    // Figure out if we are interested in this stream. If we are, write the packet to a buffer for decoder to pick up.
    // Note that Kit_WritePacketBuffer() may block if the buffer is full. It will also move the scratch_packet
    // references to its own buffer, leaving the scratch_buffer in a clean state.
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        if (demuxer->scratch_packet->stream_index == demuxer->stream_indexes[i]) {
            Kit_WritePacketBuffer(demuxer->buffers[i], demuxer->scratch_packet);
            return true;
        }
    }

    // Packet does not belong to any stream we are interested in, so get rid of it.
    av_packet_unref(demuxer->scratch_packet);
    return true;
}

Kit_Demuxer* Kit_CreateDemuxer(
    const Kit_Source *src,
    int video_index,
    int audio_index,
    int subtitle_index
) {
    Kit_Demuxer *demuxer = NULL;
    Kit_PacketBuffer *video_buf = NULL;
    Kit_PacketBuffer *audio_buf = NULL;
    Kit_PacketBuffer *subtitle_buf = NULL;
    AVPacket *scratch_packet;

    if ((demuxer = calloc(1, sizeof(Kit_Demuxer))) == NULL) {
        Kit_SetError("Unable to allocate demuxer");
        goto error_0;
    }
    if ((scratch_packet = av_packet_alloc()) == NULL) {
        goto error_1;
    }
    if (video_index >= 0) {
        video_buf = Kit_CreatePacketBuffer(
            3,
            (buf_obj_alloc) av_packet_alloc,
            (buf_obj_unref) av_packet_unref,
            (buf_obj_free) av_packet_free,
            (buf_obj_move) av_packet_move_ref
        );
        if(video_buf == NULL) {
            Kit_SetError("Unable to allocate video packet buffer");
            goto error_2;
        }
    }
    if (audio_index >= 0) {
        audio_buf = Kit_CreatePacketBuffer(
            32,
            (buf_obj_alloc) av_packet_alloc,
            (buf_obj_unref) av_packet_unref,
            (buf_obj_free) av_packet_free,
            (buf_obj_move) av_packet_move_ref
        );
        if(audio_buf == NULL) {
            Kit_SetError("Unable to allocate audio packet buffer");
            goto error_3;
        }
    }
    if (subtitle_index >= 0) {
        subtitle_buf = Kit_CreatePacketBuffer(
            32,
            (buf_obj_alloc) av_packet_alloc,
            (buf_obj_unref) av_packet_unref,
            (buf_obj_free) av_packet_free,
            (buf_obj_move) av_packet_move_ref
        );
        if(subtitle_buf == NULL) {
            Kit_SetError("Unable to allocate subtitle packet buffer");
            goto error_4;
        }
    }

    demuxer->src = src;
    demuxer->scratch_packet = scratch_packet;
    demuxer->buffers[KIT_VIDEO_INDEX] = video_buf;
    demuxer->buffers[KIT_AUDIO_INDEX] = audio_buf;
    demuxer->buffers[KIT_SUBTITLE_INDEX] = subtitle_buf;
    demuxer->stream_indexes[KIT_VIDEO_INDEX] = video_index;
    demuxer->stream_indexes[KIT_AUDIO_INDEX] = audio_index;
    demuxer->stream_indexes[KIT_SUBTITLE_INDEX] = subtitle_index;
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

void Kit_ClearDemuxerBuffers(const Kit_Demuxer *demuxer) {
    if (!demuxer)
        return;
    for (int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_FlushPacketBuffer(demuxer->buffers[i]);
    }
}

Kit_PacketBuffer* Kit_GetDemuxerPacketBuffer(const Kit_Demuxer *demuxer, KitBufferIndex buffer_index) {
    assert(demuxer);
    return demuxer->buffers[buffer_index];
}

void Kit_CloseDemuxer(Kit_Demuxer **ref) {
    if (!ref || !*ref)
        return;

    Kit_Demuxer *demuxer = *ref;
    for(int i = 0; i < KIT_INDEX_COUNT; i++) {
        Kit_FreePacketBuffer(&demuxer->buffers[i]);
        demuxer->stream_indexes[i] = -1;
    }
    av_packet_free(&demuxer->scratch_packet);
    free(demuxer);
    *ref = NULL;
}