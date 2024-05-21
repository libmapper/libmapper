
_LIBPATH = ./../../src/.libs/libmapper

all-local: Mapper.NET.dll varargs_wrapper.dylib

Mapper.NET.dll: Mapper.NET/*.cs
	-cp $(_LIBPATH).*.dylib ./libmapper.dylib
	-cp $(_LIBPATH).*.so ./libmapper.so
	-cp $(_LIBPATH).*.dll ./libmapper.dll
	dotnet publish -c Release -o bin Mapper.NET
	-cp bin/Mapper.NET.dll Mapper.NET.dll
	
varargs_wrapper.dylib: varargs_wrapper.s
	gcc -shared -o varargs_wrapper.dylib varargs_wrapper.s
	
MOSTLYCLEANFILES = *.dll *.exe *.dylib

EXTRA_DIST = Mapper.NET.dll varargs_wrapper.dylib