#ifndef KITDEMUXER_H
#define KITDEMUXER_H

#include "kitchensink2/internal/kitbufferindex.h"
#include "kitchensink2/internal/kitpacketbuffer.h"
#include "kitchensink2/kitconfig.h"
#include "kitchensink2/kitsource.h"
#include <libavcodec/avcodec.h>
#include <stdbool.h>

typedef struct Kit_Demuxer {
    const Kit_Source *src;
    Kit_PacketBuffer *buffers[KIT_INDEX_COUNT];
    int stream_indexes[KIT_INDEX_COUNT];
    AVPacket *scratch_packet;
} Kit_Demuxer;

KIT_LOCAL Kit_Demuxer *Kit_CreateDemuxer(const Kit_Source *src, int video_index, int audio_index, int subtitle_index);
KIT_LOCAL void Kit_CloseDemuxer(Kit_Demuxer **demuxer);
KIT_LOCAL bool Kit_RunDemuxer(Kit_Demuxer *demuxer);
KIT_LOCAL Kit_PacketBuffer *Kit_GetDemuxerPacketBuffer(const Kit_Demuxer *demuxer, Kit_BufferIndex buffer_index);
KIT_LOCAL void Kit_ClearDemuxerBuffers(const Kit_Demuxer *demuxer);
KIT_LOCAL void Kit_SignalDemuxer(const Kit_Demuxer *demuxer);
KIT_LOCAL bool Kit_DemuxerSeek(Kit_Demuxer *demuxer, int64_t seek_target);
KIT_LOCAL void Kit_SetDemuxerStreamIndex(Kit_Demuxer *demuxer, Kit_BufferIndex index, int stream_index);
KIT_LOCAL void Kit_GetDemuxerBufferState(
    const Kit_Demuxer *demuxer, Kit_BufferIndex buffer_index, unsigned int *length, unsigned int *capacity, size_t *bytes
);

#endif // KITDEMUXER_H
