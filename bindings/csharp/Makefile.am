
_LIBPATH = ./../../src/.libs/libmapper

all-local: Mapper.exe test.exe testinstance.exe

Mapper.exe: Mapper.cs
	-cp $(_LIBPATH).*.dylib ./libmapper.dylib
	-cp $(_LIBPATH).*.so ./libmapper.so
	-cp $(_LIBPATH).*.dll ./libmapper.dll
	csc -unsafe -t:library Mapper.cs

test.exe: test.cs
	csc /r:Mapper.dll test.cs

testinstance.exe: testinstance.cs
	csc /r:Mapper.dll testinstance.cs

MOSTLYCLEANFILES = *.dll *.exe *.dylib

EXTRA_DIST = Mapper.cs README.md test.cs testinstance.cs

test_all_ordered = test.exe testinstance.exe

tests: all
	for i in $(test_all_ordered); do echo Running $$i; if ! mono $$i; then exit 1; fi; done
