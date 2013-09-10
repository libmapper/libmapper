
all-local: _mapper.$(PYEXT)

$(builddir)/%_wrap.c %.py: %.i
	$(SWIG) -I$(top_srcdir)/include -python -o $(builddir)/mapper_wrap.c \
		$(srcdir)/mapper.i

# Don't interfere with distutils CFLAGS
_%.$(PYEXT): $(builddir)/%_wrap.c
	env CFLAGS="" python setup.py build_ext
	cp -v `./copywhich.sh $@` .

clean-local:
	-@rm -vf mapper.py mapper_wrap.c
	python setup.py clean --all

MOSTLYCLEANFILES = _mapper.$(PYEXT) mapper.py mapper_wrap.c installed_files.log

install-exec-hook: $(builddir)/mapper_wrap.c
	if test -n "$(DESTDIR)"; then\
		python setup.py install --root=$(DESTDIR) --prefix=$(prefix) --record=@top_builddir@/swig/installed_files.log; \
	else \
		python setup.py install --prefix=$(prefix) --record=@top_builddir@/swig/installed_files.log; \
	fi

uninstall-hook:
	cat @top_builddir@/swig/installed_files.log \
	  | awk '{print "$(DESTDIR)"$$1}' | xargs rm -vf

EXTRA_DIST = mapper.i tkgui.py
