
_CXX =  $(filter-out ccache,$(CXX))

all-local: _pwm.$(PYEXT) $(top_builddir)/examples/pwm_synth/libpwm.la

$(builddir)/%_wrap.cxx $(buildir)/%.py: %.i
	$(SWIG) -I$(top_srcdir)/examples/pwm_synth -I$(top_srcdir)/include -c++ \
		-python -o $(builddir)/pwm_wrap.cxx $(srcdir)/pwm.i

# Don't interfere with distutils CFLAGS
_%.$(PYEXT): $(builddir)/%_wrap.cxx
	pwd
	cd $(builddir) && env CFLAGS="" CXX="$(_CXX)" python setup.py build_ext
	-@mv -v build/lib.*/$@ .
	-@rm -rf build

clean-local:
	-@rm -vf _pwm.$(PYEXT) pwm.py pwm_wrap.c

EXTRA_DIST = pwm.i tk_pwm.py
