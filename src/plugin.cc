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
#include "queue.h"
#include "structs.h"

#if DROIDCAM_OVERRIDE==0

#if LIBOBS_API_MAJOR_VER==28
#include <QtGui/QAction>
#else
#include <QtWidgets/QAction>
#endif
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QMainWindow>
#include "obs-frontend-api.h"

#endif

const char *PluginVer  = "011";
const char *PluginName = "DroidCam Virtual Output";
obs_output_t *droidcam_virtual_output = NULL;

void map_yuv420_yuyv(uint8_t** data, uint32_t *linesize, uint8_t* dst,
    int shift_x, int shift_y, int is_aligned_128b,
    const int dest_width, const int dest_height,
    const int width, const int height);

void clear_yuyv(uint8_t* dst, int size, int color);

struct droidcam_output_plugin {
    // video
    int webcam_w, webcam_h;
    int default_w, default_h;
    int default_interval;
    int shift_x, shift_y;
    int is_aligned_128b;

    // audio
    int default_sample_rate;
    enum speaker_layout default_speaker_layout;

    //
    bool have_video;
    bool have_audio;

    int audio_frame_size_bytes;
    struct audio_convert_info audio_conv;
    struct video_scale_info   video_conv;

    //
    obs_output_t *output;
    pthread_t audio_thread;
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
    DataQueue audioDataQueue;
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

static inline int to_channels(enum speaker_layout speaker_layout) {
    switch (speaker_layout) {
    case SPEAKERS_STEREO:
        return 2;
    case SPEAKERS_MONO:
    default:
        return 1;
    }
}

#define AUDIO_CUSHION 4

static void *audio_thread(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    dlog("audio_thread start");

    int waiting = 0;
    while ((os_event_timedwait(plugin->stop_signal, 5) != 0) && (plugin->pAudioData))
    {
        if (!plugin->have_audio) {
            if (!waiting) waiting = 1;
            continue;
        }

        if (waiting) {
            if (plugin->audioDataQueue.readyQueue.size() < AUDIO_CUSHION)
                continue;

            waiting = 0;
        }
        else {
            if (plugin->audioDataQueue.readyQueue.size() == 0)
                waiting = 1;
        }

        if (plugin->pAudioHeader->data_valid)
            continue;

        plugin->audioDataQueue.lock();
        DataPacket *packet = plugin->audioDataQueue.pull_ready_packet();
        if (packet) {
            memcpy(plugin->pAudioData, packet->data, packet->used);
            plugin->pAudioHeader->data_valid = 1;
            plugin->audioDataQueue.push_empty_packet(packet);
        } else {
            dlog("missed frame");
        }
        plugin->audioDataQueue.unlock();
    }

    dlog("audio_thread end");
    return 0;
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

    // force even amounts
    shift_x &= ~1;
    shift_y &= ~1;
    dst_w &= ~1;
    dst_h &= ~1;

    ilog("video scaling: %dx%d -> %dx%d -> %dx%d at %d,%d",
        src_w, src_h, dst_w, dst_h,
        plugin->webcam_w, plugin->webcam_h,
        shift_x, shift_y);
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

        //dlog("audio queue size: %d / %d",
        //    (int) plugin->audioDataQueue.emptyQueue.size(),
        //    (int) plugin->audioDataQueue.readyQueue.size());

        if (!have_video && !have_audio) {
            if (obs_output_active(plugin->output)) {
                ilog("webcam became inactive");
                obs_output_end_data_capture(plugin->output);
            }

            continue;
        }

        int flags = 0;
        int webcam_w, webcam_h, webcam_interval;

        int webcam_audio_rate;
        enum speaker_layout webcam_speaker_layout;

        if (have_video) {
            webcam_w = vh->info.width;
            webcam_h = vh->info.height;
            webcam_interval = vh->info.interval;
            flags |= OBS_OUTPUT_VIDEO;
        }
        else {
            webcam_w = plugin->default_w;
            webcam_h = plugin->default_h;
            webcam_interval = plugin->default_interval;
        }

        if (have_audio) {
            webcam_audio_rate  = ah->info.sample_rate;
            webcam_speaker_layout = to_speaker_layout(ah->info.channels);

            if (webcam_speaker_layout != SPEAKERS_UNKNOWN) {
                flags |= OBS_OUTPUT_AUDIO;
            }
            else {
                elog("WARN: unknown webcam speaker layout, channels=%d", ah->info.channels);
                have_audio = false;
            }
        }
        if (!have_audio) {
            webcam_audio_rate = plugin->default_sample_rate;
            webcam_speaker_layout = plugin->default_speaker_layout;
        }

        const bool video_ok =
            (webcam_w - plugin->shift_x - plugin->shift_x - plugin->video_conv.width <= 4) &&
            (webcam_h - plugin->shift_y - plugin->shift_y - plugin->video_conv.height <= 4);

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
            ilog("webcam video active %dx%d %dfps, video_ok=%d",
                webcam_w, webcam_h,
                (int)(RefTime::UNITS / webcam_interval),
                (int) video_ok);

        if (have_audio)
            ilog("webcam audio active %d Hz %d channels, audio_ok=%d",
                webcam_audio_rate, webcam_speaker_layout, (int) audio_ok);

        if (!video_ok) {
            plugin->webcam_w = webcam_w;
            plugin->webcam_h = webcam_h;
            video_conversion(plugin);
            plugin->is_aligned_128b = (plugin->video_conv.width % 16 == 0);
            obs_output_set_video_conversion(plugin->output, &plugin->video_conv);
        }

        if (!audio_ok) {
            plugin->audio_frame_size_bytes = (SAMPLE_BITS/8) * to_channels(webcam_speaker_layout);
            plugin->audio_conv.speakers = webcam_speaker_layout;
            plugin->audio_conv.samples_per_sec = webcam_audio_rate;
            obs_output_set_audio_conversion(plugin->output, &plugin->audio_conv);
        }

        plugin->have_video = have_video;
        plugin->have_audio = have_audio;
        plugin->audioDataQueue.lock();
        plugin->audioDataQueue.clear();
        plugin->audioDataQueue.unlock();
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
    pthread_join(plugin->audio_thread, NULL);
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
    dlog("output_start: video %dx%d interval %lld format %d", width, height, interval, format);

    #if DEBUG==1
    if (ovi.output_width != width)
        elog("output width mismatch !!");
    if (ovi.output_height != height)
        elog("output height mismatch !!");
    if (ovi.output_format != format)
        elog("output format mismatch !!");
    #endif

    plugin->have_video = false;
    plugin->shift_x = 0;
    plugin->shift_y = 0;
    plugin->default_w = width;
    plugin->default_h = height;
    plugin->default_interval = interval;
    plugin->video_conv.format = VIDEO_FORMAT_I420;
    plugin->video_conv.width  = width;
    plugin->video_conv.height = height;
    plugin->is_aligned_128b = (width % 16 == 0);
    obs_output_set_video_conversion(plugin->output, &plugin->video_conv);

    audio_t *audio = obs_output_audio(plugin->output);
    int channels = audio_output_get_channels(audio);
    int sample_rate = (int) audio_output_get_sample_rate(audio);
    dlog("            : audio channels %d sample_rate %d", channels, sample_rate);

    plugin->have_audio = false;
    plugin->default_sample_rate = sample_rate;
    plugin->default_speaker_layout = to_speaker_layout(channels);
    plugin->audio_frame_size_bytes = (SAMPLE_BITS/8) * channels;
    plugin->audio_conv.format = OBS_AUDIO_FMT;
    plugin->audio_conv.samples_per_sec = sample_rate;
    plugin->audio_conv.speakers = plugin->default_speaker_layout;
    obs_output_set_audio_conversion(plugin->output, &plugin->audio_conv);

    os_event_reset(plugin->stop_signal);
    pthread_create(&plugin->audio_thread, NULL, audio_thread, plugin);
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
            ilog("closing shared memory [video]");
            UnmapViewOfFile(plugin->pVideoMem);
            CloseHandle(plugin->hVideoMapping);
        }

