/*
Copyright (C) 2022 DEV47APPS, github.com/dev47apps

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

const char *PluginVer  = "001";
const char *PluginName = "DroidCam Virtual Output";

void map_yuv420_yuyv(uint8_t** data, uint32_t *linesize, uint8_t* dst,
    int shift_x, int shift_y,
    const int dest_width, const int dest_height,
    const int width, const int height);

void clear_yuyv(uint8_t* dst, int size, int color);

struct droidcam_output_plugin {
    // video
    int webcam_w, webcam_h;
    int default_w, default_h;
    int default_interval;
    int shift_x, shift_y;

    // audio
    int default_sample_rate;
    enum speaker_layout default_speaker_layout;

    //
    bool have_video;
    bool have_audio;

    struct audio_convert_info audio_conv;
    struct video_scale_info   video_conv;

    //
    obs_output_t *output;
    pthread_t control_thread;
    os_event_t *stop_signal;

    //
    #ifdef _WIN32
    volatile VideoHeader *pVideoHeader;
    volatile AudioHeader *pAudioHeader;
    BYTE *pVideoData;
    BYTE *pAudioData;

    LPVOID pVideoMem;
    HANDLE hVideoMapping;
    HANDLE hVideoWrLock;
    HANDLE hVideoRdLock;

    LPVOID pAudioMem;
    HANDLE hAudioMapping;
    #endif
};

static inline enum speaker_layout to_speaker_layout(int channels) {
    switch (channels) {
    case 1:
        return SPEAKERS_MONO;
    case 2:
        return SPEAKERS_STEREO;
    default:
        return SPEAKERS_UNKNOWN;
    }
}

static void video_conversion(droidcam_output_plugin *plugin) {
    int shift_x, shift_y;
    int src_w = plugin->default_w;
    int src_h = plugin->default_h;
    int dst_w = plugin->webcam_w;
    int dst_h = plugin->webcam_h;

    if (src_w == dst_w && src_h == dst_h) {
        plugin->shift_x = 0;
        plugin->shift_y = 0;
        plugin->video_conv.width = dst_w;
        plugin->video_conv.height = dst_h;
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

    ilog("video scaling: %dx%d -> %dx%d at %d,%d",
        src_w, src_h, dst_w, dst_h, shift_x, shift_y);
    plugin->video_conv.width = dst_w;
    plugin->video_conv.height = dst_h;
    plugin->shift_x = shift_x;
    plugin->shift_y = shift_y;
}

static void *control_thread(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    dlog("control_thread start");

    volatile VideoHeader *vh = plugin->pVideoHeader;
    volatile AudioHeader *ah = plugin->pAudioHeader;

    while (os_event_timedwait(plugin->stop_signal, 999) != 0) {

        bool have_video =
            vh->info.control == CONTROL
            && vh->info.checksum == (vh->info.interval ^
                    vh->info.width  ^ vh->info.height);

        bool have_audio =
            ah->info.control == CONTROL
            && ah->info.checksum == (ah->info.sample_rate ^ ah->info.channels);

        if (!have_video && !have_audio) {
            if (obs_output_active(plugin->output)) {
                dlog("webcam became inactive");
                obs_output_end_data_capture(plugin->output);
            }

            continue;
        }

        int webcam_w, webcam_h, webcam_interval;

        int webcam_audio_rate;
        enum speaker_layout webcam_speaker_layout;

        if (have_video) {
            webcam_w = vh->info.width;
            webcam_h = vh->info.height;
            webcam_interval = vh->info.interval;
        }
        else {
            webcam_w = plugin->default_w;
            webcam_h = plugin->default_h;
            webcam_interval = plugin->default_interval;
        }

        if (have_audio) {
            webcam_audio_rate  = ah->info.sample_rate;
            webcam_speaker_layout = to_speaker_layout(ah->info.channels);
        }
        else {
            webcam_audio_rate = plugin->default_sample_rate;
            webcam_speaker_layout = plugin->default_speaker_layout;
        }

        const bool video_ok =
            (webcam_w - plugin->shift_x - plugin->shift_x - plugin->video_conv.width < 4) &&
            (webcam_h - plugin->shift_y - plugin->shift_y - plugin->video_conv.height < 4);

        const bool audio_ok =
            plugin->audio_conv.speakers == webcam_speaker_layout &&
            plugin->audio_conv.samples_per_sec == webcam_audio_rate;

        if (obs_output_active(plugin->output)) {
            if (audio_ok && video_ok)
                continue;

            dlog("output conversion needs to be updated");
            obs_output_end_data_capture(plugin->output);

            // todo: this can probably be improved..
            while (obs_output_active(plugin->output)) {
                os_sleep_ms(5);
                if (os_event_try(plugin->stop_signal) != EAGAIN)
                    return 0;
            }
        }

        if (have_video)
            ilog("webcam video active %dx%d %dfps",
                webcam_w, webcam_h,
                (int)(RefTime::UNITS / webcam_interval));

        if (have_audio)
            ilog("webcam audio active %d Hz %d channels",
                webcam_audio_rate, webcam_speaker_layout);

        if (!video_ok) {
            plugin->webcam_w = webcam_w;
            plugin->webcam_h = webcam_h;
            video_conversion(plugin);
            obs_output_set_video_conversion(plugin->output, &plugin->video_conv);
        }

        if (!audio_ok) {
            plugin->audio_conv.speakers = webcam_speaker_layout;
            plugin->audio_conv.samples_per_sec = webcam_audio_rate;

            if (plugin->audio_conv.speakers == SPEAKERS_UNKNOWN) {
                elog("WARN: unkown webcam speaker layout: %d", plugin->audio_conv.speakers);
                have_audio = false;
            }
            obs_output_set_audio_conversion(plugin->output, &plugin->audio_conv);
        }

        plugin->have_video = have_video;
        plugin->have_audio = have_audio;
        memset(plugin->pAudioData, 0, AUDIO_DATA_SIZE * CHUNKS_COUNT);
        clear_yuyv(plugin->pVideoData, MAX_WIDTH*MAX_HEIGHT*2, 0x80008000);
        obs_output_begin_data_capture(plugin->output, 0);
    }

    dlog("control_thread end");
    return 0;
}

static void output_stop(void *data, uint64_t ts) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);

    dlog("output_stop");
    os_event_signal(plugin->stop_signal);
    pthread_join(plugin->control_thread, NULL);
    obs_output_end_data_capture(plugin->output);

    UNUSED_PARAMETER(ts);
}

static bool output_start(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (!(plugin->pVideoMem && plugin->pAudioMem)) {
        elog("Cannot start without memory mapping !! ");
        return false;
    }
    if (os_event_try(plugin->stop_signal) != 0) {
        output_stop(data, 0);
    }

    video_t *video = obs_output_video(plugin->output);
    int width = video_output_get_width(video);
    int height = video_output_get_height(video);
    int format = video_output_get_format(video);

    struct obs_video_info ovi;
    obs_get_video_info(&ovi);
    int interval = ovi.fps_den * RefTime::UNITS / ovi.fps_num;
    ilog("output_start: video %dx%d interval %lld format %d", width, height, interval, format);

    plugin->have_video = false;
    plugin->default_w = width;
    plugin->default_h = height;
    plugin->default_interval = interval;
    plugin->video_conv.format = VIDEO_FORMAT_I420;
    plugin->video_conv.width  = width;
    plugin->video_conv.height = height;
    obs_output_set_video_conversion(plugin->output, &plugin->video_conv);

    audio_t *audio = obs_output_audio(plugin->output);
    int channels = audio_output_get_channels(audio);
    int sample_rate = (int) audio_output_get_sample_rate(audio);
    ilog("            : audio channels %d sample_rate %d", channels, sample_rate);

    plugin->have_audio = false;
    plugin->default_sample_rate = sample_rate;
    plugin->default_speaker_layout = to_speaker_layout(channels);
    plugin->audio_conv.format = OBS_AUDIO_FMT;
    plugin->audio_conv.samples_per_sec = sample_rate;
    plugin->audio_conv.speakers = plugin->default_speaker_layout;
    obs_output_set_audio_conversion(plugin->output, &plugin->audio_conv);

    os_event_reset(plugin->stop_signal);
    pthread_create(&plugin->control_thread, NULL, control_thread, plugin);
    return true;
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
            dlog("closing shared memory [video]");
            UnmapViewOfFile(plugin->pVideoMem);
            CloseHandle(plugin->hVideoMapping);
        }

        plugin->pAudioData = NULL;
        plugin->pAudioHeader = NULL;
        if (plugin->pAudioMem) {
            dlog("closing shared memory [audio]");
            UnmapViewOfFile(plugin->pAudioMem);
            CloseHandle(plugin->hAudioMapping);
        }

        if (plugin->hVideoWrLock) CloseHandle(plugin->hVideoWrLock);
        if (plugin->hVideoRdLock) CloseHandle(plugin->hVideoRdLock);
        #endif

        os_event_destroy(plugin->stop_signal);
        delete plugin;
    }
}

static void *output_create(obs_data_t *settings, obs_output_t *output) {
    ilog("output_create: %p r%s", output, PluginVer);
    droidcam_output_plugin *plugin = new droidcam_output_plugin();
    plugin->output = output;
    os_event_init(&plugin->stop_signal, OS_EVENT_TYPE_MANUAL);

#ifdef _WIN32
{
    const LPCWSTR name = VIDEO_MAP_NAME;
    DWORD size = VIDEO_MAP_SIZE;
    ALIGN_SIZE(size, ALIGNMENT);

    if (CreateSharedMem(&plugin->hVideoMapping, &plugin->pVideoMem, name, size)) {
        dlog("mapped %8d bytes @ %p [video]", size, plugin->pVideoMem);
        plugin->pVideoHeader = (VideoHeader *) plugin->pVideoMem;
        plugin->pVideoData   = (BYTE*)(plugin->pVideoHeader + 1);
    }

    plugin->hVideoWrLock = CreateEventW( NULL, TRUE, TRUE, VIDEO_WR_LOCK_NAME );
    plugin->hVideoRdLock = CreateEventW( NULL, TRUE, TRUE, VIDEO_RD_LOCK_NAME );
}
{
    const LPCWSTR name = AUDIO_MAP_NAME;
    DWORD size = AUDIO_MAP_SIZE;
    ALIGN_SIZE(size, ALIGNMENT);

    if (CreateSharedMem(&plugin->hAudioMapping, &plugin->pAudioMem, name, size)) {
        dlog("mapped %8d bytes @ %p [audio]", size, plugin->pAudioMem);
        plugin->pAudioHeader = (AudioHeader *) plugin->pAudioMem;
        plugin->pAudioData   = (BYTE*)plugin->pAudioMem + sizeof(AudioHeader);
    }
}
#endif // _WIN32

    UNUSED_PARAMETER(settings);
    return plugin;
}

static void on_video(void *data, struct video_data *frame) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (plugin->pVideoData && plugin->have_video) {
        uint8_t* dst = plugin->pVideoData;

        #ifdef _WIN32
        if (plugin->hVideoWrLock) ResetEvent( plugin->hVideoWrLock );
        if (plugin->hVideoRdLock) WaitForSingleObject(plugin->hVideoRdLock, 5);
        #endif

        map_yuv420_yuyv(frame->data, frame->linesize, dst,
            plugin->shift_x, plugin->shift_y,
            plugin->webcam_w, plugin->webcam_h,
            plugin->video_conv.width, plugin->video_conv.height);

        #ifdef _WIN32
        if (plugin->hVideoWrLock) SetEvent(plugin->hVideoWrLock);
        #endif
    }
}

static void on_audio(void *data, struct audio_data *frame) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (plugin->pAudioData && plugin->have_audio) {
        // FIXME hmm.....
        dlog("on_audio: %p frames=%d, ts=%ld",
            frame->data[0], frame->frames, frame->timestamp);
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
    droidcam_virtual_output_info.flags    = OBS_OUTPUT_AV,
    droidcam_virtual_output_info.get_name = output_getname,
    droidcam_virtual_output_info.create   = output_create,
    droidcam_virtual_output_info.destroy  = output_destroy,
    droidcam_virtual_output_info.start    = output_start,
    droidcam_virtual_output_info.stop     = output_stop,
    droidcam_virtual_output_info.raw_video = on_video,
    droidcam_virtual_output_info.raw_audio = on_audio,
    obs_register_output(&droidcam_virtual_output_info);

    #ifdef DROIDCAM_OVERRIDE
    obs_data_t *obs_settings = obs_data_create();
    obs_data_set_bool(obs_settings, "vcamEnabled", true);
    obs_apply_private_data(obs_settings);
    obs_data_release(obs_settings);
    #endif
    return true;
}
