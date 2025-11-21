#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libavformat/avformat.h>
#include <libavutil/opt.h>

#include "kitchensink2/internal/utils/kitlog.h"
#include "kitchensink2/kiterror.h"
#include "kitchensink2/kitsource.h"

#define AVIO_BUF_SIZE 32768

static int _ScanSource(AVFormatContext *format_ctx) {
    av_opt_set_int(format_ctx, "probesize", INT_MAX, 0);
    av_opt_set_int(format_ctx, "analyzeduration", INT_MAX, 0);
    if(avformat_find_stream_info(format_ctx, NULL) < 0) {
        Kit_SetError("Unable to fetch source information");
        return 1;
    }
    return 0;
}

Kit_Source *Kit_CreateSourceFromUrl(const char *url) {
    assert(url != NULL);

    Kit_Source *src = calloc(1, sizeof(Kit_Source));
    if(src == NULL) {
        Kit_SetError("Unable to allocate source");
        return NULL;
    }

    // Attempt to open source
    if(avformat_open_input((AVFormatContext **)&src->format_ctx, url, NULL, NULL) < 0) {
        Kit_SetError("Unable to open source Url");
        goto EXIT_0;
    }

    // Scan source information (may seek forwards)
    if(_ScanSource(src->format_ctx)) {
        goto EXIT_1;
    }

    return src;

EXIT_1:
    avformat_close_input((AVFormatContext **)&src->format_ctx);
EXIT_0:
    free(src);
    return NULL;
}

Kit_Source *Kit_CreateSourceFromCustom(Kit_ReadCallback read_cb, Kit_SeekCallback seek_cb, void *userdata) {
    assert(read_cb != NULL);

    Kit_Source *src = calloc(1, sizeof(Kit_Source));
    if(src == NULL) {
        Kit_SetError("Unable to allocate source");
        return NULL;
    }

    uint8_t *avio_buf = av_malloc(AVIO_BUF_SIZE);
    if(avio_buf == NULL) {
        Kit_SetError("Unable to allocate avio buffer");
        goto EXIT_0;
    }

    AVFormatContext *format_ctx = avformat_alloc_context();
    if(format_ctx == NULL) {
        Kit_SetError("Unable to allocate format context");
        goto EXIT_1;
    }

    AVIOContext *avio_ctx = avio_alloc_context(avio_buf, AVIO_BUF_SIZE, 0, userdata, read_cb, 0, seek_cb);
    if(avio_ctx == NULL) {
        Kit_SetError("Unable to allocate avio context");
        goto EXIT_2;
    }
    // avio_alloc_context takes ownership of avio_buf, so don't free it separately after this point

    // Set the format as AVIO format
    format_ctx->pb = avio_ctx;

    // Attempt to open source
    if(avformat_open_input(&format_ctx, "", NULL, NULL) < 0) {
        Kit_SetError("Unable to open custom source");
        goto EXIT_3;
    }

    // Scan source information (may seek forwards)
    if(_ScanSource(format_ctx)) {
        goto EXIT_4;
    }

    // Set internals
    src->format_ctx = format_ctx;
    src->avio_ctx = avio_ctx;
    return src;

EXIT_4:
    // avformat_close_input frees format_ctx and its pb (avio_ctx), but not avio_buf
    avformat_close_input(&format_ctx);
    av_freep(&avio_ctx->buffer);
    av_freep(&avio_ctx);
    goto EXIT_0;
EXIT_3:
    // avio_ctx owns avio_buf, so free avio_ctx which will not free the buffer automatically
    av_freep(&avio_ctx->buffer);
    av_freep(&avio_ctx);
EXIT_2:
    avformat_free_context(format_ctx);
    goto EXIT_0;
EXIT_1:
    av_freep(&avio_buf);
EXIT_0:
    free(src);
    return NULL;
}

static int _RWReadCallback(void *userdata, uint8_t *buf, int size) {
    size_t bytes_read = SDL_RWread((SDL_RWops *)userdata, buf, 1, size);
    return bytes_read == 0 ? AVERROR_EOF : bytes_read;
}

static int64_t _RWGetSize(SDL_RWops *rw_ops) {
    // First, see if tell works at all, and fail with -1 if it doesn't.
    const int64_t current_pos = SDL_RWtell(rw_ops);
    if(current_pos < 0) {
        return -1;
    }

    // Seek to end, get pos (this is the size), then return.
    if(SDL_RWseek(rw_ops, 0, RW_SEEK_END) < 0) {
        return -1; // Seek failed, never mind then
    }
    const int64_t max_pos = SDL_RWtell(rw_ops);
    SDL_RWseek(rw_ops, current_pos, RW_SEEK_SET);
    return max_pos;
}

static int64_t _RWSeekCallback(void *userdata, int64_t offset, int whence) {
    int rw_whence = 0;
    if(whence & AVSEEK_SIZE)
        return _RWGetSize(userdata);

    if((whence & ~AVSEEK_FORCE) == SEEK_CUR)
        rw_whence = RW_SEEK_CUR;
    else if((whence & ~AVSEEK_FORCE) == SEEK_SET)
        rw_whence = RW_SEEK_SET;
    else if((whence & ~AVSEEK_FORCE) == SEEK_END)
        rw_whence = RW_SEEK_END;

    return SDL_RWseek((SDL_RWops *)userdata, offset, rw_whence);
}

Kit_Source *Kit_CreateSourceFromRW(SDL_RWops *rw_ops) {
    return Kit_CreateSourceFromCustom(_RWReadCallback, _RWSeekCallback, rw_ops);
}

