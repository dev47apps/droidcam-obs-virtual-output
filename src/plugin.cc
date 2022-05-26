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
#include <cmath>
#include <util/threading.h>
#include <util/platform.h>
#include "plugin.h"
#include "structs.h"

#define MILLI_SEC 1000
#define NANO_SEC  1000000000

const char *PluginVer  = "001";
const char *PluginName = "DroidCam Virtual Output";

void convert_yuv420_yuyv(uint8_t** data, uint32_t *linesize,
    uint8_t* dst, const int width, const int height);

void clear_yuyv(uint8_t* dst, int size, int color);

struct droidcam_output_plugin {
    int webcam_interval;
    int webcam_w, webcam_h;
    int shift_x, shift_y;

    struct audio_convert_info audio_conv;
    struct video_scale_info   video_conv;

    obs_output_t *output;
    pthread_t control_thread;
    os_event_t *stop_signal;

    #ifdef _WIN32
    VideoHeader *pVideoHeader;
    BYTE        *pVideoData;

    LPVOID pVideoMem;
    HANDLE hVideoMapping;
    #endif
};

static bool get_video_info(droidcam_output_plugin *plugin) {
    VideoHeader *header = plugin->pVideoHeader;
    int checksum = (header->info.width  ^ header->info.height
        ^ header->info.interval);

    bool active = header->info.control == CONTROL
                && header->info.checksum == checksum;

    if (active) {
        plugin->webcam_w = header->info.width;
        plugin->webcam_h = header->info.height;
        plugin->webcam_interval = header->info.interval;
        return true;
    }

    // dlog("header: checksum=%d/exp:%d control=%x/exp:%x",
    //     checksum, header->info.checksum, header->info.control, CONTROL);
    return false;
}

#if 0
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
    ilog("video will be scaled: %dx%d -> %dx%d at %d,%d",
        src_w, src_h, dst_w, dst_h, shift_x, shift_y);
    return;
#endif

static void *control_thread(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);

    int audio_is_active = 0; // todo

    dlog("control_thread start");

    while (os_event_timedwait(plugin->stop_signal, 999) != 0) {

        if (plugin->pVideoHeader->info.control != CONTROL) {
            if (obs_output_active(plugin->output)) {
                dlog("webcam became inactive");
                if (!audio_is_active) {
                    obs_output_end_data_capture(plugin->output);
                }
            }
            continue;
        }

        // wait for webcam
        if (!get_video_info(plugin))
            continue;

        if (obs_output_active(plugin->output)) {
            if (plugin->video_conv.width == plugin->webcam_w
                && plugin->video_conv.height == plugin->webcam_h)
                continue;

            // need to adjust conversion size
            obs_output_end_data_capture(plugin->output);

            // todo: use signals
            do { os_sleep_ms(5); } while (obs_output_active(plugin->output));
        }
        else {
            ilog("webcam became active: %dx%d at %d fps",
                plugin->webcam_w, plugin->webcam_h,
                (int)(RefTime::UNITS / plugin->webcam_interval));
        }

        clear_yuyv(plugin->pVideoData, MAX_WIDTH*MAX_HEIGHT*2, 0x80008000);

        // fixme: check aspect ratio
        plugin->video_conv.width  = plugin->webcam_w;
        plugin->video_conv.height = plugin->webcam_h;
        obs_output_set_video_conversion(plugin->output, &plugin->video_conv);
        obs_output_begin_data_capture(plugin->output, 0);
    }

    dlog("control_thread end");
    return 0;
}

static bool output_start(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (!plugin->pVideoMem) {
        elog("Cannot start without memory mapping !! ");
        return false;
    }

    video_t *video = obs_output_video(plugin->output);
    int width = video_output_get_width(video);
    int height = video_output_get_height(video);
    int format = video_output_get_format(video);

    struct obs_video_info ovi;
    obs_get_video_info(&ovi);
    int interval = ovi.fps_den * RefTime::UNITS / ovi.fps_num;
    ilog("output_start: canvas size %dx%d interval %lld format %d", width, height, interval, format);

    if (0) {
        plugin->audio_conv.format = AUDIO_FORMAT_16BIT;
        plugin->audio_conv.samples_per_sec = 44100;
        plugin->audio_conv.speakers = SPEAKERS_STEREO;
        obs_output_set_audio_conversion(plugin->output, &plugin->audio_conv);
    }

    plugin->video_conv.format = VIDEO_FORMAT_I420;
    plugin->video_conv.width  = width;
    plugin->video_conv.height = height;
    os_event_reset(plugin->stop_signal);
    pthread_create(&plugin->control_thread, NULL, control_thread, plugin);
    return true;
}

static void output_stop(void *data, uint64_t ts) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);

    dlog("output_stop");
    os_event_signal(plugin->stop_signal);
    pthread_join(plugin->control_thread, NULL);
    obs_output_end_data_capture(plugin->output);

    UNUSED_PARAMETER(ts);
}

static void output_destroy(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);

    dlog("output_destroy");
    if (plugin) {

        if (os_event_try(plugin->stop_signal) != 0) {
            output_stop(data, 0);
        }

        #ifdef _WIN32
        plugin->pVideoData = NULL;
        plugin->pVideoHeader = NULL;
        if (plugin->pVideoMem) {
            dlog("closing shared memory");
            UnmapViewOfFile(plugin->pVideoMem);
            CloseHandle(plugin->hVideoMapping);
        }
        #endif

        os_event_destroy(plugin->stop_signal);
        delete plugin;
    }
}

static void *output_create(obs_data_t *settings, obs_output_t *output) {
    ilog("output_create output=%p r%s", output, PluginVer);
    droidcam_output_plugin *plugin = new droidcam_output_plugin();
    plugin->output = output;
    os_event_init(&plugin->stop_signal, OS_EVENT_TYPE_MANUAL);

    #ifdef _WIN32
    const LPCWSTR name = VIDEO_MAP_NAME;
    DWORD size = VIDEO_MAP_SIZE;
    ALIGN_SIZE(size, ALIGNMENT);

    if (CreateSharedMem(&plugin->hVideoMapping, &plugin->pVideoMem, name, size)) {
        dlog("mapped %d bytes @ %p", size, plugin->pVideoMem);
        plugin->pVideoHeader = (VideoHeader *) plugin->pVideoMem;
        plugin->pVideoData   = (BYTE*)plugin->pVideoMem + sizeof(VideoHeader);
    }
    #endif // _WIN32

    UNUSED_PARAMETER(settings);
    return plugin;
}

static void on_video(void *data, struct video_data *frame) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (plugin->pVideoData) {
        int src_w = plugin->video_conv.width;
        int src_h = plugin->video_conv.height;
        uint8_t* dst = plugin->pVideoData;

        // FIXME lock()
        convert_yuv420_yuyv(frame->data, frame->linesize, dst, src_w, src_h);
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
