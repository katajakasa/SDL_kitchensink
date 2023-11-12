#ifndef KITDECODER_H
#define KITDECODER_H

#include <stdbool.h>

#include <SDL_mutex.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "kitchensink/kitcodec.h"
#include "kitchensink/kitconfig.h"
#include "kitchensink/kitformat.h"
#include "kitchensink/kitsource.h"
#include "kitpacketbuffer.h"
#include "kittimer.h"

typedef struct Kit_Decoder Kit_Decoder;

typedef enum Kit_DecoderInputResult {
    KIT_DEC_INPUT_OK = 0,
    KIT_DEC_INPUT_RETRY,
    KIT_DEC_INPUT_EOF,
} Kit_DecoderInputResult;

typedef Kit_DecoderInputResult (*dec_input_cb)(const Kit_Decoder *decoder, const AVPacket *packet);
typedef bool (*dec_decode_cb)(const Kit_Decoder *decoder, double *pts);
typedef void (*dec_flush_cb)(Kit_Decoder *decoder);
typedef void (*dec_signal_cb)(Kit_Decoder *decoder);
typedef void (*dec_close_cb)(Kit_Decoder *decoder);

struct Kit_Decoder {
    Kit_Timer *sync_timer;
    AVRational aspect_ratio;     ///< Aspect ratio for the current frame (may change frame-to-frame)
    AVCodecContext *codec_ctx;   ///< FFMpeg internal: Codec context
    AVStream *stream;            ///< FFMpeg internal: Data stream
    void *userdata;              ///< Decoder specific information (Audio, video, subtitle context)
    dec_input_cb dec_input;      ///< Decoder packet input function callback
    dec_decode_cb dec_decode;    ///< Decoder decoding function callback
    dec_flush_cb dec_flush;      ///< Decoder buffer flusher function callback
    dec_signal_cb dec_signal;    ///< Decoder kill signal handler function callback (This is called before shutdown).
    dec_close_cb dec_close;      ///< Decoder close function callback
};

KIT_LOCAL Kit_Decoder* Kit_CreateDecoder(
    AVStream *stream,
    Kit_Timer *sync_timer,
    int thread_count,
    dec_input_cb dec_input,
    dec_decode_cb dec_decode,
    dec_flush_cb dec_flush,
    dec_signal_cb dec_signal,
    dec_close_cb dec_close,
    void *userdata
);
KIT_LOCAL void Kit_CloseDecoder(Kit_Decoder **dec);

KIT_LOCAL int Kit_GetDecoderStreamIndex(const Kit_Decoder *decoder);
KIT_LOCAL int Kit_GetDecoderCodecInfo(const Kit_Decoder *decoder, Kit_Codec *codec);

KIT_LOCAL bool Kit_RunDecoder(const Kit_Decoder *decoder, double *pts);
KIT_LOCAL Kit_DecoderInputResult Kit_AddDecoderPacket(const Kit_Decoder *decoder, const AVPacket *packet);
KIT_LOCAL void Kit_ClearDecoderBuffers(Kit_Decoder *decoder);
KIT_LOCAL void Kit_SignalDecoder(Kit_Decoder *decoder);

#endif // KITDECODER_H
