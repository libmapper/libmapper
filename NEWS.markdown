# libmapper NEWS

version 2.4.14
--------------

We are pleased to announce the release of version 2.4.14 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release is focused on bugfixes, improvements to the expression engine and test suite, and documentation.

Bug fixes and improvements:

- Convergent maps built with remote signals now return the original map pointer after handshaking.
- Remote devices now automatically set `STATUS_ACTIVE` bitflag.
- Added helper function `mpr_obj_get_prop_as_dbl()` to public API.
- Enabled casting from `MPR_TIME` type to other typed property getters.
- Internal improvements to the `mpr_bitflags` and `mpr_value` data structures and methods.
- Added local bundle handling as a message destination to `network.c` for more efficient handling of local maps.
- Fixed handling of write-protected object properties.
- Fixed timestamp behaviour for devices which only update signals through local maps.

Expression engine:

- Added vector `product()` and `reverse()` functions.
- Added exponential moving deviation function `emd()`.
- Optimized vector `mean()` function.
- Added a `pass()` function for e.g. rounding integers.
- Detect divide-by-zero and out-of-bounds arguments in evaluator.
- Detect evaluation errors during precomputation of expression fragments rather than over whole expression.
- Use sub-expression evaluation errors detection to simplify `linear()` macro expansion.
- Limited errno checking to EDOM.
- Improved trace messages.
- Fixed vector construction bug in parser.
- Enabled retrieval of current instance index in expression mini-language.
- Fixed possible reference to most recent input by TOK_VAR_NUM_INST.
- Fixed parser fail when operand was a reduce expression; added test.

Test suite:

- Added additional tests for `MPR_TIME`-typed properties and write-access to `testprops.c`
- Reversed configuration order in `testconvergent.c` to test map handshaking from 3rd party graph before signals are known.
- Added tests for inverse trig functions including out-of-bounds argmuments to `testparser.c`
- Separated tracking of parser and evaluator failure in `testparser.c`.
- Fixed memory leak when checking list-typed properties in `testprops.c`.
- Fixed possible `powf` and int overflows in `testparser.c`.
- Use polling arg `-1` when running `testconvergent.c` in fast mode.
- Added configuration to `testconvergent.c` to test per-element convergent mapping.
- Fixed naming in several tests.

Version 2.4.13
--------------

We are pleased to announce the release of version 2.4.13 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release focuses on updates to the C# bindings: refactoring, documentation, and config scripts.

Additional changes:

- Fix for removal of private properties from non-local objects; added more checks to testprops.
- Java bindings: added missing Device method updateMaps().
- Updated tutorials.
- Include mediapipe example scripts when making dist.
- Tweaks to signal instance management

Version 2.4.12
--------------

We are pleased to announce the release of version 2.4.12 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