        plugin->pAudioData = NULL;
        plugin->pAudioHeader = NULL;
        if (plugin->pAudioMem) {
            ilog("closing shared memory [audio]");
            UnmapViewOfFile(plugin->pAudioMem);
            CloseHandle(plugin->hAudioMapping);
        }

        if (plugin->hVideoWrLock) CloseHandle(plugin->hVideoWrLock);
        if (plugin->hVideoRdLock) CloseHandle(plugin->hVideoRdLock);
        #endif

        os_event_destroy(plugin->stop_signal);
        delete plugin;
        ilog("plugin destroyed");
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
        ilog("mapped %8d bytes @ %p [video]", size, plugin->pVideoMem);
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
        ilog("mapped %8d bytes @ %p [audio]", size, plugin->pAudioMem);
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
    if (plugin->have_video && plugin->pVideoData) {
        #ifdef _WIN32
        if (plugin->hVideoWrLock && plugin->hVideoRdLock) {
            ResetEvent(plugin->hVideoWrLock);
            if (WaitForSingleObject(plugin->hVideoRdLock, 5) == 0)
            {
                uint8_t* dst = plugin->pVideoData;
                map_yuv420_yuyv(frame->data, frame->linesize, dst,
                    plugin->shift_x, plugin->shift_y,
                    plugin->is_aligned_128b,
                    plugin->webcam_w, plugin->webcam_h,
                    plugin->video_conv.width, plugin->video_conv.height);
            }
            else
            {
                dlog("video lock fail/timeout: frame dropped");
            }
            SetEvent(plugin->hVideoWrLock);
        }
        #endif
    }
}

