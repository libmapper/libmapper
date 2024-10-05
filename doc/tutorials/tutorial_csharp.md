# Getting started with libmapper and C\#

Once you have libmapper installed, it can be imported into your program:

~~~csharp
using Mapper;
~~~

## Overview of the C\# API

If you take a look at the API documentation, there is a section called "modules".
This is divided into the following sections:

* Graphs
* Devices
* Signals
* Maps

For this tutorial, the only sections to pay attention to are **Devices** and **Signals**.
**Graphs** and **Maps** are mostly used when building user interfaces for designing mapping configurations (session managers).

## Devices

### Creating a device

To create a _libmapper_ device, it is necessary to provide a device name to the
constructor.
There is an initialization period after a device is created where a unique ordinal is chosen to append to the device name.
This allows multiple
devices with the same name to exist on the network.

A second optional parameter of the constructor is a Graph object.
It is not necessary to provide this, but can be used to specify different networking parameters, such as specifying the name of the network interface to use.

An example of creating a device:

~~~csharp
Device dev = new Device("myDevice");
~~~

### Polling the device

The device lifecycle looks like this:

<img style="display:block;margin:auto;padding:0px;width:75%" src="./images/device_lifecycle.png">

In other words, after a device is created, it must be continuously polled during its lifetime.

The polling is necessary for several reasons: to respond to requests on the
admin bus; to check for incoming signals; to update outgoing signals.
Therefore even a device that does not have signals must be polled.
The user program must organize to have a timer or idle handler which can poll the device often enough.
The polling interval is not extremely sensitive, but should be 100 ms or less.
The faster it is polled, the faster it can handle incoming and outgoing signals.

The `Poll()` function can be blocking or non-blocking, depending on how you want your application to behave.
It takes an optional number of milliseconds during which it should do some work before returning:

~~~csharp
int dev.Poll(int blockMs);
~~~

An example of calling it with non-blocking behaviour:

~~~csharp
dev.Poll();
~~~

If your polling is in the middle of a processing function or in response to a
GUI event for example, non-blocking behaviour is desired.
On the other hand if you put it in the middle of a loop which reads incoming data at intervals or steps through a simulation for example, you can use `Poll()` as your "sleep" function, so that it will react to network activity while waiting.

It returns the number of messages handled, so optionally you could continue to
call it until there are no more messages waiting.
The same effect can be acheived by passing a negative value for the `blockMs` argument.
Of course, you should be careful doing that without limiting the time it will loop for, since if the incoming stream is fast enough you might never get anything else done!

Note that an important difference between blocking and non-blocking polling is
that during the blocking period, messages will be handled immediately as they
are received.
On the other hand, if you use your own sleep, messages will be queued up until you can call poll(); stated differently, it will "time-quantize" the message handling.
This is not necessarily bad, but you should be aware of this effect.

If your code has updated signal values and will not be calling `Poll()` immediately, you may wish to call the function `UpdateMaps()`.
This will immediately cause any outgoing maps to be processed and send their updates to the destination signal.

### Initialization

Since there is a delay before the device is completely initialized, it is
sometimes useful to be able to determine this using `Ready()`.
Only when `dev.Ready()` returns non-zero is it valid to use the device's name.

## Signals

Now that we know how to create a device, poll it, and free it, we only need to
know how to add signals in order to give our program some input/output
functionality.
While libmapper enables arbitrary connections between _any_ declared signals, we still find it helpful to distinguish between two type of signals: `Incoming` and `Outgoing`.

- `Outgoing` signals are _sources_ of data, updated locally by their parent
  device
- `Incoming` signals are _consumers_ of data and are **not** generally updated
  locally by their parent device.

This can become a bit confusing, since the "reverb" parameter of a sound synthesizer might be updated locally through user interaction with a GUI, however the normal use of this signal is as a _destination_ for control data streams so it should be defined as an `Incoming` signal.
Note that this distinction is to help with GUI organization and user-understanding – _libmapper_ enables connections from `Incoming` signals and to `Outgoing` signals if desired.

### Creating a signal

We'll start with creating a "sender", so we will first talk about how to update
output signals.
A signal requires a bit more information than a device, much of which is optional:

