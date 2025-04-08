// Copyright (C) 2021 DEV47APPS, github.com/dev47apps
#pragma once
#pragma warning(disable : 4505)

#define CONTROL    0x02020101
#define MAX_WIDTH  3860
#define MAX_HEIGHT 2160
#define DEF_WIDTH  640
#define DEF_HEIGHT 480

#define RGB_BUFFER_SIZE(w,h) (w*h*3)

#define ALIGNMENT 32
#define ALIGN_SIZE(size, align) size = (((size) + (align - 1)) & (~(align - 1)))

#define VIDEO_MAP_SIZE  (sizeof(VideoHeader) + RGB_BUFFER_SIZE(MAX_WIDTH,MAX_HEIGHT))

#define SAMPLE_BITS    16
#define OBS_AUDIO_FMT  AUDIO_FORMAT_16BIT
#define MAX_CHANNELZ   2
#define DEF_FRAMES     1024
#define CHUNKS_COUNT   2
#define AUDIO_DATA_SIZE  ((SAMPLE_BITS/8) * DEF_FRAMES * MAX_CHANNELZ)
#define AUDIO_MAP_SIZE   (sizeof(AudioHeader) + (AUDIO_DATA_SIZE * CHUNKS_COUNT))

#define AUDIO_MAP_NAME     L"DroidCamOBS_AudioOut0"
#define VIDEO_MAP_NAME     L"DroidCamOBS_VideoOut1"
#define VIDEO_WR_LOCK_NAME L"DroidCamOBS_VideoWr1"
#define VIDEO_RD_LOCK_NAME L"DroidCamOBS_VideoRd1"

#define REG_WEBCAM_SIZE_KEY  L"SOFTWARE\\DroidCam"
#define REG_WEBCAM_SIZE_VAL  L"Size"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/* Video Header */
typedef struct {
    int version;
    int control;
    int checksum;
    int width, height;
    int interval;
    int format;
} DroidCamVideoInfo;

typedef union {
    struct {
        DroidCamVideoInfo info;
    };
    char pad[1024];
} VideoHeader;

/* Audio Header */
typedef struct {
    int version;
    int control;
    int checksum;
    int sample_rate;
    int channels;
} DroidCamAudioInfo;

typedef union {
    struct {
        DroidCamAudioInfo info;
        int data_valid;
    };
    char pad[1024];
} AudioHeader;

#ifdef    __cplusplus
} // "C"
#endif

// See also dshow CRefTime
#define MS_TO_100NS_UNITS(ms) ((ms) * (UNITS / MILLI_SEC))
enum RefTime {
    MILLI_SEC = 1000ULL,       // 10 ^ 3
    NANO_SEC  = 1000000000ULL, // 10 ^ 9
    UNITS = NANO_SEC/100,      // 10 ^ 7
};
