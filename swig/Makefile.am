
all-local: _mapper.so

$(builddir)/%_wrap.c %.py: %.i
	swig -I$(top_srcdir)/include -python -o $(builddir)/mapper_wrap.c \
		$(srcdir)/mapper.i

# Don't interfere with distutils CFLAGS
_%.so: $(builddir)/%_wrap.c
	env CFLAGS="" python setup.py build_ext
	-@mv -v build/lib.*/$@ .
	-@rm -rf build

clean-local:
	-@rm -vf _mapper.so mapper.py mapper_wrap.c

EXTRA_DIST = mapper.i tkgui.py
