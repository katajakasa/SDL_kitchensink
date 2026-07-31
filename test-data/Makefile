# Test fixture generator for the SDL_kitchensink test suite.
#
# The generated files under media/ are committed to git; this Makefile is only
# run when fixtures are added or changed, never by CMake or CI. After running
# it, `git add` the files you meant to change. Encodes run with bitexact flags
# so regeneration is byte-stable on the same ffmpeg build; encoder version
# bumps can still change output bytes even when semantically unchanged.
#
# Usage: make [-j] [FFMPEG=/path/to/ffmpeg] [FONT=/path/to/font.ttf]
#
# Requirements (checked below): ffmpeg CLI with libx264, libx265, libvpx-vp9,
# aac, libmp3lame, libvorbis and libopus encoders, POSIX shell tools, python3
# (for the VobSub generator), and a DejaVu Sans or Liberation Sans TTF font
# (for the font-attachment fixture).

FFMPEG ?= ffmpeg
FF = $(FFMPEG) -y -v error
# Output-side flags (must precede the output file in each rule): suppress
# encoder/muxer version tags and timestamps so regeneration doesn't byte-churn
# semantically unchanged fixtures. Per-stream flags are ignored for absent
# stream types, so one variable serves audio-only and video-only rules alike.
BITEXACT = -fflags +bitexact -flags:v +bitexact -flags:a +bitexact
SRC = src
OUT = media

FONT ?= $(firstword $(wildcard \
    /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
    /usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf \
    /usr/share/fonts/TTF/DejaVuSans.ttf \
    /usr/share/fonts/TTF/LiberationSans-Regular.ttf))

FIXTURES = \
    $(OUT)/video_audio.mp4 \
    $(OUT)/audio_only.m4a \
    $(OUT)/subtitled.mkv \
    $(OUT)/subtitled_full.mkv \
    $(OUT)/subtitled_ass.mkv \
    $(OUT)/subtitled_font.mkv \
    $(OUT)/subtitled_image.mkv \
    $(OUT)/video_only.mp4 \
    $(OUT)/audio_first.mkv \
    $(OUT)/dual_audio.mkv \
    $(OUT)/many_subs.mkv \
    $(OUT)/audio_s16.wav \
    $(OUT)/audio_f32.wav \
    $(OUT)/audio_s32.flac \
    $(OUT)/audio_mp3.mp3 \
    $(OUT)/audio_vorbis.ogg \
    $(OUT)/audio_opus.mka \
    $(OUT)/audio_mono.m4a \
    $(OUT)/audio_5point1.mka \
    $(OUT)/audio_22050.m4a \
    $(OUT)/single_frame.mp4 \
    $(OUT)/short_audio.m4a \
    $(OUT)/video_vp9.webm \
    $(OUT)/video_10bit.mkv \
    $(OUT)/video_mpeg2.ts \
    $(OUT)/video_yuyv422.nut \
    $(OUT)/video_oddsize.nut \
    $(OUT)/video_tiny.mp4 \
    $(OUT)/truncated.mp4 \
    $(OUT)/corrupt_middle.mp4 \
    $(OUT)/garbage.mp4 \
    $(OUT)/empty.mp4 \
    $(OUT)/broken.srt

# Preflight: fail early with a clear message instead of mid-build. Skipped for
# `make clean` so cleaning works on machines without ffmpeg.
ifneq ($(MAKECMDGOALS),clean)
ifeq ($(shell command -v $(FFMPEG) >/dev/null 2>&1 && echo ok),)
$(error ffmpeg CLI not found (FFMPEG=$(FFMPEG)))
endif
MISSING_ENCODERS := $(shell for e in libx264 libx265 libvpx-vp9 aac libmp3lame libvorbis libopus; do \
    $(FFMPEG) -hide_banner -h encoder=$$e 2>/dev/null | grep -q "^Encoder $$e " || echo $$e; done)
ifneq ($(MISSING_ENCODERS),)
$(error $(FFMPEG) is missing required encoders: $(MISSING_ENCODERS))
endif
ifeq ($(FONT),)
$(error No DejaVuSans.ttf/LiberationSans-Regular.ttf found; pass FONT=/path/to/font.ttf)
endif
ifeq ($(shell command -v python3 >/dev/null 2>&1 && echo ok),)
$(error python3 not found (needed for the VobSub generator))
endif
endif

.PHONY: all clean
all: $(FIXTURES)

clean:
	rm -rf $(OUT)

$(OUT):
	mkdir -p $(OUT)

# Basic A/V fixtures.
$(OUT)/video_audio.mp4: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -f lavfi -i sine=frequency=440:duration=2 \
	    -c:v libx264 -pix_fmt yuv420p -c:a aac $(BITEXACT) $@