1. the direction of the signal: either `Direction.Outgoing` or `Direction.Incoming`
* a name for the signal (must be unique within a devices inputs or outputs)
* the signal's vector length
* the signal's data type, one of `Type.Int32`, `Type.Float`, or `Type.Double`
* the signal's unit (optional)
* the signal's instance count (omit for singleton signals)

examples:

~~~csharp
Signal input = dev.AddSignal(Direction.Incoming, "myInput", 1,
                             Type.Float, "m/s");

int min[4] = {1,2,3,4};
int max[4] = {10,11,12,13};
Signal output = dev.AddSignal(Direction.Outgoing, "myOutput", 4,
                              Type.Int32, 0, min, max);
~~~

The only _required_ parameters here are the signal "length", its name, and data
type.
Signals are assumed to be vectors of values, so for usual single-valued signals, a length of 1 should be specified.
Finally, supported types are currently `Type.Int32`, `Type.Float`, or `Type.Double`, for `int`, `float`, or `double` values, respectively.

The more information you provide describing your signal, the more _libmapper_
can do some things automatically.
For example, if `min` and `max` are provided, it will be possible to create linear-scaled connections very quickly.
If `unit` is provided, _libmapper_ will be able to similarly figure out a linear scaling based on unit conversion (from centimeters to inches for example).
Currently automatic unit-based scaling is not a supported feature, but will be
added in the future.
You can take advantage of this future development by simply providing unit information whenever it is available.
It is also helpful documentation for users.

~~~csharp
int[] min = {1,2,3,4}, max = {10,11,12,13};
Signal outsig = dev.AddSignal(Direction.Outgoing, "outsig", 1, Type.Float)
                   .SetProperty(Property.Min, min)
                   .SetProperty(Property.Max, max);
~~~

Lastly, for `Incoming` signals it is usually necessary to be informed when the
signal values change.
This is done by providing a function to be called whenever its value is modified by an incoming message.
It is specified using the Signal method `SetCallback()`.

An example of creating a "barebones" `int` scalar output signal with no unit,
minimum, or maximum information:

~~~csharp
Signal sig = dev.AddSignal(Direction.Outgoing, "outA", 1, Type.Int32);
~~~

An example of a `float` signal where some more information is provided:

~~~csharp
float min = 0.0f;
float max = 5.0f;
Signal sig;
sig = dev.AddSignal(Direction.Outgoing, "sensor1", 1, Type.Float, "V")
         .SetProperty(Property.Min, min)
         .SetProperty(Property.Max, max);
~~~

So far we know how to create a device and to specify an output signal for it.
To recap, let's review the code so far:

~~~csharp
using Mapper;

public class Tutorial
{
    Device dev("testSender");
    Signal sig = dev.AddSignal(Direction.Outgoing, "sensor1", 1, Type.Float, "V")
                    .SetProperty(Property.Min, 0.0)
                    .SetProperty(Property.Max, 5.0);

    while (true) {
        dev.Poll(10);
        ... do stuff ...
        ... update signals ...
    }
}
~~~

It is possible to retrieve a device's signals at a later time using the function `dev.GetSignals()`.
This function returns an object of type `Mapper.List<Mapper.Signal>` which can be used to retrieve all of the signals belonging to a particular device:

~~~csharp
Console.WriteLine("Signals belonging to " + dev);

List<Signal> list = dev.GetSignals(Direction.Incoming);
foreach (Signal s in list) {
    Console.WriteLine("signal: " + s);
}
~~~

### Updating signals

We can imagine the above program getting sensor information in a loop.
It could be running on an network-enable ARM device and reading the ADC register directly, or it could be running on a computer and reading data from an Arduino over a USB serial port, or it could just be a mouse-controlled GUI slider.
However it's getting the data, it must provide it to _libmapper_ so that it will be sent to other devices if that signal is mapped.

This is accomplished by the function `SetValue()`, which is overloaded to
accept a wide variety of argument types (scalars and arrays of int, float, or
double).
Check the API documentation for more information.
The data passed to `SetValue()` is not required to match the length and type of the signal itself—libmapper will perform type coercion if necessary.

So in the "sensor1" example, assuming in `DoStuff()` we have some code which
reads sensor 1's value into a float variable called `v1`, the loop becomes:

~~~csharp
while (true) {
    dev.Poll(50);

    // call a hypothetical user function that reads a sensor
    float v1 = DoStuff();
    sig.SetValue(v1);
}
~~~

