CXXFLAGS = -D__LINUX_ALSA__ -DRAWWAVE_PATH=\"/usr/share/stk/rawwaves\" \
	$(shell pkg-config --cflags libmapper-0)
LDFLAGS = -lstk $(shell pkg-config --libs libmapper-0)

synth_Cmapper: synth_Cmapper.o