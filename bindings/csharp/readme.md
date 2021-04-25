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
csc /unsafe /r:Mapper.dll test.cs
~~~

This should produce a file named `test.exe`

## Running the test example

Run the following command from within the bindings/csharp directory:

~~~
mono test.exe
~~~

You may need to copy the libmapper dynamic library into the same directory (depending on dynamic linker path configuration).

## To Do

* add handlers for specific types to avoid typecasts and allow compilation of tests without `/unsafe`
* add scripts for automatically building bindings
* wrap remaining libmapper API
    * lists
    * object properties
    * graphs
    * signal instances
* interact with C# Events
* add more tests
