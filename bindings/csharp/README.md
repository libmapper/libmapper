# C# Bindings for libmapper


## Building C# wrapper library

Run the following command from within the bindings/csharp directory:

~~~
dotnet publish -c Release -o . Libmapper.NET
~~~

This should produce a file named `Libmapper.NET.dll`, as well as some debugging symbols.

## Building the test example

Run the following command from within the bindings/csharp directory:

~~~
dotnet publish -c Release -o testbin Demo
~~~

This should produce an executable in the `testbin` directory named `Demo`

## Running the test example

> [!IMPORTANT]  
> If you're on Apple Silicon, you'll need to compile another library first. See the Notes section below.

Run the `Demo` executable from the `testbin` directory.

You may need to copy the libmapper dynamic library into the same directory (depending on dynamic linker path configuration).

## Notes

Due to how Apple Silicon handles variadic arguments (and C#'s lack of support for variadic arguments on non-Windows platforms), 
the constructor `Map(string, Signal[])`, used for creating a map from an expression string, requires another dynamic library to act as a wrapper
around the variadic call. The code for this library is contained in `varargs_wrapper.s`.

If you don't plan on using that constructor, you can stop reading now. Else, you'll need to compile the wrapper library:
```bash
$ clang -shared -o varargs_wrapper.dylib varargs_wrapper.s
```

Either put `varargs_wrapper.dylib` in your library search path or in the same directory as your executable.

## To Do

* add handlers for specific types to avoid typecasts and allow compilation of tests without `/unsafe`
* interact with C# Events
* add more tests