Well that was quick! This release primarily fixes an important bug in the implementation of the map handshake protocol. When initiated by a third-party Graph object (e.g. from a session manager such as [WebMapper](https://github.com/libmapper/webmapper)), the first map between any two unlinked devices would succeed but subsequent maps would fail on the first attempt to establish them. Maps created programmatically using local resources would succeed an all these cases.

This buggy behaviour seemed annoying enough to warrant a release.

Version 2.4.11
--------------

We are pleased to announce the release of version 2.4.11 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release focuses on further improvements and fixes for signal instance management, particularly when mapping between ephemeral and persistent signals.

- Proxy `id_maps` are now used internally to enable persistent signals to maintain their global identifiers while still allowing control by upstream ephemeral signals.
- When a map is released the map destination will attempt to release all active signal instances that originated with the map source signal(s). This involves slightly delaying complete removal of the destination-side map structure until after the source as stopped sending updates that might re-activate destination instances.
- Handshake `/unmap` messages now include the map version property in order to handle a race condition when a map is released and then recreated before cleanup as completed.

Additional changes:

- Various updates to improve safety and stability when using libmapper in multithreaded projects.
- Fixes for updating object versions during their lifetime.

Tests:

- added `testremap.c` to test suite

Version 2.4.10
--------------

We are pleased to announce the release of version 2.4.10 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release provides important fixes for signal instance management when the instance `status` API is used rather than registering callbacks. It also contains changes to the C# bindings in preparation for publishing to nuget.

Fixes:

- adding missing files to dist
- fixed memory alignment of objects stored in a `mpr_list`
- fixes for instance menagement when callbacks are not used and when instances are modified by the map expression.

Refactoring:

- refactoring expression engine

Bindings:

- renamed Mapper.NET -> Libmapper.NET, added README, license in preparation for publishing on Nuget

Tests:

- added `testinstance_no_cb.c` to test suite

Version 2.4.9
-------------

We are pleased to announce the release of version 2.4.9 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release provides several fixes and optimizations that make libmapper more stable and faster. Internally there has also been some refactoring to improve maintainability and support future development.

Fixes:

- Fixed inconsistent `mpr_bitflags` usage when calling `mpr_expr_eval()`.
- Added more length/type checking for static object properties.
- Ensure slot values are reset appropriately so that active instance count is accurate.

Optimization:

- Store local maps at the front of the list to save iterating over non-local maps during processing.
- Store device flag indicating whether child signals have been modified; use flag to decide when to update subscribers.

Refactoring:

- Removed alloca() call in `map.c`.
- Moved threaded polling into `network.c` to match earlier shared-graph work.
- Renamed internal expression structures and variables.
- Refactored `expression.c` to split into multiple smaller files.

Documentation:

- Added line documenting output of `schmitt()` function in expression syntax.

Tests:

- `testinstance.c`: added additional test configurations; print instance statuses.

Version 2.4.8
-------------

We are pleased to announce the release of version 2.4.8 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release provides minor updates to verion 2.4.7, fixing an issue with building Python wheels that prevented releasing that version to PyPI. Version 2.4.8 also adds two new `operator` constants (`MPR_OP_BAND` and `MPR_OP_BOR`) and enables status bitflag updates to the `mpr_graph` object type.

Version 2.4.7
-------------

We are pleased to announce the release of version 2.4.7 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release includes new bindings for the C# programming language, unified oject status flags, improved support for shared graphs, and many small bugfixes and improvements. For more detailed information see below.

Note: This version of libmapper depends on liblo v0.32 or greater.

API changes:

- Moved `mpr_time_print()` to public API
- Unified object status bitflags for use across all libmapper objects
- Added functions `mpr_obj_get_status()` and `mpr_obj_reset_status()`
- Added function `mpr_sig_get_inst_status()` for retrieving the status of a signal instance, including whether it has been updated locally/remotely or released remotely since the last time the status was checked. This functionality enables more sophisticated handling of instanced signals and maps without using callbacks.
- Devices that share a graph may now be polled all at once by calling `mpr_dev_poll()` on any of them, or by calling `mpr_graph_poll()` on the shared graph object.
- Added support and documentation for passing a negative `block_ms` argument to polling functions. This will case the poll function to process all messages currently in the queue and then return immediately.

Updates:

- Fixed error in the path generated by `pkg-config --cflags` after libmapper has been installed
- Fixes for overlapping memcpy released by Valgrind profiling
- New graph list items are now appended instead of prepended to support processing remote updates in the same order they were instantiated
- Fix: Reset signal property table dirty flag after sharing updates from `mpr_dev_poll()`. Reduces redundant network traffic and seems to speed up tests.
- Fix for osc bundling of instance release messages
- Accept string arguments for boolean object properties in osc messages.
- Fix for handling id_map retrieval when non-ephemeral signals are updated remotely.
- Expression engine:
    - feature: added syntax for referencing the 'newest' signal in convergent maps
    - feature: allow calling reduce expressions on variable timetags.
    - fix: ensure vectors of random values are initialized with the correct length
    - fix: expression table refactored for windows-arm64 workaround
    - fix: ensure references to user-variable timetags are properly mapped to variable list if they occur in the expression string before a non-timetag reference
    - fix for timetag interpolation bug
- Refactoring: Moved all server management to `network.c` and wait efficiently on all OSC servers simultaneously.
- Refactoring: `mpr_value` is now an opaque type.
- Refactoring: increase usage of `mpr_bitflags` and `mpr_value` types.

Documentation:

- added document [Understanding Instanced Signals and Maps](./doc/understanding_instances/understanding_instances.md)

Bindings:

- C#: bindings were rewritten
- Python: added testmonitor.py
- Java: added handling of string array property values.

Test suite:

- Added teststealing.c
- added value checking to testspeed.c

Version 2.4.6
-------------

We are pleased to announce the release of version 2.4.6 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release focuses on a bugfix for processing instance reduce functions, revision of the class structure for object-oriented language bindings (Python, Java, C#), and improvements to documentation.

Changes include

- Updated the class structure and documentation for object-oriented bindings (Python, Java, and C#) to improve consistency and clarity.
- Added the `median()` function to the expression language
- Enabled comparison by either object ids or memory locations when filtering lists.
- Added of license metadata to `pyproject.toml`
- Documentation:
    - Rearranged sections in the expression syntax documentation to improve readability.
    - Updated API documentation
    - Updated expression syntax documentation.
    - Added docstrings for Python bindings.
    - Added MediaPipe face tracking example to doc/integration_examples
    - Added exit handler with dev.free() to Java/Processing example.
- Testing:
    - Improved type checking in testparser.c
    - Updated testlist to avoid false failures when other libmapper-enabled programs are running on the network.
    - Added argument to testparser for running a specific test expression instead of the whole set.

Bug fixes include:

- Ensuring non-local links are removed from device records when links are removed from the graph.
- Switched to 64-bit type for comparing mpr_ids when filtering lists.
- Expression parser:
    - Moved timetag token into enum range eligible for typecasting.
- Expression evaluator:
    - Fixed an instance reduce regression.
    - Fixed typecasting and vector length tracking in expression engine
- Python bindings:
    - Fixed instance allocation count in signal constructor.

Version 2.4.5
-------------

We are pleased to announce the release of version 2.4.5 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This version focuses on support for network interface switching on Windows. There have also been several bug fixes and improvements to API documentation.

Bug fixes include:

- Switching network interfaces is now enabled on Windows. `mpr_graph_set_interface()`/`mpr_graph_get_interface()` now assume use of human-friendly network interface names on Windows.
- `mpr_graph_set_interface()` now only returns success if the interface was actually changed. 
- Fixes for memory handing of device hostname and url strings.
- Valid length or value arguments are no longer required when filtering a list with the MPR_OP_EX or MPR_OP_NEX operators.

Additional changes include:

- API documentation improvements.
- expression syntax documentation improvments.
- added value-checking to `testspeed.c`
- added two tests to the testing suite: `testlist.c` and `testsetiface.c`

Version 2.4.4
-------------

We are pleased to announce the release of version 2.4.4 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This version focuses on bug fixes, documentation improvements, and improvements to the Python bindings.

Bugfixes and improvements include:

- Fixed bug affecting retrieval-by-index for ordered mpr_lists (e.g. map source signals)
- Fixed bug adjusting link num_maps property on map removal.
- Fixed forwarding of received '/map' messages as 'map/modify' for existing maps in the case where the endpoints are on different devices and process location is MPR_LOC_SRC.

Usability improvements include:

- Revised list property filter to support `MPR_OP_ANY`/`MPR_OP_NONE` flags.
- Added check for ignoring unknown local map ids during map handshaking.
- Improvements to expression syntax documentation.
- Documentation improvements.

Improvements to the Python bindings include:

- Various fixes for wheel-building CI
- Added script and support files for building distribution for PyPI
- Improvements to object property getters.
- Converted Operator Enum to IntFlag to enable combinations with `MPR_OP_ANY`/`MPR_OP_NONE`.
- added `__contains__()` method to Object Lists to enable e.g. `if key in list`.

Version 2.4.3
-------------

We are pleased to announce the release of version 2.4.3 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This version includes documentation improvements, expression engine optimisations, and the results of testing on macOS/arm64 with one related bugfix detailed below. Support for local (private) object properties was added to the C++ and Java bindings.

A basic add-on for using libmapper within Blender scripting was also added to the `integration_examples` folder.

Bigfixes include:

- Fix for use of variadic functions by Python bindings on macOS/arm64
- Revision of `mpr_dev_poll()` so that blocking calls return after the expected time when device is not yet registered
- Fix for bug when returning `published` property of object properties

Version 2.4.2
-------------

Released 24 July 2023

We are pleased to announce the release of version 2.4.2 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This version focuses on bugfixes, continuous integration, usability and language bindings for Python and C#.

Bugfixes include:

- Fix for potential crash when editing switching network interfaces.
- Ensuring the graph handler is called when map sources are updated during construction of convergent maps.
- Fixes for convergent map handshake when initiated by a 3rd-party (non-local) graph.
- Fixes for graph ownership tracking in mpr_dev objects.

Usability improvements include:

- Private object properties are now applied immediately (rather than staging for `push`) since they will not be pushed to the graph anyway.
- The `mpr_list` functionality was upgraded to support generating ordered query results. Calls to `mpr_map_get_sigs()` will now result in source signal lists with the same order (alphabetical) as used internally and in map expression syntax.

Language bindings:

- Python bindings: handle bool properties and non-standard status values.
- Python bindings: test fixes, demo of cleanup on sigint.
- C# bindings: added support for private object properties.

Version 2.4
-----------

Released 1 June 2023

We are pleased to announce the release of version 2.4 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This version focuses on bugfixes and internal refactoring to support development of new features in the future.

Version 2.3
-----------

Released 6 September 2022

We are pleased to announce the release of version 2.3 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This version introduces [numpy](https://numpy.org/) array-typed signals in the Python bindings, in addition to reduction in memory usage for signal extrema properties (min and max) and improvements to Windows-related build scripts and documentation.

Version 2.2
-----------

Released 1 September 2022

We are pleased to announce the release of version 2.2 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

In addition to the improvements detailed below, this release contains a large number of small improvements and fixes, including improvements to build and CI scripts, documentation, tutorials, and examples.

## Core changes

1. The enum value `MPR_STATUS_ALL` was renamed to `MPR_STATUS_ANY` to better reflect its use.
2. The maximum admin-bus OSC bundle length was reduced to prevent dropped packets.
3. A backup attempt to complete map handshaking was added before staged maps are cleaned up. This means that new maps will be pushed to the network even if `push()` is not called. It is still recommended to call `push()` on new maps to start the handshake protocol immediately.
4. Devices sharing a graph now operate properly if they have identically-named signals. 
5. The value of remote signals can now be set directly without using a map by calling `set_value()` as you would for a local signal. The update is sent as a change in metadata rather than over a map.
* Two functions were removed from the public API:
    1. `mpr_graph_print()`, which duplicated `mpr_obj_print()` since `mpr_graph` can be cast to `mpr_obj`
    2. `mpr_map_get_sig()`, which was essentially a shortcut for `mpr_map_get_sigs()` -> `mpr_list_get_idx()`, and was inconsistent with other object getters in the API.

## Expression engine

This update enables the use of variable and/or subexpression indices for vector elements, historical samples, and input signals. It also enables  use of subexpressions for initializing reduce function accumulators, which previously were limited to literals.

The new vector function `sort()` enables sorting vectors in either ascending or descending order. The new reduce function `concat()` acts to concatenate values from the specified reduce dimension (e.g. signal instances) into a new vector. Used together, these new functions allow the creation of simple arpeggiators within a map expression.

## C# bindings

Functional C# bindings for libmapper are now available in the `/bindings` directory. Please refer to the C# tutorial for more information.

## Python bindings

The Python bindings were switched from Swig to ctypes, and the Python libmapper API was modified to match the [PEP 8 style guide](https://peps.python.org/pep-0008/). As part of the update, various improvements have been made to the API and underlying implementation. Improvements were also made to scripts for packaging and distributing the Python bindings.

## Java bindings

The Java bindings for libmapper have been cleaned up and improved to match advances on other object-oriented bindings (Python, C++, C#). This should result in an API that is more consistent and easier to use.

Version 2.1
-----------

Released 8 Nov 2021

We are pleased to announce the release of version 2.1 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release builds on the large scale changes introduced in version 2.0 while fixing bugs and improving memory use, testing, and documentation. Version 2.1 is also now compliant with the C90 standard (ISO/IEC 9899:1990). This change along with other modifications now enables CMake Visual Studio solution generation for quick compiling on Windows. Refer to the updated Windows build instructions for more information.

## Memory Improvements

Stack memory used by libmapper was reduced through profiling and many small modifications, including a switch to preallocated memory for the expression evaluation stack and optimizing the expression stack size. Tests on ESP32-based embedded platforms found an increase from a maximum of 6 signals under version 2.0 to around 150 signals before encountering memory issues.

## Shared Graphs

Multiple devices can now share a graph and network data structure. Currently each device needs to be polled individually in order to process incoming signal updates on device-specific servers, which inefficiently polls the shared graph and network resources multiple times. This optimization is left to a future release.

## Multithreading

To better support multithreaded devices with interrupt-driven signal updates, we now use a circular buffer for outgoing OSC bundles. This ensures that devices are not trying to add messages to the bundle during a call to `mpr_dev_poll()`.

## Expression Engine Improvements

This release includes many improvements to the expression parser and evaluator, including more efficient expression representations, new functions, differentiatiation between instanced and global variables, delay interpolation, variable delays, dot syntax support, extension of reduce functions to operate across instances, historical samples, and input signals, and user-defined reduce functions.

### Delay Interpolation & Variable Delays

We now support delay interpolation (using a float for the delay length) and variable length delays. For variable delays a second argument must be included to indicate the maximum delay and allow buffer allocation. The current syntax is summarized in the table below:

Description           | Expression
----------------------|-------------
simple unit delay     | `y = x + y{-5};`
interpolated delay    | `y = x + y{-5.3};`
variable length delay | `y = x + y{x/10, 50};`

### Dot Syntax

Optional dot syntax was enabled for functions that operate across elements of vectors

`()`notation | `.`notation | description
-------------|-------------|------------
any(x)       | x.any()     | output 1 if any of the elements of vector x are non-zero, otherwise output 0
all(x)       | x.all()     | output 1 if all of the elements of vector x are non-zero, otherwise output 0
sum(x)       | x.sum()     | output the sum of the elements in vector x
mean(x)      | x.mean()    | output the average (mean) of the elements in vector x
max(x)       | x.max       | output the maximum element in vector x
min(x)       | x.min()     | output the minimum element in vector x
center(x)    | x.center()  | output the midpoint between x.min() and x.max()
norm(x)      | x.norm()    | output the length of the vector x
angle(a, b)  |             | output the angle between vectors a and b
dot(a, b)    |             | output the dot product of vectors a and b

### Convergent map syntax

The syntax for indicating input signal indexes for convergent maps has been modified from `xN` to `x$N` for consistency with other indices.

### Reduce Functions

The vector 'reduce' functionality mentioned above has been extended to allow reduction across any of four different dimensions:

* vector elements
* historical samples
* signal instances
* input signals (convergent mapping)

This reduction can be accomplished either by using the convenience functions listed above (e.g. `mean()`) or by writing a custom `reduce()` function. These reduce expressions can be nested for reduction across multiple dimensions. Please refer to the expression syntax documentation for more information and examples.

## Instance Management

Since version 0.3, signal *instances* can be used to represent, map, and process signals that correspond to phenomena that are [naturally multiplex and/or ephemeral](https://ieeexplore.ieee.org/document/8259406). This release separates `multiplicity` and `ephemerality` into independent signal properties in order to control proper behaviour under some edge scenarios such as "ephemeral singleton" and "static multiplex" signals. Default behaviour should not change but the properties can now be modified separately using `mpr_obj_set_prop()`

## C# Bindings

This release introduces functional C# bindings. While they are not yet feature-complete, they are sufficient for many scenarios.

## Testing Suite

Expression parser/evaluator testing has been increased to test 102 expressions in order to cover new features and bugfixes mentioned above including fractional delays, reduce functions, vector optimisation, integer divide-by-zero within reduction loops, and extraneous commas.

Version 2.0
-----------

Released 5 November 2020

We are pleased to announce the release of version 2.0 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

This release includes large scale simplification and reorganisation of the libmapper C API, making it smaller, more consistent, and easier to use. In brief, this release:

* reduces public high-level object types from 8 to 6 (-25%)
* reduces public functions from 261 to 78 (-70%)
* reduces lines of code (LoC) from ~19.5K to ~14K (-28%)
* reduces handler typedefs from 8 to 2 (-75%)

The release also includes improvements to cleanup of partially-constructed maps and fixes for various small memory leaks.

### Data structures:

Two first-class data structures (`Links` and `Map Slots`) were removed, and the Database and Network data structures were combined to form a new `Graph` data structure. All of the first-class data structures may be safely cast to the new generic type `mpr_obj` for the purpose of property management (described below).

<table style="width:100%;">
<tr><th>v1.2</th><th>v2.0</th></tr>
<tr><td>Network</td><td rowspan=2>Graph</td></tr>
<tr><td>Database</td></tr>
<tr><td>Device</td><td>Device</td></tr>
<tr><td>Signal</td><td>Signal</td></tr>
<tr><td>Link</td><td style="background:red">removed</td></tr>
<tr><td>Map</td><td>Map</td></tr>
<tr><td>Slot</td><td style="background:red">removed</td></tr>
</table>

### Unified Handlers:

This release eliminates three handler typedefs and merges the remaining 5 previous typedefs into only 2 handler types as shown in the table below:

<table style="width:100%;">
<tr><th>v1.2</th><th>v2.0</th></tr>
<tr><td> mapper_signal_update_handler</td><td rowspan=2>mpr_sig_handler</td></tr>
<tr><td> mapper_instance_event_handler</td></tr>
<tr><td>mapper_database_device_handler</td><td rowspan=3>mpr_graph_handler</td></tr>
<tr><td>mapper_database_signal_handler</td></tr>
<tr><td>mapper_database_map_handler</td></tr>
<tr><td>mapper_database_link_handler</td><td style="background:red">removed</td></tr>
<tr><td>mapper_device_link_handler</td><td style="background:red">removed</td></tr>
<tr><td>mapper_device_map_handler</td><td style="background:red">removed</td></tr>
</table>

### Property setters and getters:

Many object/property-specific setters and getters were removed and replaced with generic setters and getters applied to the `mpr_obj` data structure which is inherited by `mpr_dev`, `mpr_sig`, `mpr_map` and `mpr_graph`. Properties can be retrieved by name, numeric identifier, or index, and new convenience functions were added for retrieving specific data types, e.g. `mpr_obj_get_prop_as_int32()`.

### Protocol changes:

The under-used multi-count signal update feature has been removed from the signal update function in order to simplify signal updaters and handlers. This functionality will be handled in the future by making update bundling a modifiable map property and buffering signal updates as necessary.

User-defined expression variables are now included as map properties using the syntax `@var@<variable_name>`. The values of these variables can be modified without recompiling the map expression by editing the property.

## Calibration and Linear Scaling

The map `mode` property was completely removed in this update; basically all maps are now permanently in `expression` mode. In order to support simple linear scaling and calibration we have added a `linear` macro to the expression parser. This macro accepts 5 arguments, the first being the name of the input variable (e.g. `x`) and the others providing the min and max values for the input and output, respectively, e.g.

~~~ c
linear(<src>, <srcMin>, <srcMax>, <dstMin>, <dstMax>)
~~~

Replacing either or both of the source extrema arguments with the `?` character will cause this value to be calibrated live from the input values; replacing any of the extrema values (source or destination) with the character `-` will attempt to use the previously stored extrema values for the associated signal.

~~~ c
// apply a linear expression to a map
snprintf(expr, len, "y=linear(x,%f,%f,%f,%f)", sMn, sMx, dMn, dMx);
mpr_obj_set_prop(map, MPR_PROP_EXPR, NULL, 1, MPR_STR, expr, 1);

// enable calibration
snprintf(expr, len, "y=linear(x,?,?,%f,%f)", dMn, dMx);
...

// keep linear scaling but disable calibration
snprintf(expr, len, "y=linear(x,-,-,-,-)");
...
~~~

## New Feature: Efficient Local Mapping

This release introduces more efficient processing of "local" maps which connect a source signal to a destination signal belonging to the same device. These maps previously used the local loopback network interface. With the new improvements, internal device handlers are called directly without involving the network. These device handler deal with some fairly complex instance logic, so we don’t call user-defined signal handlers directly. The old networked approach served to mitigate some potential issues such as infinite map loops and even fork bombs since it throttled the number of messages processed per timestep. Since direct function calls don't provide this mitigation, map loops are now detected using signal locks.

## Signal Instancing Improvements

This release includes improvements to signal instance stealing logic and adds new support for convergent maps with heterogeneous instancing. The property `use_inst` was added to the `map` data structure. In-map instance management functionality was also added, enabling managing instances from a singleton source signal using conditionals (e.g. jabs, trajectories, segmentations), and overriding default instance lifetimes. The special expression variables `alive` and `muted` were added to enable this control.

### Testing Suite

`testquery.c` was removed since the signal update bundling functionality was removed in favour of a future configurable map property. `testselect.c` was removed due to API changes, internal use of `select()` for waiting on device servers, and planned future shared-graph functionality. `testdatabase.c` was renamed to `testgraph.c` for consistency with the data structure renaming described earlier.

Two new tests were added to the suite:

* `testcalibrate.c` tests the new linear calibration expression syntax in this release.
* `testlocalmap.c` tests the efficient local mapping feature described above, including a test of update loop detection and avoidance.

Other tests were updated and improved:

* `testinstance.c` now tests 32 different configurations of signal instancing, map topology, processing location, and overflow handling.
* `testparser.c` now includes 61 tests (up from 51), randomizes inputs & tests results of expression evaluation

Value checking was added to: `testexpression.c`, `testlinear.c`, `testparser.c`, `testrate.c`, and `testvector.c`.

Version 1.2
-----------

Released 4 March 2019

We are pleased to announce the release of version 1.2 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

Changes of note since the 1.1 release include

* Improvements to the *map slot* API
* Added the ability to configure maps at runtime to use either UDP or TCP for transport.
* Improvements to the device name allocation process.
* Changes to the C++ and Java device queue API to match the approach used for the Python bindings.
* Fixes for callback thread handling in the SWIG/Python bindings.
* Fixes and improvements for handling of expired devices by the Database.
* Updated the makefile for Java bindings to use `javac -h` since `javah` is no longer available on current versions of the JDK.
* Updates to tutorials, examples and test programs.

Bugfixes:

* Prevent user code from rewriting map ids.
* Ensure devices are marked as subscribed if a subscription is added manually during the temporary subscription window.
* Fixed initialisation of the device ordinal property.
* Fixed a timetag subtraction bug.
* "/map" messages targeting existing maps are now forwarded correctly.

Version 1.1
-----------

Released 6 October 2017

We are pleased to present release 1.1 of libmapper, an open-source, cross-platform software library for declaring data signals on a shared network and enabling arbitrary connections to be made between them. The main focus of libmapper development is to provide tools for creating and using systems for interactive control of media synthesis.

Changes of note since the 1.0 release include:

* Updating the libmapper codebase to use the most recent version of [liblo](http://liblo.sourceforge.net/).
* Addition of functionality for handling *signal groups*.
* Simplified and updated documentation.
* More informative debugging messages.
* Improved handling of database subscriptions.
* More efficient batch-sending of device, signal and map metadata.
* Addition of a default signal direction property for database records, ensuring that the record can be retrieved from the database while waiting for updated metadata.
* Added simple check for map feedback loops,.
* Updated tests and tutorials.
* Extension and simplification of the C++ headers, along with new API documentation.
* Extension of the SWIG/Python bindings to support *convergent maps*.

Bugfixes include:

* Fix for integer size warnings when printing mapper_id values.
* Fix for duplicate map records in database associated with calls to the `mapper_map_new()` function using non-local signals.
* Removal of stray references to libmapper-0.
* Fix for user-defined variables in map expressions.
* Fix for reporting sub-connections of convergent maps.
* Fixes for initializing, updating and reporting various device, link and slot properties.
* Ensure expression update triggers report to subscribers.
* Ensure temporary device subscriptions are not interpreted as local devices.
* Fix for possible oversized OSC bundles.
* Ensure autorenewing subscriptions are actually renewed when database poll block time is short.
* Fix for occasional false detection of duplicate signals when constructing maps.
* Treat properties named 'maximum' and 'minimum' as aliases for 'max' and 'min'.
* Database autosubscribe flags were overwriting configurations of user-code subscription created in device callback.
* Fix for input history memory allocation in expression parser.

Version 1.0
-----------

Released Jan 4, 2016.

This release includes many API changes, improvements, and new
features since version 0.4. They are summarized very briefly here:

### Structural changes

The data structures in libmapper have been reconceived to increase consistency
and clarity. Some data structures have been merged and/or renamed:

* `mapper_device` + `mapper_db_device` ––> `mapper_device`
* `mapper_signal` + `mapper_db_signal` ––> `mapper_signal`
* `mapper_link` + `mapper_db_link` ––> `mapper_link`
* `mapper_connection` + `mapper_db_connection` ––> `mapper_map`
* `mapper_admin` ––> `mapper_network`
* `mapper_monitor` ––> (removed)
* `mapper_db` ––> `mapper_database`

This means that the database now represents remote devices and signals using the
same data structures that are used for local devices and signals. Property
getter and setter functions for these structures are the same whether the entity
is local or remote. The property getter `<object>_is_local()` can be used to
retrieve whether the data structure represents a local resource.

The `mapper_map` structure is now a "top-level" object, and is used for
creating, modifying, and destroying mapping connections. Changes to a map (and
to other structures that represent non-local resources) must be explicitly
synchronized with the network using the `_push()` function, e.g.:

    mapper_map map = mapper_map_new(num_srcs, outsigs, num_dsts, insigs);
    mapper_map_set_expression(map, "y=x+2");
    mapper_map_push(map);

Or in C++:

    mapper::Map map(outsig, insig);
    map.set_expression("y=x+2");
    map.push();

Or in Python:

    map = mapper.map(outsig, insig)
    map.expression = "y=x+2"
    map.push()

Or in Java:

    mapper.Map map = new mapper.Map(outsig, insig);
    map.setExpression("y=x+2");
    map.push();

In all these cases the arguments for the map constructor are no longer the
*names* of the signals in question, but are **signal objects**: `mapper_signal`
in C, `mapper::Signal` in C++, etc. These could be local signals or signals
retrieved from the database.

There is also a new data structure called `mapper_slot` for representing map
endpoints (see below under **convergent mapping**). This means that for properties
such as minimum, maximum, and boundary behaviours it is the *slot* property that
needs to be set.

### Function naming

The names of many functions have been changed to make them consistent with the
names of the data structures they concern, e.g. `mdev_poll()` has been renamed
to `mapper_device_poll()`.

### Database queries

Previous versions of libmapper contained many duplicate functions for accessing
e.g. signals belonging to a device: one for retrieving them from a `mapper_device`
and another for retrieving them from the database. In version 1.0 all object
queries have been unified, and children of objects (e.g. signals, maps) are
retrieved by querying their parent. For example, all devices can be retrieved
from the database like this:

    mapper_device *devs = mapper_database_devices(db);
    while (devs) {
        printf("retrieved device '%s'\n", mapper_device_name(*devs));
        devs = mapper_device_query_next(devs);
    }

The old way of retrieving the output signals belonging to a particular device
was to call:

    mapper_db_signal *sigs = mapper_db_get_outputs_by_device_name(db, name);

and the new way:

    mapper_device dev = mapper_database_device_by_name(db, name);
    mapper_signal *sigs = mapper_device_signals(dev, MAPPER_DIR_OUTGOING);

Queries can be combined using `union`, `intersection`, or `difference`
operators, making it easy to e.g. find maps with a specific source and
destination device without requiring a dedicated function to do so:

    mapper_map *src, *dst, *combo;
    
    src = mapper_device_maps(src_dev, MAPPER_DIR_OUTGOING);
    dst = mapper_device_maps(dst_dev, MAPPER_DIR_INCOMING);
    combo = mapper_map_query_intersection(src, dst);
    
    while (combo) {
        mapper_map_print(*combo);
        maps = mapper_map_query_next(combo);
    }

The data structures `mapper_device`, `mapper_signal`, and `mapper_map` can also
now be queried by arbitrary property, making it quite easy to build arbitrary
queries:

    // retrieve all maps with the property 'foo' where its value is < 10
    int value = 10;
    mapper_map *maps = mapper_database_maps_by_property(db, "foo", 1, 'i',
                                                        &value,
                                                        MAPPER_OP_LESS_THAN);

### Convergent mapping

*Convergent* (or *many-to-one*) mapping refers to scenarios in which multiple
source signals are connected to the same destination signal. Prior to 1.0,
libmapper would allow a naïve implementation of convergent mapping: multiple
sources could be connected to a given destination, but their values would
overwrite each other at each source update. Starting with version 1.0, three
other methods for convergent mapping are also available:

* _Updating specific vector elements or element ranges_. By using the vector
indexing functionality mentioned above, parts of vector destinations can be
updated by different sources; vectors updates are now null-padded so elements
not addressed in the expression property will not be overwritten
* _Destination value reference_. References to past values of the destination in
the expression string (e.g. `y=y{-1}+x`) now refer to the _actual value_ of the
destination signal rather than the output of the expression. If the past
behaviour is desired, a user-defined variable can be used to cache the
expression output (e.g. `foo=foo{-1}+x; y=foo`). Note that if the destination value
is referenced in the expression string, libmapper will automatically move signal
processing to the destination device.
* _Multi-source maps_. Lastly, maps can now be created with multiple source
signals, and expressions can specify arbitrary combinations of the source values.
To support this functionality, the endpoints of maps are now represented using the
`mapper_slot` data structure; each map has a single destination slot and at
least one source slot. Several properties that used to belong to connections
have been moved to the map slots:

  * `map->range` ––> `slot->minimum` and `slot->maximum`
  * `map->bound_max` ––> `slot->bound_max`
  * `map->bound_min` ––> `slot->bound_min`
  * `map->send_as_instance` ––> `slot->use_as_instance`

Calibration used to be specified using the connection mode `MO_CALIBRATE`. It is
now specified using the slot property `calibrating`.

If all source slots belong to the same device, signal processing will be handled
by the source device by default, otherwise the destination will handle
evaluation of the expression. This can be controlled by setting the
`process_location` property of a map to either `MAPPER_LOC_SOURCE` or
`MAPPER_LOC_DESTINATION`. If a map has sources from different devices, or if the
map expression references the value of the destination (`y{-n}`) the processing
location will be set to `MAPPER_LOC_DESTINATION`.

The `mode` property of maps has changed a bit:

* `MO_BYPASS` has been removed since it didn't really describe what was going
on - although the expression evaluator was not being called the updates were
still undergoing type-coercion and vector padding/truncation. There is a new
mode called `MAPPER_MODE_RAW` that is used internally only when setting up
convergent maps.
* `MO_REVERSE` has been removed and replaced with directed maps; if a map from
a sink signal to a source signal is desired it is simply set up that way, e.g.
`mapper_map_new(1, &sink_sig, 1, &source_sig)`.
* `MO_CALIBRATE` has been removed and replaced with the `mapper_slot` property
`calibrating`.
* `MO_EXPRESSION` has been renamed `MAPPER_MODE_EXPRESSION`
* `MO_LINEAR` has been renamed `MAPPER_MODE_LINEAR`

### Changes under the hood

There have also been a number of internal improvements that have minimal effect
on the API but improve memory-management and add functionality:

* *Memory management:* Memory is now recovered from dropped connections, e.g.
when a peer device crashes or doesn't disconnect properly.
* *Links.* Devices now create network links automatically as needed.
* *Protocol changes:* In addition to the database subscription changes, some
other updates have been made to the libmapper protocol:
  * null-padding of incomplete vector updates 
  * references to maps in the protocol now have an explicit direction, e.g.
`/map ,sss "/srcdev.1/srcsig" "->" "/dstdev.1/dstsig"`. This means that maps can
now be defined between **any** set of signals, including `output->input`
(a "normal" connection), `input->output` (was a "reverse-mode" connection in
previous versions), `output->output`, and `input->input`
  * instance updates are explicitly tagged with their index, i.e. `@instance 2`
* *Efficient polling:* select() is now used internally to wait on servers,
resulting in less wasted processing and more responsive performance.

### Language bindings

* *C++.* Headers and tests for C++ have been added to the distribution, and
are mostly complete.
* *Python.* The SWIG/Python bindings have been largely rewritten to support
API changes in libmapper and for consistency with other object-oriented
libmapper bindings (C++, Java).
* *Java.* The jni bindings have been largely rewritten to support API changes
in libmapper and to improve consistency.

Version 0.4
-----------

Released August 3, 2016.

This is still a development release, and includes many API changes,
improvements, and new features since 0.3.

They are summarized very briefly here:

### Vector properties

Properties of objects (devices, signals, links, and connections) can now have vector
values. This includes signal minima and maxima, so "linear" mode connections can now
be calibrated per-element.

### Expression parser and evaluator

New capabilities have been added to the expression parser and evaluator, including:

* *User-defined variables.* New variables can be defined implicitly in the
expression, e.g. * `ema=ema{-1}*0.9+x*0.1; y=x-ema`
* *Filter initialization.* Past values of the filter output `y{-n}` can be set using additional sub-expressions, separated using semicolons, e.g. `y=y{-1}+x; y{-1}=100`.
Filter initialization takes place the first time the expression evaluator is called;
after this point the initialization sub-expressions will not be evaluated.
* *Vector indexing.* Elements of vector quantities can now be retrieved by index,
e.g.
  * `y = x[0]` — simple vector indexing
  * `y = x[1:2]` — specify a range within the vector
  * `y = [x[1],x[2],x[0]]` — rearranging vector elements
  * `y[1] = x` — apply update to a specific element of the output
  * `y[0:2] = x` — apply update to elements `0-2` of the output vector
  * `[y[0],y[2]] = x` — apply update to output vector elements `y[0]` and `y[2]` but
leave `y[1]` unchanged.
* *Vector functions.* Several functions that address entire vectors have been added:
`any`, `all`, `min`, `max`, `sum`, `mean`
* *Expression optimization.* Some additional optimizations have been added to the
expression parser, resulting in more efficient compiled expressions.

### Database subscriptions

The underlying protocol for synchronizing databases with the libmapper network has
been switched to a subscription-with-lease model.  This improves the granularity of
reported changes to remote objects while reducing network traffic. Databases are
now properly notified of signal removal.

### Language bindings

The Java bindings have been improved and extended to support libmapper's monitor
functionality.

As usual, this release is subject to API changes and breakage, however libmapper
is starting to approach a more mature and stable state.

Version 0.3
-----------

Released April 26, 2013.

This is still a development release, and includes many API changes,
improvements, and new features since 0.2.

They are summarized very briefly here:

  * _Time._ libmapper now supports timetagging every signal update.  With
    this we include the ability to bundle several signal updates
    together using "queues" which defer sending of updates.  We also
    add an optional `rate` property to signals which can be used to
    represent regularly-sampled signals.  This means that it is
    possible to support "blocks" of data where each sample does not
    have the overhead of an individual message header.  The default
    behaviour, however, is still to send each signal update as an
    individual, time-stamped OSC message, assuming irregular updates.
    Software sampling sensors should be aware of how to distinguish
    between regularly- and irregularly-sampled signals and use the
    correct behaviour accordingly.  At low samples rates, of course,
    the difference is not as important.

  * _Instances._  libmapper now supports the concept of mapping multiple
    "instances" of a signal over a single connection.  This can be
    used for supporting interaction with multi-touch devices as well
    as for polyphonic musical instruments.  A sophisticated "ID map"
    between instances created on a sender and instantiated on the
    receiver is automatically maintained, allowing the synchronization
    of behaviours between the two connection endpoints.  Allocation of
    (possibly limited) resources to individual instances can be
    managed by a callback, which is called when new instances are
    created and old instances are destroyed.  A complete treatment of
    this topic is not possible here, but will be documented in a
    forthcoming paper.

  * _Reverse connections._ A connection mode called `MO_REVERSE` has
    been added which can be used to establish signal from from output
    to input.  The intention is to use this for training machine
    learning systems, allowing the construction of so-called
    example-based "implicit mapping" scenarios.  `MO_REVERSE` of
    course does not invert the mapping expression, so is best-used for
    direct connections to an intermediate device that sits between a
    data source and destination.  Single "snapshots" of output state
    can also be acquired by a query system, using
    `msig_query_remotes()`.

  * _Compatibility with select()._  It is now possible to get a list of
    file descriptors that libmapper reads from.  This can be used as
    arguments to select() or poll() in a program that uses libmapper
    among other sockets or files to read from.

  * _Local link/connection callbacks._ Programs using libmapper can
    now know when a local device has been linked or connected by
    registering a callback for these events.  Although it is not
    encouraged at this time, this can be used for example to implement
    data transport over alternative protocols, using libmapper only
    for determining the destination IP and agreeing on the connection
    details.  In the future we plan to add support for alternative
    protocols within the libmapper framework, so that mapping
    expressions can still be used even when alternatives to OSC are
    used to send and receive data.

  * _Null values._ An important change that users should be aware of
    is that the signal callback (whose signature has changed) can now
    be called with the `value` pointer being zero.  This indicates
    that the mapper signal is actually non-existent, i.e. no value is
    associated with the signal.  That is to say that "null" is now
    considered a valid state for a signal and is different from, for
    example, a value of zero.  User code can choose to ignore this
    case or to make use of it if there is a semantically relevant
    action to take.  An example of this condition is if some signal
    does not have a valid value when some other signal is in a certain
    state, or for reverse connections, a null value might indicate
    that the output has not been set to anything yet.  Local code can
    query the last-updated value of any signal using the `msig_value`
    function.

The tutorial has also been translated for several supported language
bindings: Python, and Max/MSP.  Build documentation has also been
added.

As usual, this release is subject to API changes and breakage, however
libmapper is starting to approach a more mature and stable state.



Version 0.2
-----------

Released May 26, 2011.

New features:

- State queries from inputs to outputs.  Allows for "learning" devices
  to know the state of their destinations.
- Element-wise vector mapping.
- Removal of misleading "mapper_signal_value_t" union.  Replaced
  everywhere with void*.
- Allow setting the multicasting interface.
- Launcher to allow multiple copies of Python Slider example on OS X.
- SWIG/Python "monitor" and "db" bindings.
- JNI/Java bindings for "device" and "signal".
- Representation of "null" signal values.  (Signals are "off", or
  value is "unknown".)
- Windows support.
- Functions "midiToHz" and "hzToMidi".
- API stubs for timetag support.  Not yet implemented.

Bug fixes:

- Crash on uninitialized expressions.
- Set device name property of existing signals after initialization.
- Add input signal handlers added after initialization.
- Small memory leak in mdev_add_input.
- Remove liblo server method when signal is removed.
- Set the multicasting interface to loopback as last resort, making it
  work on Linux if not connected to network.
- Fix erroneous calculation of blocking time in mapper_monitor_poll.
- Crash when non-existent signal receives /disconnect.

Initial release 0.1 
-------------------

Released Dec. 10, 2010.
