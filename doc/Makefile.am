
MAINTAINERCLEANFILES=Makefile.in 

CLEANFILES=doxygen-build.stamp

DOX=libmapper.doxyfile

EXTRA_DIST=expression_syntax.md how_to_compile_and_run.md windows.md	\
           tutorials/tutorial_c.md tutorials/tutorial_introduction.md	\
           tutorials/tutorial_maxmsp_central.md                         \
           tutorials/tutorial_maxmsp_multiobj.md                        \
           tutorials/tutorial_pure_data.md tutorials/tutorial_python.md \
           tutorials/tutorial_java.md

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
