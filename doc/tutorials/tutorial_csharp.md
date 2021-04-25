# Getting started with libmapper and C\#

Once you have libmapper installed, it can be imported into your program:

~~~python
using Mapper;
~~~

## Overview of the C\# API

If you take a look at the API documentation, there is a section called
"modules".  This is divided into the following sections:

* Graphs
* Devices
* Signals
* Maps

For this tutorial, the only sections to pay attention to are **Devices** and **Signals**. **Graphs** and **Maps** are mostly used when building
user interfaces for designing mapping configurations.

## Devices

### Creating a device

To create a _libmapper_ device, it is necessary to provide a device name to the
constructor.  There is an initialization period after a device is created where
a unique ordinal is chosen to append to the device name.  This allows multiple
devices with the same name to exist on the network.

A second optional parameter of the constructor is a Graph object.  It is not
necessary to provide this, but can be used to specify different networking
parameters, such as specifying the name of the network interface to use.

An example of creating a device:

~~~csharp
Device dev = new Device("my_device");
~~~

### Polling the device

The device lifecycle looks like this:

<img style="display:block;margin:auto;padding:0px;width:75%" src="./images/device_lifecyle.png">

In other words, after a device is created, it must be continuously polled during
its lifetime.

The polling is necessary for several reasons: to respond to requests on the
admin bus; to check for incoming signals; to update outgoing signals.  Therefore
even a device that does not have signals must be polled.  The user program must
organize to have a timer or idle handler which can poll the device often enough.
The polling interval is not extremely sensitive, but should be 100 ms or less.
The faster it is polled, the faster it can handle incoming and outgoing signals.

The `poll()` function can be blocking or non-blocking, depending on how you want
your application to behave.  It takes an optional number of milliseconds during
which it should do some work before returning:

~~~csharp
int dev.poll(int block_ms);
~~~

An example of calling it with non-blocking behaviour:

~~~csharp
dev.poll();
~~~

If your polling is in the middle of a processing function or in response to a
GUI event for example, non-blocking behaviour is desired.  On the other hand if
you put it in the middle of a loop which reads incoming data at intervals or
steps through a simulation for example, you can use `poll()` as your "sleep"
function, so that it will react to network activity while waiting.

It returns the number of messages handled, so optionally you could continue to
call it until there are no more messages waiting.  Of course, you should be
careful doing that without limiting the time it will loop for, since if the
incoming stream is fast enough you might never get anything else done!

Note that an important difference between blocking and non-blocking polling is
that during the blocking period, messages will be handled immediately as they
are received.  On the other hand, if you use your own sleep, messages will be
queued up until you can call poll(); stated differently, it will
"time-quantize" the message handling. This is not necessarily bad, but you
should be aware of this effect.

Since there is a delay before the device is completely initialized, it is
sometimes useful to be able to determine this using `getIsReady()`.  Only when
`dev.getIsReady()` returns non-zero is it valid to use the device's name.

## Signals

Now that we know how to create a device, poll it, and free it, we only need to
know how to add signals in order to give our program some input/output
functionality.  While libmapper enables arbitrary connections between _any_
declared signals, we still find it helpful to distinguish between two type of
signals: `inputs` and `outputs`. 

- `outputs` signals are _sources_ of data, updated locally by their parent
  device
- `inputs` signals are _consumers_ of data and are **not** generally updated
  locally by their parent device.

This can become a bit confusing, since the "reverb" parameter of a sound
synthesizer might be updated locally through user interaction with a GUI,
however the normal use of this signal is as a _destination_ for control data
streams so it should be defined as an `input` signal.  Note that this
distinction is to help with GUI organization and user-understanding –
_libmapper_ enables connections from input signals and to output signals if
desired.

### Creating a signal

We'll start with creating a "sender", so we will first talk about how to update
output signals.  A signal requires a bit more information than a device, much of
which is optional:

1. the direction of the signal: either `Direction.Outgoing` or `Direction.Incoming`
* a name for the signal (must be unique within a devices inputs or outputs)
* the signal's vector length
* the signal's data type, one of `Type.Int32`, `Type.Float`, or `Type.Double`
* the signal's unit (optional)
* the signal's minimum value (optional, type and length should match previous args)
* the signal's maximum value (optional, type and length should match previous args)
* the signal's instance count (pass `NULL` for singleton signals)
* a function to be called when the signal is updated (optional)
* flags indicating which events should trigger a call to the function

