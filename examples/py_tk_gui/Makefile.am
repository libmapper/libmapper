
all-local: _pwm.so ../pwm_synth/libpwm.la

%_wrap.c %.py: %.i
	swig -I../pwm_synth -I../include -c++ -python pwm.i

# Don't interfere with distutils CFLAGS
_%.so: %_wrap.c
	env CFLAGS="" python setup.py build_ext
	-@mv -v build/lib.*/$@ .
	-@rm -rf build

clean-local:
	-@rm -vf _pwm.so pwm.py pwm_wrap.c

EXTRA_DIST = pwm.i
