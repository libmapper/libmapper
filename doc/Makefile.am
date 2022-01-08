
MAINTAINERCLEANFILES=Makefile.in 

CLEANFILES=doxygen-build.stamp

DOX=libmapper.doxyfile

EXTRA_DIST=bela_installation.md expression_syntax.md how_to_compile_and_run.md \
           windows_mingw.md \
           images/diagrams.md \
           images/dest_value_reference.png images/full_convergent.png \
           images/naive_convergent.png images/partial_vector_updates.png \
           images/shared_instance_pools.png \
           integration_examples/mediapipe_integration.md \
           integration_examples/images/landmarks_model.jpg \
           integration_examples/images/pong_scene.png \
           tutorials/tutorial_c.md tutorials/tutorial_cpp.md \
           tutorials/tutorial_csharp.md tutorials/tutorial_introduction.md \
           tutorials/tutorial_java.md tutorials/tutorial_python.md \
           tutorials/images/device_lifecycle.png tutorials/images/network_diagram.png

INSTIMAGES=html/doxygen.png

DOC_STAMPS=html-build.stamp

DOC_DIR=$(HTML_DIR)

all-local: doxygen-build.stamp

doxygen-build.stamp: $(DOX) $(top_srcdir)/src/*.c $(top_srcdir)/src/*.h \
	$(top_srcdir)/include/mapper/*.h
	@echo '*** Running doxygen ***'
	doxygen $(DOX)
	touch doxygen-build.stamp

clean-local:
	rm -f *~ *.bak $(DOC_STAMPS) || true
	if test -d html; then rm -fr html; fi
	if test -d latex; then rm -fr latex; fi
	if test -d man; then rm -fr man; fi

distclean-local: clean
	rm -f *.stamp || true
	if test -d html; then rm -rf html; fi
