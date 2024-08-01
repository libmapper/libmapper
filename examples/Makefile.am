
SUBDIRS =

if HAVE_AUDIO
SUBDIRS += pwm_synth

bin_PROGRAMS = pwm_example
pwm_example_CXXFLAGS = -Wall -I$(top_srcdir)/include $(liblo_CFLAGS)
pwm_example_SOURCES = pwm_example.cpp
pwm_example_LDADD = pwm_synth/libpwm.la \
	$(top_builddir)/src/libmapper.la \
	@RTAUDIO_LIBS@ $(liblo_LIBS)
endif

EXTRA_DIST = pyo_example.py pyo_example_instanced.py pyside_gui_label_example.py \
             pwm_synth/pwm.h pwm_synth/RtAudio-LICENSE.txt pwm_synth/RtAudio-README.md \
             pwm_synth/RtAudio.h
