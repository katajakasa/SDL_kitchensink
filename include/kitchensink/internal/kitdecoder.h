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
#include "kitpacketbuffer.h"

typedef struct Kit_Decoder Kit_Decoder;

typedef bool (*dec_input_cb)(const Kit_Decoder *decoder, const AVPacket *packet);
typedef bool (*dec_decode_cb)(const Kit_Decoder *decoder);
typedef void (*dec_flush_cb)(Kit_Decoder *decoder);
typedef void (*dec_signal_cb)(Kit_Decoder *decoder);
typedef void (*dec_close_cb)(Kit_Decoder *decoder);

struct Kit_Decoder {
    double clock_sync;           ///< Sync source for current stream
    double clock_pos;            ///< Current pts for the stream
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

KIT_LOCAL void Kit_SetDecoderClockSync(Kit_Decoder *decoder, double sync);
KIT_LOCAL void Kit_ChangeDecoderClockSync(Kit_Decoder *decoder, double sync);
KIT_LOCAL double Kit_GetDecoderPTS(const Kit_Decoder *decoder);

KIT_LOCAL bool Kit_RunDecoder(const Kit_Decoder *decoder);
KIT_LOCAL bool Kit_AddDecoderPacket(const Kit_Decoder *decoder, const AVPacket *packet);
KIT_LOCAL void Kit_ClearDecoderBuffers(Kit_Decoder *decoder);
KIT_LOCAL void Kit_SignalDecoder(Kit_Decoder *decoder);

#endif // KITDECODER_H
