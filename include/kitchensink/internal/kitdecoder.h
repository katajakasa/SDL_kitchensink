#ifndef KITDECODER_H
#define KITDECODER_H

#include <stdbool.h>

#include <SDL_mutex.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "kitchensink/kitformat.h"
#include "kitchensink/kitcodec.h"
#include "kitchensink/kitconfig.h"
#include "kitchensink/kitsource.h"
#include "kitchensink/internal/utils/kitbuffer.h"

enum {
    KIT_DEC_BUF_IN = 0,
    KIT_DEC_BUF_OUT,
    KIT_DEC_BUF_COUNT
};

typedef struct Kit_Decoder Kit_Decoder;

typedef int (*dec_decode_cb)(Kit_Decoder *dec, AVPacket *in_packet);
typedef void (*dec_close_cb)(Kit_Decoder *dec);
typedef void (*dec_free_packet_cb)(void *packet);

struct Kit_Decoder {
    int stream_index;            ///< Source stream index for the current stream
    double clock_sync;           ///< Sync source for current stream
    double clock_pos;            ///< Current pts for the stream
    AVRational aspect_ratio;     ///< Aspect ratio for the current frame (may change frome-to-frame)
    Kit_OutputFormat output;     ///< Output format for the decoder

    AVCodecContext *codec_ctx;   ///< FFMpeg internal: Codec context
    AVFormatContext *format_ctx; ///< FFMpeg internal: Format context (owner: Kit_Source)

    SDL_mutex *output_lock;      ///< Threading lock for output buffer
    Kit_Buffer *buffer[2];       ///< Buffers for incoming and decoded packets

    void *userdata;              ///< Decoder specific information (Audio, video, subtitle context)
    dec_decode_cb dec_decode;    ///< Decoder decoding function callback
    dec_close_cb dec_close;      ///< Decoder close function callback
};

KIT_LOCAL Kit_Decoder* Kit_CreateDecoder(const Kit_Source *src, int stream_index,
                                         int out_b_size, dec_free_packet_cb free_out_cb,
                                         int thread_count);
KIT_LOCAL void Kit_CloseDecoder(Kit_Decoder *dec);

KIT_LOCAL int Kit_GetDecoderStreamIndex(const Kit_Decoder *dec);
KIT_LOCAL int Kit_GetDecoderCodecInfo(const Kit_Decoder *dec, Kit_Codec *codec);
KIT_LOCAL int Kit_GetDecoderOutputFormat(const Kit_Decoder *dec, Kit_OutputFormat *output);

KIT_LOCAL void Kit_SetDecoderClockSync(Kit_Decoder *dec, double sync);
KIT_LOCAL void Kit_ChangeDecoderClockSync(Kit_Decoder *dec, double sync);

KIT_LOCAL int Kit_RunDecoder(Kit_Decoder *dec);
KIT_LOCAL void Kit_ClearDecoderBuffers(const Kit_Decoder *dec);

KIT_LOCAL bool Kit_CanWriteDecoderInput(const Kit_Decoder *dec);
KIT_LOCAL int Kit_WriteDecoderInput(const Kit_Decoder *dec, AVPacket *packet);
KIT_LOCAL AVPacket* Kit_ReadDecoderInput(const Kit_Decoder *dec);
KIT_LOCAL void Kit_ClearDecoderInput(const Kit_Decoder *dec);
KIT_LOCAL AVPacket* Kit_PeekDecoderInput(const Kit_Decoder *dec);
KIT_LOCAL void Kit_AdvanceDecoderInput(const Kit_Decoder *dec);

KIT_LOCAL int Kit_WriteDecoderOutput(const Kit_Decoder *dec, void *packet);
KIT_LOCAL bool Kit_CanWriteDecoderOutput(const Kit_Decoder *dec);
KIT_LOCAL void* Kit_PeekDecoderOutput(const Kit_Decoder *dec);
KIT_LOCAL void* Kit_ReadDecoderOutput(const Kit_Decoder *dec);
KIT_LOCAL void Kit_ClearDecoderOutput(const Kit_Decoder *dec);
KIT_LOCAL void Kit_AdvanceDecoderOutput(const Kit_Decoder *dec);
KIT_LOCAL void Kit_ForEachDecoderOutput(const Kit_Decoder *dec, Kit_ForEachItemCallback foreach_cb, void *userdata);
KIT_LOCAL unsigned int Kit_GetDecoderOutputLength(const Kit_Decoder *dec);

KIT_LOCAL int Kit_LockDecoderOutput(const Kit_Decoder *dec);
KIT_LOCAL void Kit_UnlockDecoderOutput(const Kit_Decoder *dec);

#endif // KITDECODER_H
