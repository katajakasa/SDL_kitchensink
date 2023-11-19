#include <libavutil/avstring.h>
#include <libavutil/time.h>

#include "kitchensink/internal/utils/kithelpers.h"

static const char *const font_mime[] = {
    "application/x-font-ttf",
    "application/x-font-truetype",
    "application/x-truetype-font",
    "application/x-font-opentype",
    "application/vnd.ms-opentype",
    "application/font-sfnt",
    NULL};

double Kit_GetSystemTime() {
    return (double)av_gettime() / 1000000.0;
}

bool Kit_StreamIsFontAttachment(const AVStream *stream) {
    if(stream->codecpar->codec_type != AVMEDIA_TYPE_ATTACHMENT)
        return false;
    const AVDictionaryEntry *tag = av_dict_get(stream->metadata, "mimetype", NULL, AV_DICT_MATCH_CASE);
    if(tag) {
        for(int n = 0; font_mime[n]; n++) {
            if(av_strcasecmp(font_mime[n], tag->value) == 0) {
                return true;
            }
        }
    }
    return false;
}

int min2(int a, int b) {
    return a < b ? a : b;
}