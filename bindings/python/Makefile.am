clean-local:
	-@rm -vfr build libmapper/__pycache__ pypi

MOSTLYCLEANFILES = installed_files.log

install-exec-hook:
	if test -n "$(DESTDIR)"; then\
		$(PYTHON) setup.py install --root=$(DESTDIR) --prefix=$(prefix) --record=@top_builddir@/bindings/python/installed_files.log; \
	else \
		$(PYTHON) setup.py install --prefix=$(prefix) --record=@top_builddir@/bindings/python/installed_files.log; \
	fi

uninstall-hook:
	cat @top_builddir@/bindings/python/installed_files.log \
	  | awk '{print "$(DESTDIR)"$$1}' | xargs rm -vf

EXTRA_DIST = libmapper/__init__.py test.py testcallbacks.py testconvergent.py testgetvalue.py \
             testinstance.py testmapfromstr.py testmonitor.py testnumpy.py testrefcount.py \
             testreverse.py testvector.py tkgui.py pyproject.toml

test_all_ordered = test testcallbacks testconvergent testgetvalue testinstance testmapfromstr testnumpy \
             testreverse testvector

tests: all
	for i in $(test_all_ordered); do echo Running $$i; if ! python ./$$i.py; then exit 1; fi; done
