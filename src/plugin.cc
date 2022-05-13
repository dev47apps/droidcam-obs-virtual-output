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
#include "plugin.h"

const char *PluginVer  = "001"
const char *PluginName = "DroidCam Virtual Output";

struct doridcam_output_plugin {
    obs_output_t *output;
    video_queue_t *vq;
};

static bool virtualcam_start(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    uint32_t width = obs_output_get_width(plugin->output);
    uint32_t height = obs_output_get_height(plugin->output);

    struct obs_video_info ovi;
    obs_get_video_info(&ovi);

    uint64_t interval = ovi.fps_den * 10000000ULL / ovi.fps_num;
    plugin->vq = video_queue_create(width, height, interval);
    // FIXME.. continue here

    struct video_scale_info vsi = {0};
    vsi.format = VIDEO_FORMAT_I420;
    vsi.width  = width;
    vsi.height = height;
    obs_output_set_video_conversion(plugin->output, &vsi);

    obs_output_begin_data_capture(plugin->output, 0);
    ilog("output started")
    return true;
}

static void output_stop(void *data, uint64_t ts) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);

    obs_output_end_data_capture(plugin->output);
    if (plugin->vq) {
        video_queue_close(plugin->vq);
        plugin->vq = NULL;
    }
    UNUSED_PARAMETER(ts);
    ilog("output stopped")
}

static void output_destroy(void *data) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);
    if (plugin) {
        if (plugin->vq) video_queue_close(plugin->vq);
        delete plugin;
    }
}

static void *output_create(obs_data_t *settings, obs_output_t *output) {
    ilog("create(output=%p) r%s", PluginVer, output);
    droidcam_output_plugin *plugin = new droidcam_output_plugin();
    plugin->output = output;

    UNUSED_PARAMETER(settings);
    return plugin;
}

static void on_video(void *data, struct video_data *frame) {
    droidcam_output_plugin *plugin = reinterpret_cast<droidcam_output_plugin *>(data);

    if (!plugin->vq)
        return;

    video_queue_write(plugin->vq, frame->data, frame->linesize, frame->timestamp);
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
    return true;
}