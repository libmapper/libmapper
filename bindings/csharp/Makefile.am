
_LIBPATH = ./../../src/.libs/libmapper

targets := Libmapper.NET.dll

if DARWIN_ARM64
targets += varargs_wrapper.dylib
endif

all-local: $(targets)

Libmapper.NET.dll: Libmapper.NET/*.cs
	-cp $(_LIBPATH).*.dylib ./libmapper.dylib
	-cp $(_LIBPATH).*.so ./libmapper.so
	-cp $(_LIBPATH).*.dll ./libmapper.dll
	dotnet publish -c Release -f netstandard2.1 -o bin Libmapper.NET
	-cp bin/Libmapper.NET.dll Libmapper.NET.dll

varargs_wrapper.dylib: varargs_wrapper.s
	gcc -shared -o varargs_wrapper.dylib varargs_wrapper.s

MOSTLYCLEANFILES = *.dll *.exe *.dylib

EXTRA_DIST = varargs_wrapper.s Libmapper.NET.sln README.md Demo/Demo.csproj Demo/Program.cs \
             Libmapper.NET/Assets/libmapper_logo_black_512px.png Libmapper.NET/Libmapper.NET.csproj \
             Libmapper.NET/Device.cs Libmapper.NET/Graph.cs Libmapper.NET/Map.cs Libmapper.NET/MapperList.cs \
             Libmapper.NET/MapperObject.cs Libmapper.NET/Signal.cs Libmapper.NET/Time.cs Libmapper.NET/Types.cs
