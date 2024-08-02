
_LIBPATH = ./../../src/.libs/libmapper

targets := Mapper.NET.dll

if DARWIN_ARM64
targets += varargs_wrapper.dylib
endif

all-local: $(targets)

Mapper.NET.dll: Mapper.NET/*.cs
	-cp $(_LIBPATH).*.dylib ./libmapper.dylib
	-cp $(_LIBPATH).*.so ./libmapper.so
	-cp $(_LIBPATH).*.dll ./libmapper.dll
	dotnet publish -c Release -f netstandard2.1 -o bin Mapper.NET
	-cp bin/Mapper.NET.dll Mapper.NET.dll

varargs_wrapper.dylib: varargs_wrapper.s
	gcc -shared -o varargs_wrapper.dylib varargs_wrapper.s

MOSTLYCLEANFILES = *.dll *.exe *.dylib

EXTRA_DIST = varargs_wrapper.s Mapper.NET.sln README.md Demo/Demo.csproj Demo/Program.cs \
             Mapper.NET/Assets/libmapper_logo_black_512px.png Mapper.NET/Mapper.NET.csproj \
             Mapper.NET/Device.cs Mapper.NET/Graph.cs Mapper.NET/Map.cs Mapper.NET/MapperList.cs \
             Mapper.NET/MapperObject.cs Mapper.NET/Signal.cs Mapper.NET/Time.cs Mapper.NET/Types.cs
