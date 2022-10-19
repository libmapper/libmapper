
_LIBPATH = ./../../src/.libs/libmapper

all-local: Mapper.exe test.exe testinstance.exe testmonitor.exe testvector.exe

Mapper.exe: Mapper.cs
	-cp $(_LIBPATH).*.dylib ./libmapper.dylib
	-cp $(_LIBPATH).*.so ./libmapper.so
	-cp $(_LIBPATH).*.dll ./libmapper.dll
	csc -unsafe -t:library Mapper.cs

test.exe: test.cs
	csc /r:Mapper.dll test.cs

testinstance.exe: testinstance.cs
	csc /r:Mapper.dll testinstance.cs

testmonitor.exe: testmonitor.cs
	csc /r:Mapper.dll testmonitor.cs

testvector.exe: testvector.cs
	csc /r:Mapper.dll testvector.cs

MOSTLYCLEANFILES = *.dll *.exe *.dylib

EXTRA_DIST = Mapper.cs README.md test.cs testinstance.cs testmonitor.cs testvector.cs

test_all_ordered = test.exe testinstance.exe testvector.exe

tests: all
	for i in $(test_all_ordered); do echo Running $$i; if ! mono $$i; then exit 1; fi; done
