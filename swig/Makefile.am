
_CXX =  $(filter-out ccache,$(CXX))

all-local: _mapper.$(PYEXT)

$(builddir)/%_wrap.c %.py: %.i
	$(SWIG) -I$(top_srcdir)/include -python @PY3SWIG@ -modern -o $(builddir)/mapper_wrap.c \
		$(srcdir)/mapper.i

# Don't interfere with distutils CFLAGS
_%.$(PYEXT): $(builddir)/%_wrap.c
	env CFLAGS="" CXX="$(_CXX)" $(PYTHON) setup.py build_ext
	cp -v `./copywhich.sh $(PYTHON) $(@:.so=)` .

clean-local:
	-@rm -vf mapper.py mapper_wrap.c
	$(PYTHON) setup.py clean --all

MOSTLYCLEANFILES = _mapper*.$(PYEXT) mapper.py mapper_wrap.c installed_files.log

install-exec-hook: $(builddir)/mapper_wrap.c
	if test -n "$(DESTDIR)"; then\
		$(PYTHON) setup.py install --root=$(DESTDIR) --prefix=$(prefix) --record=@top_builddir@/swig/installed_files.log; \
	else \
		$(PYTHON) setup.py install --prefix=$(prefix) --record=@top_builddir@/swig/installed_files.log; \
	fi

uninstall-hook:
	cat @top_builddir@/swig/installed_files.log \
	  | awk '{print "$(DESTDIR)"$$1}' | xargs rm -vf

EXTRA_DIST = mapper.i tkgui.py copywhich.sh

test_all_ordered = test testcallbacks testconvergent testinstance testquery testqueue testreverse testvector

tests: all
	for i in $(test_all_ordered); do echo Running $$i; if ! python ./$$i.py; then exit 1; fi; done