$(OUT)/audio_only.m4a: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=2 -c:a aac $(BITEXACT) $@

# Video-only: player creation with no audio stream at all.
$(OUT)/video_only.mp4: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -c:v libx264 -pix_fmt yuv420p -an $(BITEXACT) $@

# Audio as stream 0, video as stream 1: catches "video is index 0" assumptions.
$(OUT)/audio_first.mkv: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -f lavfi -i sine=frequency=440:duration=2 \
	    -map 1:a -map 0:v -c:v libx264 -pix_fmt yuv420p -c:a aac $(BITEXACT) $@

# Two audio tracks with different frequencies/channel counts, for stream
# switching. First track forced to stereo (sine is mono by default) so the two
# tracks are actually distinguishable by channel count, not just frequency.
$(OUT)/dual_audio.mkv: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -f lavfi -i sine=frequency=440:duration=2 \
	    -f lavfi -i sine=frequency=880:duration=2 \
	    -map 0:v -map 1:a -map 2:a -c:v libx264 -pix_fmt yuv420p -c:a aac \
	    -filter:a:0 "aformat=channel_layouts=stereo" -ac:a:1 1 $(BITEXACT) $@

# Subtitle fixtures. subtitled.mkv is video+subtitle only; subtitled_full.mkv
# adds an audio track for tests that exercise all three subsystems against one
# player. subtitled_ass.mkv exercises the native ASS path (as opposed to
# subtitled.mkv's SRT-to-ASS conversion path), and subtitled_font.mkv muxes a
# real TTF in as a matroska attachment stream, indirectly exercising
# Kit_StreamIsFontAttachment() and libass attachment loading.
$(OUT)/subtitled.mkv: $(SRC)/subtitles.srt | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -i $(SRC)/subtitles.srt \
	    -c:v libx264 -pix_fmt yuv420p -c:s srt $(BITEXACT) $@

$(OUT)/subtitled_full.mkv: $(SRC)/subtitles.srt | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -f lavfi -i sine=frequency=440:duration=2 \
	    -i $(SRC)/subtitles.srt \
	    -c:v libx264 -pix_fmt yuv420p -c:a aac -c:s srt $(BITEXACT) $@

$(OUT)/subtitled_ass.mkv: $(SRC)/subtitles.ass | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -i $(SRC)/subtitles.ass \
	    -c:v libx264 -pix_fmt yuv420p -c:s ass $(BITEXACT) $@

$(OUT)/subtitled_font.mkv: $(SRC)/subtitles.ass | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -i $(SRC)/subtitles.ass \
	    -c:v libx264 -pix_fmt yuv420p -c:s ass \
	    -attach "$(FONT)" -metadata:s:t:0 mimetype=font/ttf $(BITEXACT) $@

# Bitmap (dvd_subtitle) fixture: exercises the image subtitle renderer
# (kitsubimage.c), which no text-based fixture can reach. ffmpeg refuses
# text->bitmap subtitle transcodes, so a minimal VobSub pair is generated
# by script and muxed in with -c:s copy.
$(OUT)/subtitled_image.mkv: $(SRC)/make_vobsub.py | $(OUT)
	rm -f $(OUT)/tmp_vobsub.idx $(OUT)/tmp_vobsub.sub
	python3 $(SRC)/make_vobsub.py $(OUT)/tmp_vobsub
	$(FF) -f lavfi -i color=c=blue:s=160x120:d=2:r=25 \
	    -i $(OUT)/tmp_vobsub.idx -map 0:v -map 1:s \
	    -c:v libx264 -pix_fmt yuv420p -c:s copy $(BITEXACT) $@
	rm -f $(OUT)/tmp_vobsub.idx $(OUT)/tmp_vobsub.sub

# Three subtitle tracks, for stream list/iteration tests.
$(OUT)/many_subs.mkv: $(SRC)/subtitles.srt | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=2:size=160x120:rate=25 \
	    -i $(SRC)/subtitles.srt -i $(SRC)/subtitles.srt -i $(SRC)/subtitles.srt \
	    -map 0:v -map 1:s -map 2:s -map 3:s \
	    -c:v libx264 -pix_fmt yuv420p -c:s srt $(BITEXACT) $@

# Audio format matrix: one fixture per supported input sample format / channel
# count / sample rate combination, for test_audio_formats.
$(OUT)/audio_s16.wav: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 -c:a pcm_s16le $(BITEXACT) $@

$(OUT)/audio_f32.wav: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 -c:a pcm_f32le $(BITEXACT) $@

$(OUT)/audio_s32.flac: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 -c:a flac -sample_fmt s32 $(BITEXACT) $@

$(OUT)/audio_mp3.mp3: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 -c:a libmp3lame $(BITEXACT) $@

