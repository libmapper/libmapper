
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
