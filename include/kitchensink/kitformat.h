#ifndef KITFORMAT_H
#define KITFORMAT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Kit_OutputFormat {
    unsigned int format; ///< SDL Format
    bool is_signed;      ///< Signedness (if audio)
    int bytes;           ///< Bytes per sample per channel (if audio)
    int samplerate;      ///< Sampling rate (if audio)
    int channels;        ///< Channels (if audio)
    int width;           ///< Width in pixels (if video)
    int height;          ///< Height in pixels (if video)
} Kit_OutputFormat;

#ifdef __cplusplus
}
#endif

#endif // KITFORMAT_H
