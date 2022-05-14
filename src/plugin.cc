/*
Copyright (C) 2021 DEV47APPS, github.com/dev47apps

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#define _WIN32_WINNT 0x0501
#define _WIN32_IE    0x0500
#define _WIN32_DCOM
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>

#include "plugin.h"
#include "structs.h"

const char *PluginVer  = "001";
const char *PluginName = "DroidCam Virtual Output";

void convert_yuv420_yuyv(uint8_t** data, uint32_t *linesize,
    uint8_t* dst, const int width, const int height);

void scale_yuv420_yuyv(uint8_t** data, uint32_t *linesize, uint8_t* dst,
    const int src_w, const int src_h,
    const int dst_w, const int dst_h,
    int shift_x, int shift_y);

enum ScaleType {
    MATCH = 0,
    SCALE,
    UNK,
};

struct droidcam_output_plugin {
    ScaleType scale_type;
    int dst_w, dst_h;
    int shift_x, shift_y;

    struct audio_convert_info audio_conv;
    struct video_scale_info   video_conv;

    obs_output_t *output;

    // Windows
    VideoHeader *pVideoHeader;
    BYTE        *pVideoData;

    LPVOID pVideoMem;
    HANDLE hVideoMapping;
};

static void get_webcam_size(droidcam_output_plugin *plugin) {
    VideoHeader *header = plugin->pVideoHeader;
    int checksum = (header->info.width  ^ header->info.height
        ^ header->info.interval);

    bool active = header->info.control == CONTROL
                && header->info.checksum == checksum;

    if (active == false) {
        // dlog("header: checksum=%d/exp:%d control=%x/exp:%x",
        //     checksum, header->info.checksum, header->info.control, CONTROL);
        plugin->scale_type = ScaleType::UNK;
        return;
    }

    ilog("webcam active: %dx%d at %d fps",
        header->info.width, header->info.height,
        (int)(RefTime::UNITS / header->info.interval));

    int shift_x, shift_y;
    int src_w = plugin->video_conv.width;
    int src_h = plugin->video_conv.height;
    int dst_w = header->info.width;
    int dst_h = header->info.height;

    if (src_w == dst_w && src_h == dst_h) {
        plugin->dst_w = dst_w;
        plugin->dst_h = dst_h;
        plugin->scale_type = ScaleType::MATCH;
        dlog("resolution matched!");
        return;
    }

    float src_ar = (float) src_w / (float) src_h;
    float dst_ar = (float) dst_w / (float) dst_h;
    if (abs(src_ar - dst_ar) < 0.01f) {
        // same aspect ratio
        shift_x = 0;
        shift_y = 0;
    } else if ((src_h - dst_h) > (src_w - dst_w)) {
        const int dst_w0 = dst_w;
        dst_w = dst_h * src_w / src_h;
        shift_x = (dst_w0 - dst_w) / 2;
        shift_y = 0;
    }
    else {
        const int dst_h0 = dst_h;
        dst_h = dst_w * src_h / src_w;
        shift_x = 0;
        shift_y = (dst_h0 - dst_h) / 2;
    }

    plugin->dst_w = dst_w;
    plugin->dst_h = dst_h;
    plugin->shift_x = shift_x;
    plugin->shift_y = shift_y;
    plugin->scale_type = ScaleType::SCALE;
    dlog("video will be scaled: %dx%d -> %dx%d at %d,%d",
        src_w, src_h, dst_w, dst_h, shift_x, shift_y);
    return;
}

static bool output_start(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (!plugin->pVideoMem) {
        elog("Cannot start without memory mapping !! ");
        return false;
    }

    video_t *video = obs_output_video(plugin->output);
    uint32_t width = video_output_get_width(video);
    uint32_t height = video_output_get_height(video);
    uint32_t format = video_output_get_format(video);

    struct obs_video_info ovi;
    obs_get_video_info(&ovi);
    uint64_t interval = ovi.fps_den * RefTime::UNITS / ovi.fps_num;
    dlog("output_start: %dx%d interval %lld format %d", width, height, interval, format);

    if (0) {
        plugin->audio_conv.format = AUDIO_FORMAT_16BIT;
        plugin->audio_conv.samples_per_sec = 44100;
        plugin->audio_conv.speakers = SPEAKERS_MONO;
        obs_output_set_audio_conversion(plugin->output, &plugin->audio_conv);
    }

    // FIXME benchmark getting YUYV directly from here
    plugin->video_conv.format = VIDEO_FORMAT_I420;
    plugin->video_conv.width  = width;
    plugin->video_conv.height = height;
    obs_output_set_video_conversion(plugin->output, &plugin->video_conv);
    get_webcam_size(plugin);

    obs_output_begin_data_capture(plugin->output, 0);
    ilog("output started");
    return true;
}

static void output_stop(void *data, uint64_t ts) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);

    obs_output_end_data_capture(plugin->output);
    plugin->scale_type = ScaleType::UNK;

    UNUSED_PARAMETER(ts);
    ilog("output stopped");
}

static void output_destroy(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);

    if (plugin) {
        plugin->pVideoData = NULL;
        plugin->pVideoHeader = NULL;
        if (plugin->pVideoMem) {
            dlog("closing shared memory");
            UnmapViewOfFile(plugin->pVideoMem);
            CloseHandle(plugin->hVideoMapping);
        }

        delete plugin;
    }
}

static void *output_create(obs_data_t *settings, obs_output_t *output) {
    ilog("create(output=%p) r%s", output, PluginVer);
    droidcam_output_plugin *plugin = new droidcam_output_plugin();
    plugin->output = output;
    plugin->scale_type = ScaleType::UNK;

    const char* name = VIDEO_MAP_NAME;
    DWORD size = VIDEO_MAP_SIZE;
    ALIGN_SIZE(size, ALIGNMENT);

    dlog("mapping memory");
    plugin->hVideoMapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE,   // use paging file
        NULL,                   // security attributes
        PAGE_READWRITE,
        0,                      // size: high 32-bits
        size,                   // size: low 32-bits
        name);

    if (plugin->hVideoMapping == NULL) {
        elog("CreateFileMapping Failed !! ");
        goto EARLY_OUT;
    }

    plugin->pVideoMem = MapViewOfFile(
        plugin->hVideoMapping,
        FILE_MAP_WRITE,
        0,0,0);


    if (plugin->pVideoMem == NULL) {
        elog("MapViewOfFile Failed !! ");
        CloseHandle(plugin->hVideoMapping);
        plugin->hVideoMapping = NULL;
        goto EARLY_OUT;
    }

    dlog("mapped %d bytes @ %p", size, plugin->pVideoMem);
    plugin->pVideoHeader = (VideoHeader *) plugin->pVideoMem;
    plugin->pVideoData   = (BYTE*)plugin->pVideoMem + sizeof(VideoHeader);

EARLY_OUT:
    UNUSED_PARAMETER(settings);
    return plugin;
}

static void on_video(void *data, struct video_data *frame) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (!plugin->pVideoData)
        return;

    int src_w = plugin->video_conv.width;
    int src_h = plugin->video_conv.height;
    uint8_t* dst = plugin->pVideoData;

    // FIXME lock()
    switch (plugin->scale_type) {
        case ScaleType::MATCH:
            convert_yuv420_yuyv(frame->data, frame->linesize, dst, src_w, src_h);
            break;

        case ScaleType::SCALE:
            scale_yuv420_yuyv(frame->data, frame->linesize, dst,
                src_w, src_h, plugin->dst_w, plugin->dst_h,
                plugin->shift_x, plugin->shift_y);

            // Check for webcam re-starts
            // Webcam resolution should match our output size once re-opened
            if (plugin->pVideoHeader->info.control != CONTROL) {
                dlog("webcam became inactive");
                plugin->scale_type = ScaleType::UNK;
            }
            break;

        case ScaleType::UNK:
            get_webcam_size(plugin);
            break;
    }
}

static const char *output_getname(void *data) {
    UNUSED_PARAMETER(data);
    return PluginName;
}

struct obs_output_info droidcam_virtual_output_info;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("droidcam-virtual-output", "en-US")
MODULE_EXPORT const char *obs_module_description(void) {
    return PluginName;
}

bool obs_module_load(void) {
    memset(&droidcam_virtual_output_info, 0, sizeof(struct obs_output_info));

    droidcam_virtual_output_info.id       = "droidcam_virtual_output",
    droidcam_virtual_output_info.flags    = OBS_OUTPUT_VIDEO,
    droidcam_virtual_output_info.get_name = output_getname,
    droidcam_virtual_output_info.create   = output_create,
    droidcam_virtual_output_info.destroy  = output_destroy,
    droidcam_virtual_output_info.start    = output_start,
    droidcam_virtual_output_info.stop     = output_stop,
    droidcam_virtual_output_info.raw_video = on_video,
    obs_register_output(&droidcam_virtual_output_info);

    #ifdef DROIDCAM_OVERRIDE
    obs_data_t *obs_settings = obs_data_create();
    obs_data_set_bool(obs_settings, "vcamEnabled", true);
    obs_apply_private_data(obs_settings);
    obs_data_release(obs_settings);
    #endif
    return true;
}
