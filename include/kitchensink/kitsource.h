#ifndef KITSOURCE_H
#define KITSOURCE_H

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
    void *vcodec_ctx;
    void *acodec_ctx;
    void *vcodec;
    void *acodec;
} Kit_Source;

typedef struct Kit_Stream {
    int index;
    Kit_streamtype type;
    int width;
    int height;
} Kit_StreamInfo;

Kit_Source* Kit_CreateSourceFromUrl(const char *path);
int Kit_InitSourceCodecs(Kit_Source *src);
void Kit_CloseSource(Kit_Source *src);

int Kit_GetSourceStreamInfo(const Kit_Source *src, Kit_StreamInfo *info, int index);
int Kit_GetSourceStreamCount(const Kit_Source *src);
int Kit_GetBestSourceStream(const Kit_Source *src, const Kit_streamtype type);
int Kit_SetSourceStream(Kit_Source *src, const Kit_streamtype type, int index);
int Kit_GetSourceStream(const Kit_Source *src, const Kit_streamtype type);

#ifdef __cplusplus
}
#endif

#endif // KITSOURCE_H
