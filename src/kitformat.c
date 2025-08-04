#include "kitchensink/kitformat.h"

#include <SDL_pixels.h>

void Kit_ResetVideoFormatRequest(Kit_VideoFormatRequest *request) {
    request->hw_device_types = KIT_HWDEVICE_TYPE_ALL;
    request->format = SDL_PIXELFORMAT_UNKNOWN;
    request->width = -1;
    request->height = -1;
}

void Kit_ResetAudioFormatRequest(Kit_AudioFormatRequest *request) {
    request->format = 0;
    request->is_signed = -1;
    request->bytes = -1;
    request->channels = -1;
    request->sample_rate = -1;
}
