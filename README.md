
An alternative virtual output plugin that connects OBS Studio
with the DroidCam virtual camera drivers on Windows.

### Important

* You must install both the DroidCam virtual output OBS plugin /and/ the DroidCam drivers.
Separate installers for both are available under [releases](../../releases).

* The plugin is NOT compatible with the legacy DroidCam drivers
("DroidCam Source 3", "DroidCam Virtual Microphone").

* These new drivers will be listed as "Droidcam Video" and "Droidcam Audio" in Device Manager.
The new drivers support up to 1080p60 video and 48kHz stereo audio.

* Output from OBS will get matched with the active resolution, fps, and sampling rate of the drivers, _as set by 3rd party apps_. For best performance, your OBS settings should match the parameters of the 3rd party apps to avoid output scaling.

### Screenshots

Windows Camera app:

<img src="http://files.dev47apps.net/img/ss/dc-windows-camera.png" />

OBS plugin toggle:

<img src="http://files.dev47apps.net/img/ss/obs-dcvo-menu.png" />

OBS output inside Windows Camera:

<img src="http://files.dev47apps.net/img/ss/obs-dcvo-test.png" />