examples:

~~~csharp
Mapper.Signal input = dev.addSignal(Direction.Incoming, "myInput", 1,
                                    Mapper.Type.Float, "m/s", IntPtr.Zero,
                                    IntPtr.Zero, IntPtr.Zero, h,
                                    (int)Mapper.Signal.Event.Update);

int min[4] = {1,2,3,4};
int max[4] = {10,11,12,13};
Mapper.Signal output = dev.addSignal(Direction.Outgoing, "myOutput", 4,
                                     Mapper.Type.Int32, 0, min, max);
~~~

The only _required_ parameters here are the signal "length", its name, and data
type.  Signals are assumed to be vectors of values, so for usual single-valued
signals, a length of 1 should be specified.  Finally, supported types are
currently `Type.Int32`, `Type.Float`, or `Type.Double`, for `int`, `float`, or `double`
values, respectively.

The other parameters are not strictly required, but the more information you
provide, the more _libmapper_ can do some things automatically.  For example, if
`minimum` and `maximum` are provided, it will be possible to create
linear-scaled connections very quickly.  If `unit` is provided, _libmapper_ will
be able to similarly figure out a linear scaling based on unit conversion (from
centimeters to inches for example). Currently automatic unit-based scaling is
not a supported feature, but will be added in the future.  You can take
advantage of this future development by simply providing unit information
whenever it is available.  It is also helpful documentation for users.

Notice that optional values are provided as `void*` pointers.  This is because a
signal can either be `int`, `float` or `double`, and your maximum and minimum
values should correspond in type.  So you should pass in a `int*`, `float*` or
`double*` by taking the address of a local variable.

Lastly, it is usually necessary to be informed when input signal values change.
This is done by providing a function to be called whenever its value is modified
by an incoming message.  It is passed in the `handler` parameter.

An example of creating a "barebones" `int` scalar output signal with no unit,
minimum, or maximum information:

~~~csharp
Mapper.Signal sig;
sig = dev.addSignal(Mapper.Direction.Outgoing, "outA", 1, Mapper.Type.Int32);
~~~

An example of a `float` signal where some more information is provided:

~~~csharp
float min = 0.0f;
float max = 5.0f;
Mapper.Signal sig;
sig = dev.addSignal(Mapper.Direction.Outgoing, "sensor1", 1,
                    Mapper.Type.Float, "V", min, max);
~~~

So far we know how to create a device and to specify an output signal
for it.  To recap, let's review the code so far:

~~~csharp
Mapper.Device dev("test_sender");
Mapper.Signal sig;
float min = 0.0f;
float max = 5.0f;
sig = dev.addSignal(Mapper.Direction.Outgoing, "sensor1", 1,
                    Mapper.Type.Float, "V", min, max);
    
while (!done) {
    dev.poll(10);
    ... do stuff ...
    ... update signals ...
}
~~~

It is possible to retrieve a device's signals at a later time using the function `dev.signals()`. This function returns an object of type `mapper.List`
which can be used to retrieve all of the signals belonging to a particular
device:

~~~csharp
Console.WriteLine("Signals belonging to " + dev.getProperty(Property.Name));

Mapper.List list = dev.signals(Direction.Incoming).begin();
for (; list != list.end(); ++list) {
    Console.WriteLine("signal: " + *list);
}

// or more simply:
for (Mapper.Signal sig : dev.signals())
    Console.WriteLine("signal: " + sig);
~~~

### Updating signals

We can imagine the above program getting sensor information in a loop.  It could
be running on an network-enable ARM device and reading the ADC register
directly, or it could be running on a computer and reading data from an Arduino
over a USB serial port, or it could just be a mouse-controlled GUI slider.
However it's getting the data, it must provide it to _libmapper_ so that it will
be sent to other devices if that signal is mapped.

This is accomplished by the function `setValue()`, which is overloaded to
accept a wide variety of argument types (scalars and arrays of int, float, or
double). Check the API documentation for more information. The data passed to set_value() is not
required to match the length and type of the signal itself—libmapper will perform
type coercion if necessary. More than one 'sample' of signal update may be
applied at once by e.g. updating a signal with length 5 using a 20-element
array.

So in the "sensor 1" example, assuming in `doStuff()` we have some code which
reads sensor 1's value into a float variable called `v1`, the loop becomes:

~~~csharp
while (!done) {
    dev.poll(50);
    
    // call a hypothetical user function that reads a sensor
    float v1 = doStuff();
    sensor1.setValue(v1);
}
~~~

This is about all that is needed to expose sensor 1's value to the network as a
mappable parameter.  The _libmapper_ GUI can now map this value to a receiver,
where it could control a synthesizer parameter or change the brightness of an
LED, or whatever else you want to do.

### Signal conditioning

Most synthesizers of course will not know what to do with the value of sensor1
--it is an electrical property that has nothing to do with sound or music.  This
is where _libmapper_ really becomes useful.

Scaling or other signal conditioning can be taken care of _before_ exposing the
signal, or it can be performed as part of the mapping.  Since the end user can
demand any mathematical operation be performed on the signal, he can perform
whatever mappings between signals as he wishes.

As a developer, it is therefore your job to provide information that will be
useful to the end user.

For example, if sensor 1 is a position sensor, instead of publishing "voltage",
you could convert it to centimeters or meters based on the known dimensions of
the sensor, and publish a "/sensor1/position" signal instead, providing the unit
information as well.

We call such signals "semantic", because they provide information with more
meaning than a relatively uninformative value based on the electrical properties
of the sensing technqiue.  Some sensors can benefit from low-pass filtering or
other measures to reduce noise.  Some sensors may need to be combined in order
to derive physical meaning.  What you choose to expose as outputs of your device
is entirely application-dependent.

You can even publish both "/sensor1/position" and "/sensor1/voltage" if desired,
in order to expose both processed and raw data.  Keep in mind that these will
not take up significant processing time, and _zero_ network bandwidth, if they
are not mapped.

### Receiving signals

Now that we know how to create a sender, it would be useful to also know how to
receive signals, so that we can create a sender-receiver pair to test out the
provided mapping functionality. The current value and timestamp for a signal can
be retrieved at any time by calling the function `value()` on your signal
object, however for event-driven applications you may want to be informed of new
values as they are received or generated.

As mentioned above, the `addSignal()` function takes an optional
`handler` argument.  This is a function that will be called whenever the
value of that signal changes.  To create a receiver for a synthesizer parameter
"pulse width" (given as a ratio between 0 and 1), specify a handler when calling
`addSignal()`.  We'll imagine there is some C\# synthesizer implemented
as a class `Synthesizer` which has functions `setPulseWidth()` which sets the
pulse width in a thread-safe manner, and `startAudioInBackground()` which sets
up the audio thread.

Create the handler function, which is fairly simple,

~~~csharp
void pulsewidthHandler(Signal sig, mpr_id instance,
                       void *value, int count,
                       Mapper.Time time)
{
    Synthesizer *s = (Synthesizer*) sig[];
    s->setPulseWidth(*(float*)v);
}
~~~

First, the pointer to the `Synthesizer` instance is extracted from the
`user_data` pointer, then it is dereferenced to set the pulse width according to
the value pointed to by `v`.

Then `main()` will look like,

~~~c++
void main()
{
    Synthesizer synth;
    synth.startAudioInBackground();
    
    float min_pw = 0.0f;
    float max_pw = 1.0f;
    
    Mapper.Device dev("synth");
    
    Mapper.Signal pulsewidth =
        dev.addSignal(Mapper.Direction.Incoming, "pulsewidth", 1,
                      Mapper.Type.Float, 0, min_pw, max_pw, 0,
                      pulsewidth_handler, Mapper.Signal.Event.Update);
    
    while (!done)
        dev.poll(50);
}
~~~

## Working with timetags