This is about all that is needed to expose sensor 1's value to the network as a
mappable parameter.
The _libmapper_ GUI can now map this value to a receiver,
where it could control a synthesizer parameter or change the brightness of an
LED, or whatever else you want to do.

### Signal conditioning

Most synthesizers of course will not know what to do with the value of sensor1—it is an electrical property that has nothing to do with sound or music.
This is where _libmapper_ really becomes useful.

Scaling or other signal conditioning can be taken care of _before_ exposing the
signal, or it can be performed as part of the mapping.
Since the end user can demand any mathematical operation be performed on the signal, they can perform whatever mappings between signals as they wish.

As a developer, it is therefore your job to provide information that will be
useful to the end user.

For example, if sensor1 is a position sensor, instead of publishing "voltage",
you could convert it to centimeters or meters based on the known dimensions of
the sensor, and publish a "/sensor1/position" signal instead, providing the unit information as well.

We call such signals "semantic", because they provide information with more
meaning than a relatively uninformative value based on the electrical properties of the sensing technique.
Some sensors can benefit from low-pass filtering or other measures to reduce noise.
Some sensors may need to be combined in order to derive physical meaning.
What you choose to expose as outputs of your device is entirely application-dependent.

You can even publish both "/sensor1/position" and "/sensor1/voltage" if desired, in order to expose both processed and raw data.
Keep in mind that these will not take up significant processing time, and _zero_ network bandwidth, if they are not mapped.

### Receiving signals

Now that we know how to create a sender, it would be useful to also know how to
receive signals, so that we can create a sender-receiver pair to test out the
provided mapping functionality.
The current value and timestamp for a signal can be retrieved at any time by calling the function `GetValue()` on your signal object, however for event-driven applications you may want to be informed of new values as they are received or generated.

As mentioned above, the `SetCallback()` function can be used to specify a function that will be called whenever the value of that signal changes.
To create a receiver for a synthesizer parameter "pulse width" (given as a ratio between 0 and 1).
We'll imagine there is some C\# synthesizer implemented as a class `Synthesizer` which has functions `SetPulseWidth()` which sets the pulse width in a thread-safe manner, and `StartAudioInBackground()` which sets up the audio thread.

Create the handler function, which is fairly simple,

~~~csharp
private static void PulseWidthHandler(Signal s, Signal.Event e, float f, Time t)
{
    synth.SetPulseWidth(f);
}
~~~

Then `Main()` will look like,

~~~csharp
public static void Main(string[] args)
{
    synth = new Synthesizer();
    synth.StartAudioInBackground();

    Device dev = new Device("synth");

    Signal pulseWidth = dev.AddSignal(Direction.Incoming, "pulsewidth",
                                      1, Type.Float).
                           .SetProperty(Property.Min, 0.0)
                           .SetProperty(Property.Max, 1.0);
                           .SetCallback((Action<Signal, Signal.Event, float, Time>)pulseWidthHandler, Signal.Event.Update);

    while (!done)
        dev.poll(50);
}
~~~

## Working with timetags