$(OUT)/audio_vorbis.ogg: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 -c:a libvorbis $(BITEXACT) $@

$(OUT)/audio_opus.mka: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 -c:a libopus $(BITEXACT) $@

$(OUT)/audio_mono.m4a: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 -c:a aac -ac 1 $(BITEXACT) $@

$(OUT)/audio_5point1.mka: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 \
	    -filter:a "pan=5.1|FL=c0|FR=c0|FC=c0|LFE=c0|BL=c0|BR=c0" -c:a aac $(BITEXACT) $@

$(OUT)/audio_22050.m4a: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=1 -c:a aac -ar 22050 $(BITEXACT) $@

# Boundary-media fixtures for test_playback_bounds: a video with exactly one
# encoded frame, and an audio-only clip so short its only buffer is a partial
# one.
$(OUT)/single_frame.mp4: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=1:size=160x120:rate=25 \
	    -frames:v 1 -c:v libx264 -pix_fmt yuv420p $(BITEXACT) $@

$(OUT)/short_audio.m4a: | $(OUT)
	$(FF) -f lavfi -i sine=frequency=440:duration=0.05 -c:a aac $(BITEXACT) $@

# Video codec/pixel-format matrix: one fixture per supported codec, pixel
# format, or odd geometry, for test_video_formats.
$(OUT)/video_vp9.webm: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=1:size=160x120:rate=25 \
	    -c:v libvpx-vp9 -deadline realtime -cpu-used 8 -b:v 200k $(BITEXACT) $@

$(OUT)/video_10bit.mkv: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=1:size=160x120:rate=25 \
	    -c:v libx265 -pix_fmt yuv420p10le -x265-params log-level=none $(BITEXACT) $@

$(OUT)/video_mpeg2.ts: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=1:size=160x120:rate=25 \
	    -f lavfi -i sine=frequency=440:duration=1 \
	    -c:v mpeg2video -c:a mp2 $(BITEXACT) $@

$(OUT)/video_yuyv422.nut: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=1:size=160x120:rate=25 \
	    -c:v rawvideo -pix_fmt yuyv422 $(BITEXACT) $@

# ffmpeg's testsrc2 lavfi source silently rounds odd (non-mod-2) sizes down to
# the nearest even value, so an explicit exact=1 crop after generation is
# needed to actually land on 177x101.
$(OUT)/video_oddsize.nut: | $(OUT)
	$(FF) -f lavfi -i "testsrc2=duration=1:size=178x102:rate=25,crop=177:101:0:0:exact=1" \
	    -c:v rawvideo -pix_fmt rgb24 $(BITEXACT) $@

$(OUT)/video_tiny.mp4: | $(OUT)
	$(FF) -f lavfi -i testsrc2=duration=1:size=16x16:rate=25 \
	    -c:v libx264 -pix_fmt yuv420p $(BITEXACT) $@

# Broken-input fixtures for test_broken_input: deliberately malformed/
# truncated media, to pin that the library fails (or degrades) cleanly rather
# than crashing or hanging.

# Truncated: first 25% of a valid mp4 (moov placement makes mp4 the hard case).
$(OUT)/truncated.mp4: $(OUT)/video_audio.mp4
	size=$$(stat -c%s $(OUT)/video_audio.mp4); head -c $$((size / 4)) $(OUT)/video_audio.mp4 > $@

# Corrupt-middle: valid file with 512 garbage bytes at offset 8192 (header/
# moov untouched, only compressed sample data hit). The garbage comes from the
# committed src/random.bin so regeneration is byte-stable.
$(OUT)/corrupt_middle.mp4: $(OUT)/video_audio.mp4 $(SRC)/random.bin
	cp $(OUT)/video_audio.mp4 $@
	head -c 512 $(SRC)/random.bin | dd of=$@ bs=1 seek=8192 count=512 conv=notrunc 2>/dev/null

# Garbage with a media extension: 4096 random bytes (the committed
# src/random.bin, so regeneration is byte-stable), small enough that it
# cannot accidentally happen to look like valid media (no supported container
# has a magic/header structure random bytes could satisfy at this size).
$(OUT)/garbage.mp4: $(SRC)/random.bin | $(OUT)
	cp $(SRC)/random.bin $@

$(OUT)/empty.mp4: | $(OUT)
	: > $@

# Malformed SRT (unparseable timestamp, end-before-start cue, empty-text cue).
# Not muxed into an mkv: ffmpeg's SRT demuxer rejects the whole file outright
# at open time, so test_broken_input.c opens it directly as a source instead.
$(OUT)/broken.srt: $(SRC)/broken.srt | $(OUT)
	cp $(SRC)/broken.srt $@
