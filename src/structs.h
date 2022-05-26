// Copyright (C) 2021 DEV47APPS, github.com/dev47apps
#pragma once

#define CONTROL    0x02020101
#define MAX_WIDTH  1920
#define MAX_HEIGHT 1080
#define DEF_WIDTH  640
#define DEF_HEIGHT 480

#define RGB_BUFFER_SIZE(w,h) (w*h*3)

#define ALIGNMENT 32
#define ALIGN_SIZE(size, align) size = (((size) + (align - 1)) & (~(align - 1)))

#define VIDEO_MAP_SIZE  (sizeof(VideoHeader) + RGB_BUFFER_SIZE(MAX_WIDTH,MAX_HEIGHT))


#define AUDIO_MAP_NAME L"DroidCamOBS_AudioOut0"

#define VIDEO_MAP_NAME     L"DroidCamOBS_VideoOut0"
#define VIDEO_WR_LOCK_NAME L"DroidCamOBS_VideoWr0"
#define VIDEO_RD_LOCK_NAME L"DroidCamOBS_VideoRd0"

#define REG_WEBCAM_SIZE_KEY  L"SOFTWARE\\DroidCam"
#define REG_WEBCAM_SIZE_VAL  L"Size"

/* Video Header */
typedef struct {
    int version;
    int control;
    int checksum;
    int width, height;
    int interval;
    int format;
} DroidCamVideoInfo;

typedef struct {
    DroidCamVideoInfo info;
    char reserved0[1024 - sizeof(DroidCamVideoInfo)];
} VideoHeader;

/* Audio Header */
typedef struct {
    int version;
    int control;
    int checksum;
    int sample_rate;
    int channels;
} DroidCamAudioInfo;

typedef struct {
    DroidCamAudioInfo info;
    char reserved0[1024 - sizeof(DroidCamAudioInfo)];
} AudioHeader;

// See also dshow CRefTime
#define MS_TO_100NS_UNITS(ms) ((ms) * (UNITS / MILLI_SEC))
enum RefTime {
    MILLI_SEC = 1000ULL,       // 10 ^ 3
    NANO_SEC  = 1000000000ULL, // 10 ^ 9
    UNITS = NANO_SEC/100,      // 10 ^ 7
};
