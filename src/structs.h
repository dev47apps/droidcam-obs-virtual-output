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
#define VIDEO_MAP_NAME L"DroidCamOBS_VideoOut0"

#define REG_WEBCAM_SIZE_KEY  L"SOFTWARE\\DroidCam"
#define REG_WEBCAM_SIZE_VAL  L"Size"


typedef struct smvi_s {
    int version;
    int control;
    int checksum;
    int width, height;
    int interval;
    int format;
} DroidCamVideoInfo;

typedef struct smh_s {
    DroidCamVideoInfo info;
    char reserved0[1024 - sizeof(DroidCamVideoInfo)];
} VideoHeader;

// See also dshow CRefTime
#define MS_TO_100NS_UNITS(ms) ((ms) * (UNITS / MILLI_SEC))
enum RefTime {
    MILLI_SEC = 1000ULL,       // 10 ^ 3
    NANO_SEC  = 1000000000ULL, // 10 ^ 9
    UNITS = NANO_SEC/100,      // 10 ^ 7
};

#define WEBCAM_SIZE_COUNT 4
static const int WEBCAM_SIZE_MAP[WEBCAM_SIZE_COUNT][2] = {
    {  640, 480 },
    {  960, 720 },
    { 1280, 720 },
    { 1920,1080 },
};

static int WebcamIndexFromSize(int width, int height) {
    int index = WEBCAM_SIZE_COUNT;
    for (; index > 0; --index) {
        if (WEBCAM_SIZE_MAP[index][0] == width
            && WEBCAM_SIZE_MAP[index][1] == height)
            break;
    }
    return index;
}
