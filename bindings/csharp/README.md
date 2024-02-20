# C# Bindings for libmapper


## Building C# wrapper library

Run the following command from within the bindings/csharp directory:

~~~
csc -unsafe -t:library Mapper.cs
~~~

This should produce a file named `Mapper.dll`

## Building the test example

Run the following command from within the bindings/csharp directory:

~~~
csc /r:Mapper.dll test.cs
~~~

This should produce a file named `test.exe`

## Running the test example

Run the following command from within the bindings/csharp directory:

~~~
mono test.exe
~~~

You may need to copy the libmapper dynamic library into the same directory (depending on dynamic linker path configuration).

## Notes

Constructing Maps using a format/expression string and Signal arguments does not currently work on Apple Silicon due to architectural differences in how arguments to variadic functions are stored (stack vs registers). For now we recommend against using this constructor if you want your code to be fully cross-platform.

`public Map(string expression, params Signal[] signals)`

## To Do

* add handlers for specific types to avoid typecasts and allow compilation of tests without `/unsafe`
* interact with C# Events
* add more tests
