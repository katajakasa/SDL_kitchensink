#include "kitchensink/kitsource.h"
#include "kitchensink/kiterror.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

Kit_Source* Kit_CreateSourceFromUrl(const char *url) {
	assert(url != NULL);

	AVFormatContext *format_ctx = NULL;

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

void Kit_CloseSource(Kit_Source *src) {
	assert(src != NULL);
	avformat_close_input((AVFormatContext **)&src->format_ctx);
	free(src);
}

int Kit_GetSourceStreamInfo(const Kit_Source *src, Kit_StreamInfo *info, int index) {
	assert(src != NULL);
	assert(info != NULL);

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
	return 0;
}

int Kit_GetBestSourceStream(const Kit_Source *src, const Kit_streamtype type) {
	assert(src != NULL);
	int avmedia_type = 0;
	switch(type) {
		case KIT_STREAMTYPE_VIDEO: avmedia_type = AVMEDIA_TYPE_VIDEO; break;
		case KIT_STREAMTYPE_AUDIO: avmedia_type = AVMEDIA_TYPE_AUDIO; break;
		default: return -1;
	}
	return av_find_best_stream((AVFormatContext *)src->format_ctx, avmedia_type, -1, -1, NULL, 0);
}

int Kit_SetSourceStream(Kit_Source *src, const Kit_streamtype type, int index) {
	assert(src != NULL);
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
	assert(src != NULL);
	switch(type) {
		case KIT_STREAMTYPE_AUDIO: return src->astream_idx;
		case KIT_STREAMTYPE_VIDEO: return src->vstream_idx;
		default:
			break;
	}
	return -1;
}

int Kit_GetSourceStreamCount(const Kit_Source *src) {
	assert(src != NULL);
	return ((AVFormatContext *)src->format_ctx)->nb_streams;
}