_libmapper_ uses the `Time` class to store
[NTP timestamps](http://en.wikipedia.org/wiki/Network_Time_Protocol#NTP_timestamps).
For example, the handler function called when a signal update is received
contains a `time` argument.  This argument indicates the time at which the
source signal was _sampled_ (in the case of sensor signals) or _generated_ (in
the case of sequenced or algorithimically-generated signals).

_libmapper_ provides helper functions for getting the current device-time,
setting the value of a `Timetag` from other representations, and comparing or
copying timetags.  Check the API documentation for more information.

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
  and destroyed. _libmapper_ will ensure that linked devices share a common
  understanding of the relatonships between instances when they are mapped.
* **map once for all instances**: one mapping connection serves to map all of a
  signal's instances.

All signals possess one instance by default. If you would like to reserve more
instances you can use:

~~~c++
sig.reserveInstances(int num)
sig.reserveInstances(int num, mpr_id *ids)
sig.reserveInstances(int num, mpr_id *ids, void **data)
~~~

After reserving instances you can update a specific instance, for example:

~~~c++
sig.instance(id).setValue(value)
~~~

All of the arguments except one should be familiar from the documentation of
`set_value()` presented earlier.  The `instance` argument does not have to be
considered as an array index - it can be any integer that is convenient for
labelling your instance. _libmapper_ will internally create a map from your id
label to one of the preallocated instance structures.

### Receiving instances

You might have noticed earlier that the handler function called when a signal
update is received has a argument called `instance`. Here is the function
prototype again:

~~~csharp
void handler(Mapper.Signal sig, Mapper.Signal.Event evt, mpr_id inst,
             int len, mpr_type type, void *value, Mapper.Time time);
~~~

Under normal usage, this argument will have a value (0 <= n <= num_instances)
and can be used as an array index. Remember that you will need to reserve
instances for your input signal using `sig.reserve_instances()` if you want to
receive instance updates.

### Instance Stealing

For handling cases in which the sender signal has more instances than the
receiver signal, the _instance allocation mode_ can be set for an input signal
to set an action to take in case all allocated instances are in use and a
previously unseen instance id is received. Use the function:

~~~csharp
sig.setProperty(Mapper.Property.StealingMode, mode);
~~~

The argument `mode` can have one of the following values:

* `Stealing.None` Default value, in which no stealing of instances will occur;
* `Stealing.Oldest` Release the oldest active instance and reallocate its
  resources to the new instance;
* `Stealing.Newest` Release the newest active instance and reallocate its
  resources to the new instance;

If you want to use another method for determining which active instance to
release (e.g. the sound with the lowest volume), you can create a `mpr_sig_handler` for the signal and write the method yourself:

~~~csharp
void my_handler(Mapper.Signal sig, Mapper.Signal.Event evt, UInt64 inst,
                int len, Mapper.Type type, IntPtr val, IntPtr time)
{
    if (evt == Mapper.Signal.Event.Overflow) {
        // user code chooses which instance to release
        mpr_id release_me = choose_instance_to_release(sig);
    
        sig.instance(release_me).release(time);
    }
}
~~~

For this function to be called when instance stealing is necessary, we need to
register it for `IN_OVERFLOW` events:

~~~csharp
sig.setCallback(my_handler, Signal.Event.Overflow);
~~~

## Publishing metadata

Things like device names, signal units, and ranges, are examples of metadata
--information about the data you are exposing on the network.

_libmapper_ also provides the ability to specify arbitrary extra metadata in the
form of name-value pairs.  These are not interpreted by _libmapper_ in any way,
but can be retrieved over the network.  This can be used for instance to give a
device X and Y information, or to perhaps give a signal some property like
"reliability", or some category like "light", "motor", "shaker", etc.

Some GUI could then use this information to display information about the
network in an intelligent manner.

Any time there may be extra knowledge about a signal or device, it is a good
idea to represent it by adding such properties, which can be of any
OSC-compatible type.  (So, numbers and strings, etc.)

The property interface is through the functions,

~~~csharp
void <object>.setProperty(<name>, <value>);
~~~

The `<value>` arguments can be a scalar, or array of type `int`,
`float`, `double`, or `char*`.

For example, to store a `float` indicating the X position of a device, you can
call it like this:

~~~csharp
dev.setProperty("x", 12.5f);
sig.setProperty("sensingMethod", "resistive");
~~~

### Reserved keys

You can use any property name not already reserved by _libmapper_.

#### Reserved keys for devices

`data`, `id`, `is_local`, `lib_version`, `linked`, `name`, `num_maps_in`,
`num_maps_out`, `num_sigs_in`, `num_sigs_out`, `ordinal`, `status`, `synced`,
`version`

#### Reserved keys for signals

`data`, `device`, `direction`, `id`, `is_local`, `jitter`, `length`, `max`,
`maximum`, `min`, `minimum`, `name`, `num_inst`, `num_maps_in`, `num_maps_out`,
`period`, `steal`, `type`, `unit`, `use_inst`, `version`

#### Reserved keys for maps

`data`, `expr`, `id`, `is_local`, `muted`, `num_sigs_in`, `process_loc`,
`protocol`, `scope`, `status`, `use_inst`, `version`
