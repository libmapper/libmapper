
if HAVE_AUDIO
noinst_LTLIBRARIES = libpwm.la
libpwm_la_CXXFLAGS = -Wall @RTAUDIO_CFLAGS@
libpwm_la_SOURCES = pwm.cpp RtAudio.cpp
libpwm_la_LDFLAGS = -export-dynamic -version-info @SO_VERSION@
EXTRA_DIST = pwm.h RtAudio.h RtError.h RtAudio-readme.txt README.md
endif