void Kit_CloseSource(Kit_Source *src) {
    assert(src != NULL);
    AVFormatContext *format_ctx = src->format_ctx;
    AVIOContext *avio_ctx = src->avio_ctx;
    avformat_close_input(&format_ctx);
    if(avio_ctx) {
        av_freep(&avio_ctx->buffer);
        av_freep(&avio_ctx);
    }
    free(src);
}

static Kit_StreamType Kit_GetKitStreamType(const enum AVMediaType type) {
    switch(type) {
        case AVMEDIA_TYPE_DATA:
            return KIT_STREAMTYPE_DATA;
        case AVMEDIA_TYPE_VIDEO:
            return KIT_STREAMTYPE_VIDEO;
        case AVMEDIA_TYPE_AUDIO:
            return KIT_STREAMTYPE_AUDIO;
        case AVMEDIA_TYPE_SUBTITLE:
            return KIT_STREAMTYPE_SUBTITLE;
        case AVMEDIA_TYPE_ATTACHMENT:
            return KIT_STREAMTYPE_ATTACHMENT;
        default:
            return KIT_STREAMTYPE_UNKNOWN;
    }
}

int Kit_GetSourceStreamInfo(const Kit_Source *src, Kit_SourceStreamInfo *info, int index) {
    assert(src != NULL);
    assert(info != NULL);

    const AVFormatContext *format_ctx = (AVFormatContext *)src->format_ctx;
    if(index < 0 || index >= format_ctx->nb_streams) {
        Kit_SetError("Invalid stream index");
        return 1;
    }

    const AVStream *stream = format_ctx->streams[index];
    info->type = Kit_GetKitStreamType(stream->codecpar->codec_type);
    info->index = index;
    return 0;
}

static bool Kit_IsSubtitleSupported(const enum AVCodecID type) {
    switch(type) {
        case AV_CODEC_ID_TEXT:
        case AV_CODEC_ID_HDMV_TEXT_SUBTITLE:
        case AV_CODEC_ID_SRT:
        case AV_CODEC_ID_SUBRIP:
        case AV_CODEC_ID_SSA:
        case AV_CODEC_ID_ASS:
            return true; // Text types
        case AV_CODEC_ID_DVD_SUBTITLE:
        case AV_CODEC_ID_DVB_SUBTITLE:
        case AV_CODEC_ID_HDMV_PGS_SUBTITLE:
            return true; // Image types
        default:
            return false;
    }
}

static int Kit_GetBestSubtitleStream(const Kit_Source *src) {
    AVFormatContext *format_ctx = src->format_ctx;
    for(int i = 0; i < format_ctx->nb_streams; i++) {
        const AVStream *stream = format_ctx->streams[i];
        if(stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE &&
           Kit_IsSubtitleSupported(stream->codecpar->codec_id)) {
            return i;
        }
    }
    return -1;
}

int Kit_GetBestSourceStream(const Kit_Source *src, const Kit_StreamType type) {
    assert(src != NULL);
    int avmedia_type = 0;
    switch(type) {
        case KIT_STREAMTYPE_VIDEO:
            avmedia_type = AVMEDIA_TYPE_VIDEO;
            break;
        case KIT_STREAMTYPE_AUDIO:
            avmedia_type = AVMEDIA_TYPE_AUDIO;
            break;
        case KIT_STREAMTYPE_SUBTITLE:
            return Kit_GetBestSubtitleStream(src);
        default:
            return -1;
    }
    int ret = av_find_best_stream((AVFormatContext *)src->format_ctx, avmedia_type, -1, -1, NULL, 0);
    if(ret == AVERROR_STREAM_NOT_FOUND) {
        return -1;
    }
    if(ret == AVERROR_DECODER_NOT_FOUND) {
        Kit_SetError("Unable to find a decoder for the stream");
        return 1;
    }
    return ret;
}

int Kit_GetSourceStreamCount(const Kit_Source *src) {
    assert(src != NULL);
    return ((AVFormatContext *)src->format_ctx)->nb_streams;
}

double Kit_GetSourceDuration(const Kit_Source *src) {
    assert(src != NULL);
    return (((AVFormatContext *)src->format_ctx)->duration / AV_TIME_BASE);
}

int Kit_GetSourceStreamList(const Kit_Source *src, const Kit_StreamType type, int *list, int size) {
    int p = 0;
    AVFormatContext *format_ctx = (AVFormatContext *)src->format_ctx;
    Kit_StreamType found;

    for(int n = 0; n < Kit_GetSourceStreamCount(src); n++) {
        found = Kit_GetKitStreamType(format_ctx->streams[n]->codecpar->codec_type);
        if(found == type) {
            if(p >= size)
                return p;
            list[p++] = n;
        }
    }
    return p;
}

int Kit_GetNextSourceStream(const Kit_Source *src, const Kit_StreamType type, int current_index, int loop) {
    AVFormatContext *format_ctx = (AVFormatContext *)src->format_ctx;

    for(int n = current_index + 1; n < Kit_GetSourceStreamCount(src); n++)
        if(Kit_GetKitStreamType(format_ctx->streams[n]->codecpar->codec_type) == type)
            return n;

    if(!loop)
        return -1;

    for(int n = 0; n < current_index; n++) {
        if(Kit_GetKitStreamType(format_ctx->streams[n]->codecpar->codec_type) == type)
            return n;
    }

    return -1;
}