#ifndef KITSOURCE_H
#define KITSOURCE_H

#include "kitchensink/kitconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KIT_CODECNAMESIZE 32
#define KIT_CODECLONGNAMESIZE 128

typedef enum Kit_streamtype {
    KIT_STREAMTYPE_UNKNOWN,
    KIT_STREAMTYPE_VIDEO,
    KIT_STREAMTYPE_AUDIO,
    KIT_STREAMTYPE_DATA,
    KIT_STREAMTYPE_SUBTITLE,
    KIT_STREAMTYPE_ATTACHMENT
} Kit_streamtype;

typedef struct Kit_Source {
    int astream_idx;
    int vstream_idx;
    void *format_ctx;
} Kit_Source;

typedef struct Kit_Stream {
    int index;
    Kit_streamtype type;
} Kit_StreamInfo;

KIT_API Kit_Source* Kit_CreateSourceFromUrl(const char *path);
KIT_API void Kit_CloseSource(Kit_Source *src);

KIT_API int Kit_GetSourceStreamInfo(const Kit_Source *src, Kit_StreamInfo *info, int index);
KIT_API int Kit_GetSourceStreamCount(const Kit_Source *src);
KIT_API int Kit_GetBestSourceStream(const Kit_Source *src, const Kit_streamtype type);
KIT_API int Kit_SetSourceStream(Kit_Source *src, const Kit_streamtype type, int index);
KIT_API int Kit_GetSourceStream(const Kit_Source *src, const Kit_streamtype type);

#ifdef __cplusplus
}
#endif

#endif // KITSOURCE_H
