# recordmydesktop-pulse
Fork of recordmydesktop with PulseAudio support.

To enable PulseAudio support, configure with --enable-pulse.

OSS is too old and should not be used. PulseAudio is used only if compiled with ALSA, and ALSA is used as primary backend, not OSS.

No special command line options are needed to run recording with pulseaudio (unlike JACK, which requires --jack option). If compiled with alsa+pulse support, recordmydesktop first tries to connect to PulseAudio server, and if that fails, falls back to ALSA. Full backward compatibility.

In case of using pulseaudio, only the source of audio data bytes is changed, all other caching/encoding processes are the same as before.
