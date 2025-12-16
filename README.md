
An alternative virtual output plugin that connects [OBS Studio](https://obsproject.com)
with the DroidCam virtual camera drivers on Windows.

Important:

* You must install both the OBS plugin and the DroidCam drivers.
Both are available under [releases](../../releases).

* This plugin is NOT compatible with the legacy (Classic) DroidCam drivers
("DroidCam Source 3", "DroidCam Virtual Microphone").

* The new drivers will be listed as "Droidcam Video" and "Droidcam Audio" in Device Manager.
The new drivers support up to 1440p video and 48kHz stereo audio.

* Output from OBS will get matched with the active resolution, fps, and sampling rate of the drivers (as set by 3rd party apps). For best performance your OBS settings should match the parameters of the 3rd party apps.