_libmapper_ uses the `Time` class to store
[NTP timestamps](http://en.wikipedia.org/wiki/Network_Time_Protocol#NTP_timestamps).
For example, the handler function called when a signal update is received
contains a `time` argument.
This argument indicates the time at which the source signal was _sampled_ (in the case of sensor signals) or _generated_ (in the case of sequenced or algorithimically-generated signals).

_libmapper_ provides helper functions for getting the current device-time,
setting the value of a `Time` from other representations, and comparing or
copying timetags.
Check the API documentation for more information.

## Working with signal instances

_libmapper_ also provides support for signals with multiple _instances_, for
example:

* control parameters for polyphonic synthesizers;
* touches tracked by a multitouch surface;
* "blobs" identified by computer vision systems;
* objects on a tabletop tangible user interface;
* _temporal_ objects such as gestures or trajectories.

The important qualities of signal instances in _libmapper_ are:

* **instances are interchangeable**: if there are semantics attached to a
  specific instance it should be represented with separate signals instead.
* **instances can be ephemeral**: signal instances can be dynamically created
  and destroyed.
  _libmapper_ will ensure that linked devices share a common
  understanding of the relatonships between instances when they are mapped.
* **map once for all instances**: one mapping connection serves to map all of a
  signal's instances.

All signals possess one instance by default.
If you would like to reserve more instances you can use:

~~~csharp
sig.ReserveInstances(int num);
~~~

After reserving instances you can update a specific instance, for example:

~~~csharp
Signal.Instance i = sig.GetInstance(id);
i.SetValue(value);

// or more simply
sig.GetInstance(id).SetValue(value);
~~~

All of the arguments except one should be familiar from the documentation of
`SetValue()` presented earlier.
The `id` argument does not have to be considered as an array index - it can be any integer that is convenient for labelling your instance.
_libmapper_ will internally create a map from your id label to one of the preallocated instance structures.

### Receiving instances

For instanced signals it is necessary to modify our callback function slightly:

~~~csharp
void Handler(Signal.Instance inst, Signal.Event evt, float f, Time time)
{
    ...
}
~~~

The inner class `Signal.Instance` can be updated and read using the same functions as its parent.
Remember – if you want to receive instance updates that you will need to reserve instances for your input signal using the `numInstances` argument in the signal constructor or by calling `sig.ReserveInstances()`.

### Instance Stealing

For handling cases in which the sender signal has more instances than the
receiver signal, the _instance allocation mode_ can be set for an input signal
to set an action to take in case all allocated instances are in use and a
previously unseen instance id is received.
Use the function:

~~~csharp
sig.SetProperty(Property.Stealing, mode);
~~~

The argument `mode` can have one of the following values:

* `Stealing.None` Default value, in which no stealing of instances will occur;
* `Stealing.Oldest` Release the oldest active instance and reallocate its
  resources to the new instance;
* `Stealing.Newest` Release the newest active instance and reallocate its
  resources to the new instance;

If you want to use another method for determining which active instance to
release (e.g. the sound with the lowest volume), you can create a handler for the signal and write the method yourself:

~~~csharp
void MyHandler(Signal.Instance s, Signal.Event e, Float f, Time t)
{
    if (e == Signal.Event.Overflow) {
        // user code chooses which instance to release
        UInt64 releaseMe = ChooseInstanceToRelease(sig);

        sig.GetInstance(releaseMe).Release();
    }
}
~~~

For this function to be called when instance stealing is necessary, we need to
register it for `Signal.Event.Overflow` events:

~~~csharp
sig.SetCallback(MyHandler, Signal.Event.Update | Signal.Event.Overflow);
~~~

## Publishing metadata

Things like device names, signal units, and ranges, are examples of metadata
--information about the data you are exposing on the network.

_libmapper_ also provides the ability to specify arbitrary extra metadata in the form of name-value pairs.
These are not interpreted by _libmapper_ in any way, but can be retrieved over the network.
This can be used for instance to give a device X and Y information, or to perhaps give a signal some property like "reliability", or some category like "light", "motor", "shaker", etc.

Some GUI could then use this information to display information about the
network in an intelligent manner.

Any time there may be extra knowledge about a signal or device, it is a good idea to represent it by adding such properties, which can be of any OSC-compatible type.  (So, numbers and strings, etc.)

The property interface is through the functions,

~~~csharp
void <object>.SetProperty(<name>, <value>);
~~~

The `<value>` arguments can be a scalar, or array of type `int`,
`float`, `double`, or `string`.

For example, to store a `float` indicating the position of a device, you can
call it like this:

~~~csharp
dev.SetProperty("position", [12.5, 5.6, 0.0]);
sig.SetProperty("sensingMethod", "resistive");
~~~

### Reserved keys

You can use any property name not already reserved by *libmapper*.

Object | Reserved keys
-------|--------------
All    | `data`, `description`, `id`, `is_local`, `name`, `status`, `version`
Device | `host`, `libversion`, `num_maps`, `num_maps_in`, `num_maps_out`, `num_sigs_in`, `num_sigs_out`, `ordinal`, `port`, `signal`, `synced`
Signal | `device`, `direction`, `ephemeral`, `jitter`, `length`, `max`, `maximum`, `min`, `minimum`, `num_inst`, `num_maps`, `num_maps_in`, `num_maps_out`, `period`, `rate`, `steal`, `type`, `unit`
Maps   | `bundle`, `expr`, `muted`, `num_destinations`, `num_sources`, `process_loc`, `protocol`, `scope`, `signal`, `slot`, `use_inst`