static void on_audio(void *data, struct audio_data *frame) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (plugin->have_audio) {

        #ifdef _WIN32
        if (plugin->audioDataQueue.readyQueue.size() < AUDIO_CUSHION) {
            const int frames = frame->frames > DEF_FRAMES ? DEF_FRAMES : frame->frames;
            const int size = frames * plugin->audio_frame_size_bytes;

            plugin->audioDataQueue.lock();
            DataPacket *packet = plugin->audioDataQueue.pull_empty_packet(size);
            memcpy(packet->data, frame->data[0], size);
            packet->used = size;
            // packet->pts = frame->timestamp;
            plugin->audioDataQueue.push_ready_packet(packet);
            plugin->audioDataQueue.unlock();
        }
        #endif // _WIN32

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



    #if DROIDCAM_OVERRIDE

    obs_data_t *obs_settings = obs_data_create();
    obs_data_set_bool(obs_settings, "vcamEnabled", true);
    obs_apply_private_data(obs_settings);
    obs_data_release(obs_settings);

    #else

    QMainWindow *main_window = (QMainWindow *)obs_frontend_get_main_window();
    QAction *tools_menu_action = (QAction*)obs_frontend_add_tools_menu_qaction(PluginName);
    tools_menu_action->setCheckable(true);
    tools_menu_action->setChecked(false);

    tools_menu_action->connect(tools_menu_action, &QAction::triggered, [=] (bool checked) {
        if (!droidcam_virtual_output) {
            obs_data_t *obs_settings = obs_data_create();
            droidcam_virtual_output = obs_output_create(
                "droidcam_virtual_output", "DroidCamVirtualOutput", obs_settings, NULL);
            ilog("droidcam_virtual_output=%p", droidcam_virtual_output);
            obs_data_release(obs_settings);
        }

        if (checked) {
            obs_output_set_media(droidcam_virtual_output,
                obs_get_video(), obs_get_audio());

            if (!obs_output_start(droidcam_virtual_output)) {
                obs_output_force_stop(droidcam_virtual_output);
                tools_menu_action->setChecked(false);

                QMessageBox mb(QMessageBox::Warning, PluginName,
                    obs_module_text("OutputStartFailed"), QMessageBox::Ok, main_window);
                mb.setButtonText(QMessageBox::Ok, "OK");
                mb.exec();
            }
        }
        else {
            // Force stop since we may not be actively capturing data,
            // if the webcam is not in use.
            obs_output_force_stop(droidcam_virtual_output);
        }
    });

    // todo - investigate: there seems to be a race condition in obs_graphics_thread,
    // causing a crash when exiting while the output is enabled and capturing.
    // I'm guessing the pthread_joins here are creating delays and triggering it.
    // Comment this out to reproduce.
    obs_frontend_add_event_callback([] (enum obs_frontend_event event, void*) {
        if (event == OBS_FRONTEND_EVENT_EXIT && droidcam_virtual_output) {
            obs_output_force_stop(droidcam_virtual_output);
            obs_output_release(droidcam_virtual_output);
            droidcam_virtual_output = NULL;
        }

    }, NULL);

    #endif // DROIDCAM_OVERRIDE
    return true;
}

void obs_module_unload(void) {
}
