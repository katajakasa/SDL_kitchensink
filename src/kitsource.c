#include "kitchensink/kitsource.h"
#include "kitchensink/kiterror.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <stdlib.h>
#include <string.h>

Kit_Source* Kit_CreateSourceFromUrl(const char *url) {
	AVFormatContext *format_ctx = NULL;

	if(url == NULL) {
		Kit_SetError("Source URL must not be NULL");
		return NULL;
	}

	// Attempt to open source
	if(avformat_open_input(&format_ctx, url, NULL, NULL) < 0) {
		Kit_SetError("Unable to open source Url");
		goto exit_0;
	}

	// Fetch stream information. This may potentially take a while.
	if(avformat_find_stream_info(format_ctx, NULL) < 0) {
		Kit_SetError("Unable to fetch source information");
		goto exit_1;
	}

	// Find best streams for defaults
	int best_astream_idx = av_find_best_stream(format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	int best_vstream_idx = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

	// Allocate and return a new source
	// TODO: Check allocation errors
	Kit_Source *out = calloc(1, sizeof(Kit_Source));
	out->format_ctx = format_ctx;
	out->astream_idx = best_astream_idx;
	out->vstream_idx = best_vstream_idx;
	return out;

exit_1:
	avformat_close_input(&format_ctx);
exit_0:
	return NULL;
}

int Kit_InitSourceCodecs(Kit_Source *src) {
	AVCodecContext *acodec_ctx = NULL;
	AVCodecContext *vcodec_ctx = NULL;
	AVCodec *acodec = NULL;
	AVCodec *vcodec = NULL;

	if(src == NULL) {
		Kit_SetError("Source must not be NULL");
		return 1;
	}
	if(src->acodec_ctx || src->vcodec_ctx) {
		Kit_SetError("Source codecs already initialized");
		return 1;
	}

	// Make sure indexes seem correct
	AVFormatContext *format_ctx = (AVFormatContext *)src->format_ctx;
	if(src->astream_idx < 0 || src->astream_idx >= format_ctx->nb_streams) {
		Kit_SetError("Invalid audio stream index");
		return 1;
	}
	if(src->vstream_idx < 0 || src->vstream_idx >= format_ctx->nb_streams) {
		Kit_SetError("Invalid video stream index");
		return 1;
	}

	// Find video decoder
	vcodec = avcodec_find_decoder(format_ctx->streams[src->vstream_idx]->codec->codec_id);
	if(!vcodec) {
		Kit_SetError("No suitable video decoder found");
		goto exit_0;
	}

	// Copy the original video codec context
	vcodec_ctx = avcodec_alloc_context3(vcodec);
	if(avcodec_copy_context(vcodec_ctx, format_ctx->streams[src->vstream_idx]->codec) != 0) {
		Kit_SetError("Unable to copy video codec context");
	 	goto exit_0;
	}

	// Create a video decoder context
	if(avcodec_open2(vcodec_ctx, vcodec, NULL) < 0) {
		Kit_SetError("Unable to allocate video codec context");
		goto exit_1;
	}

	// Find audio decoder
	acodec = avcodec_find_decoder(format_ctx->streams[src->astream_idx]->codec->codec_id);
	if(!acodec) {
		Kit_SetError("No suitable audio decoder found");
		goto exit_2;
	}

	// Copy the original audio codec context
	acodec_ctx = avcodec_alloc_context3(acodec);
	if(avcodec_copy_context(acodec_ctx, format_ctx->streams[src->astream_idx]->codec) != 0) {
		Kit_SetError("Unable to copy audio codec context");
	 	goto exit_2;
	}

	// Create an audio decoder context
	if(avcodec_open2(acodec_ctx, acodec, NULL) < 0) {
		Kit_SetError("Unable to allocate audio codec context");
		goto exit_3;
	}

	src->acodec = acodec;
	src->vcodec = vcodec;
	src->acodec_ctx = acodec_ctx;
	src->vcodec_ctx = vcodec_ctx;
	return 0;

exit_3:
	avcodec_free_context(&acodec_ctx);
exit_2:
	avcodec_close(vcodec_ctx);
exit_1:
	avcodec_free_context(&vcodec_ctx);
exit_0:
	return 1;
}

void Kit_CloseSource(Kit_Source *src) {
	if(src == NULL) return;
	avcodec_close((AVCodecContext*)src->acodec_ctx);
	avcodec_close((AVCodecContext*)src->vcodec_ctx);
	avcodec_free_context((AVCodecContext**)&src->acodec_ctx);
	avcodec_free_context((AVCodecContext**)&src->vcodec_ctx);
	avformat_close_input((AVFormatContext **)&src->format_ctx);
	free(src);
}

int Kit_GetSourceStreamInfo(const Kit_Source *src, Kit_StreamInfo *info, int index) {
	if(src == NULL) {
		Kit_SetError("Source must not be NULL");
		return 1;
	}
	AVFormatContext *format_ctx = (AVFormatContext *)src->format_ctx;
	if(index < 0 || index >= format_ctx->nb_streams) {
		Kit_SetError("Invalid stream index");
		return 1;
	}

	AVStream *stream = format_ctx->streams[index];
	switch(stream->codec->codec_type) {
		case AVMEDIA_TYPE_UNKNOWN: info->type = KIT_STREAMTYPE_UNKNOWN; break;
		case AVMEDIA_TYPE_DATA: info->type = KIT_STREAMTYPE_DATA; break;
		case AVMEDIA_TYPE_VIDEO: info->type = KIT_STREAMTYPE_VIDEO; break;
		case AVMEDIA_TYPE_AUDIO: info->type = KIT_STREAMTYPE_AUDIO; break;
		case AVMEDIA_TYPE_SUBTITLE: info->type = KIT_STREAMTYPE_SUBTITLE; break;
		case AVMEDIA_TYPE_ATTACHMENT: info->type = KIT_STREAMTYPE_ATTACHMENT; break;
		default:
			Kit_SetError("Unknown native stream type");
			return 1;
	}

	info->index = index;
	info->width = stream->codec->width;
	info->height = stream->codec->height;
	return 0;
}

int Kit_GetBestSourceStream(const Kit_Source *src, const Kit_streamtype type) {
	if(src == NULL) {
		Kit_SetError("Source must not be NULL");
		return -1;
	}
	int avmedia_type = 0;
	switch(type) {
		case KIT_STREAMTYPE_VIDEO: avmedia_type = AVMEDIA_TYPE_VIDEO; break;
		case KIT_STREAMTYPE_AUDIO: avmedia_type = AVMEDIA_TYPE_AUDIO; break;
		default: return -1;
	}
	return av_find_best_stream((AVFormatContext *)src->format_ctx, avmedia_type, -1, -1, NULL, 0);
}

int Kit_SetSourceStream(Kit_Source *src, const Kit_streamtype type, int index) {
	if(src == NULL) {
		Kit_SetError("Source must not be NULL");
		return 1;
	}
	switch(type) {
		case KIT_STREAMTYPE_AUDIO: src->astream_idx = index; break;
		case KIT_STREAMTYPE_VIDEO: src->vstream_idx = index; break;
		default:
			Kit_SetError("Invalid stream type");
			return 1;
	}
	return 0;
}

int Kit_GetSourceStream(const Kit_Source *src, const Kit_streamtype type) {
	if(src == NULL) {
		Kit_SetError("Source must not be NULL");
		return -1;
	}
	switch(type) {
		case KIT_STREAMTYPE_AUDIO: return src->astream_idx;
		case KIT_STREAMTYPE_VIDEO: return src->vstream_idx;
		default:
			break;
	}
	return -1;
}

int Kit_GetSourceStreamCount(const Kit_Source *src) {
	if(src == NULL) {
		Kit_SetError("Source must not be NULL");
		return -1;
	}
	return ((AVFormatContext *)src->format_ctx)->nb_streams;
}
