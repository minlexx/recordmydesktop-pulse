# recordmydesktop-pulse
Fork of recordmydesktop with PulseAudio support.

To enable PulseAudio support, configure with `--enable-pulse`.

PulseAudio is used only if compiled with ALSA, and ALSA is used as primary backend, not OSS (this is used by default). OSS is too old anyway and should not be used.

No special command line options are needed to run recording with PulseAudio (unlike JACK, which requires `--use-jack` option). If compiled with alsa+pulse support, recordmydesktop first tries to connect to PulseAudio server, and if that fails, falls back to ALSA. Full backward compatibility. When using pulseaudio, only the source of audio data bytes is changed, all other caching/encoding processes are the same as before. The behavior of front-ends like qt-recordMyDesktop or gtk-recordMyDesktop is not affected in any way.

Demo video: https://youtu.be/LezNArBVvjM
