# Getting started with libmapper and Java

Since _libmapper_ uses GNU autoconf, getting started with the library is the
same as any other library on Linux; use `./configure` and then `make` to compile
it.  You'll need the `Java Developer Kit (JDK)`  available if you want to
compile the Java bindings.

Once you have _libmapper_ installed, it can be imported into your program:

~~~java
import mapper.*;
~~~

## Overview of the API

The _libmapper_ API is is divided into the following sections:

* Graphs
* Devices
* Signals
* Maps
* Lists

For this tutorial, the only sections to pay attention to are **Devices** and
**Signals**. The other sections are mostly used when building user interfaces for
designing mapping configurations.

## Devices

### Creating a device

To create a _libmapper_ device, it is necessary to provide a device name to the
constructor.  There is a brief initialization period after a device is created
during which a unique ordinal is chosen to append to the device name.  This
allows multiple devices with the same name to exist on the network.

A second optional parameter of the constructor is a `Graph` object.  It is not
necessary to provide this, but can be used to specify different networking
parameters, such as specifying the name of the network interface to use.

An example of creating a device:

~~~java
final Device dev = new Device("my_device");
~~~

### Polling the device

The device lifecycle looks like this:

<img style="display:block;margin:auto;padding:0px;width:75%" src="./images/device_lifecyle.png">

In other words, after a device is created, it must be continuously polled during
its lifetime.

The polling is necessary for several reasons: to respond to administrative
messages; to check for incoming signals.  Therefore even a device that does not
have signals must be polled.  The user program must organize to have a timer or
idle handler which can poll the device often enough.  The polling interval is
not extremely sensitive, but should be 100 ms or less.  The more often it is
polled, the faster it can handle incoming and outgoing signals.

The `poll()` function can be blocking or non-blocking, depending on how you want
your application to behave.  It takes a number of milliseconds during which it
should do some work, or 0 if it should check for any immediate actions and then
return without waiting:

~~~java
dev.poll(int block_ms);
~~~

An example of calling it with non-blocking behaviour:

~~~java
dev.poll(0);
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
queued up until you can call `poll()`; stated differently, it will
"time-quantize" the message handling.  This is not necessarily bad, but you
should be aware of this effect.

Since there is a delay before the device is completely initialized, it is
sometimes useful to be able to determine this using `ready()`.  Only when
`ready()` returns non-zero is it valid to use the device's name.

## Signals

Now that we know how to create a device and poll it, we only need to know how to
add signals in order to give our program some input/output functionality.  While
libmapper enables arbitrary connections between _any_ declared signals, we still
find it helpful to distinguish between two type of signals: `inputs` and
`outputs`. 

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

* a name for the signal (must be unique within a device's inputs or outputs)
* the signal's vector length
* the signal's data type: Type.INT32, Type.FLT, or Type.DBL
* the signal's unit (optional)
* the signal's minimum value (optional)
* the signal's maximum value (optional)
* the signal's number of `Instances` (optional, will default to singleton)

for input signals you will usually include additional arguments:

* a function to be called when the signal is updated
* 

examples:

~~~java
Signal in = dev.addSignal(Direction.INCOMING, "my_input", 1, Type.FLOAT, "m/s",
                          -10.f, 10.f, null, new mapper.signal.Listener() {
    public void onEvent(Signal sig, mapper.signal.Event e, float value, Time t) {
        System.out.println("got input for signal "+sig.properties().get("name"));
    }});

Signal out = dev.addSignal(Direction.OUTGOING, "my_output", 4, Type.INT32, null,
                           0, 1000, null, null);
~~~

The only _required_ parameters here are the signal "length", its name, and data
type.  Signals are assumed to be vectors of values, so for usual single-valued
signals, a length of 1 should be specified.  Finally, supported types are
currently 'i', 'f' or 'd' for `int`, `float` or `double` values, respectively.

The other parameters are not strictly required, but the more information you
provide, the more _libmapper_ can do some things automatically.  For example, if
the `minimum` and `maximum` properties are provided, it will be possible to
create linear-scaled connections very quickly. If `unit` is provided, _libmapper_
will be able to similarly figure out a linear scaling based on unit conversion
(centimeters to inches for example).  Currently automatic unit-based scaling is
not a supported feature, but will be added in the future.  You can take
advantage of this future development by simply providing unit information
whenever it is available.  It is also helpful documentation for users.

Lastly, it is usually necessary to be informed when input signal values change.
This is done by providing a function to be called whenever its value is modified
by an incoming message.  It is passed in the `UpdateListener` parameter.

An example of creating a "barebones" integer scalar output signal with no unit,
minimum, or maximum information:

~~~java
Signal outA = dev.addSignal(Direction.OUTGOING, "outA", 1, 'i', null, null, null);
~~~

An example of a `float` signal where some more information is provided:

~~~java
Signal sensor1 = dev.addSignal(Direction.OUTGOING, "sensor1", 1, 'f', "V", 0.0, 5.0)
~~~

So far we know how to create a device and to specify an output signal for it.
To recap, let's review the code so far:

~~~java
import mapper.*;
import mapper.signal.*;

class test {
    public static void main() {
        final Device dev = new Device("testDevice");
        Signal sensor1 = dev.addSignal(Direction.OUTGOING, "sensor1", 1, 'f', "V",
                                       0.0, 5.0);
        while (1) {
            dev.poll(50);
            ... do stuff ...
            ... update signals ...
        }
    }
}
~~~

It is possible to retrieve a device's inputs or outputs at a later time using
the functions `inputs()` and `outputs()`.

### Updating signals

We can imagine the above program getting sensor information in a loop.  It could
be running on an network-enabled ARM device and reading the ADC register
directly, or it could be running on a computer and reading data from an Arduino
over a USB serial port, or it could just be a mouse-controlled GUI slider.
However it's getting the data, it must provide it to _libmapper_ so that it will
be sent to other devices if that signal is mapped.

This is accomplished by the `update()` function:

~~~java
<sig>.update(<value>)
~~~

So in the "sensor 1" example, assuming we have some code which reads sensor 1's
value into a float variable called `v1`, the loop becomes:

~~~java
while (1) {
    dev.poll(50);
    
    // call a hypothetical function that reads a sensor
    v1 = read_sensor_1();
    sensor1.update(v1);
}
~~~

This is about all that is needed to expose sensor 1's value to the network as a
mappable parameter.  The _libmapper_ GUI can now be used to create a mapping
between this value and a receiver, where it could control a synthesizer
parameter or change the brightness of an LED, or whatever else you want to do.

### Signal conditioning

Most synthesizers of course will not know what to do with the value of sensor1 
-- it is an electrical property that has nothing to do with sound or music.
This is where _libmapper_ really becomes useful.

Scaling or other signal conditioning can be taken care of _before_ exposing the
signal, or it can be performed as part of the mapping.  Since end users can
demand any mathematical operation be performed on the signal, they can perform
whatever mappings between signals they wish.

As a developer, it is therefore your job to provide information that will be
useful to the end user.

For example, if sensor 1 is a position sensor, instead of publishing "voltage",
you could convert it to centimeters or meters based on the known dimensions of
the sensor, and publish a "/sensor1/position" signal instead, providing the unit
information as well.

We call such signals "semantic", because they provide information with more
meaning than a relatively uninformative value based on the electrical properties
of the sensing technique.  Some sensors can benefit from low-pass filtering or
other measures to reduce noise.  Some sensor data may need to be combined in
order to derive physical meaning.  What you choose to expose as outputs of your
device is entirely application-dependent.

You can even publish both "/sensor1/position" and "/sensor1/voltage" if desired,
in order to expose both processed and raw data.  Keep in mind that these will
not take up significant processing time, and _zero_ network bandwidth, if they
are not mapped.

### Receiving signals

Now that we know how to create a sender, it would be useful to also know how to
receive signals, so that we can create a sender-receiver pair to test out the
provided mapping functionality.

As mentioned above, the `addInputSignal()` function takes an optional
`UpdateListener`.  This is a function that will be called whenever the value of
that signal changes.  To create a receiver for a synthesizer parameter "pulse
width" (given as a ratio between 0 and 1), specify a handler when calling
`addInputSignal()`.  We'll imagine there is some Java synthesizer implemented as
a class `Synthesizer` which has functions `setPulseWidth()` which sets the pulse
width in a thread-safe manner, and `startAudioInBackground()` which sets up the
audio thread.

We need to create a handler function for _libmapper_ to update the synth:

~~~java
UpdateListener freqHandler = new UpdateListener() {
    public void onUpdate(Signal sig, float[] value, TimeTag tt) {
        setPulseWidth(value);
    }};
~~~

Then our program will look like this:

~~~java
import mapper.*;
import mapper.signal.*;

# Some synth stuff
startAudioInBackground();

UpdateListener freqHandler = new Listener() {
    public void onEvent(Signal sig, mapper.signal.Event event, float value, Time t) {
        setPulseWidth(value);
    }};

final Device dev = new Device("mySynth");
Signal pw = dev.addSignal(Direction.INCOMING, "pulseWidth", 1, Type.FLOAT,
                          "Hz", 0.0, 1.0, null, freqHandler);

while (1) {
    dev.poll(100);
}

synth.stop()
~~~

Alternately, we can declare the `UpdateListener` as part of the
`addInputSignal()` function:

~~~java
Signal pulseWidth = dev.addSignal(Direction.INCOMING, "pulseWidth", 1, 'f', "Hz",
                                  0.0, 1.0, null, new Listener() {
    public void onEvent(Signal sig, mapper.signal.Event event, float value, Time t) {
        setPulseWidth(value);
    }
});
~~~

## Working with timetags

_libmapper_ uses the `TimeTag` class to store 
[NTP timestamps](http://en.wikipedia.org/wiki/Network_Time_Protocol#NTP_timestamps)
associated with signal updates.  For example, the handler function called when a
signal update is received contains a `timetag` argument.  This argument
indicates the time at which the source signal was _sampled_ (in the case of
sensor signals) or _generated_ (in the case of sequenced or
algorithimically-generated signals).

_libmapper_ also provides helper functions for getting the current time:

~~~java
TimeTag tt = new TimeTag();
tt.now();
~~~

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
* **one mapping connection serves to map all of its instances.**

All signals possess one instance by default. If you would like to reserve more
instances you can use:

~~~java
<sig>.reserveInstances(int num);
~~~

After reserving instances you can update a specific instance:

~~~java
Signal.Instance inst = <sig>.instance();
inst.setValue(<value>);
~~~

### Associating signal instances with Java objects

Signal instances can be associated with an arbitrary object, for example:

~~~java
int[] my_obj = new int[]{1,2,3,4};
Signal.Instance inst = <sig>.instance(my_obj);
~~~

The object can be retrieved:

~~~java
Object o = inst.userReference();
~~~

### Receiving instances

To receive updates to multiple instances of an input signal you will need to
declare a `Listener` for the signal in question. Here is the
listener prototype:

~~~java
new Listener(Signal.Instance inst, float value, Time t);
~~~

The listener can be added using the function `setListener()`:

~~~java
<sig>.setListener(new mapper.signal.Listener() {
    public void onUpdate(Signal.Instance inst, float v, Time t) {
        System.out.println("in onInstanceUpdate() for "
                           +inst.signal().name()+" instance "
                           +inst.id()+": "+inst.userReference()+", val= "
                           +Arrays.toString(v));
    }
});
~~~

Remember that you will need to reserve instances for your input signal using
`<sig>.reserveInstances()` if you want to receive instance updates.

### Instance Stealing

For handling cases in which the sender signal has more instances than the
receiver signal, the _instance allocation mode_ can be set for an input signal
to set an action to take in case all allocated instances are in use and a
previously unseen instance id is received. Use the function:

~~~java
<sig>.setInstanceStealingMode(mode);
~~~

The argument `mode` can have one of the following values:

* `StealingMode.NONE` Default value, in which no stealing of instances will
occur;
* `StealingMode.OLDEST` Release the oldest active instance and reallocate its
resources to the new instance;
* `StealingMode.NEWEST` Release the newest active instance and reallocate its
resources to the new instance;

If you want to use another method for determining which active instance to
release (e.g. the sound with the lowest volume), you can create an
`InstanceEventListener` for the signal and write the method yourself:

~~~java
signal.Listener myHandler = new signal.Listener() {
    public void onEvent(Signal.Instance inst, mapper.signal.Event event,
                        Time t) {
        System.out.println("onEvent() for "
                           + inst.signal().name() + " instance "
                           + inst.id() + ": " + event.value());
        // call user function that chooses an instance to release
        Signal.Instance release_me = choose_instance(inst.signal());
        release_me.release();
    }
}
~~~

For this function to be called when instance stealing is necessary, we
need to register it for `mapper.signal.Event.OVERFLOW` events:

~~~java
<sig>.setListener(myHandler, mapper.signal.Event.OVERFLOW);
~~~

## Publishing metadata

Things like device names, signal units, and ranges, are examples of metadata –
information about the data you are exposing on the network.

_libmapper_ also provides the ability to specify arbitrary extra metadata in the
form of name-value pairs.  These are not interpreted by _libmapper_ in any way,
but can be retrieved over the network.  This can be used for instance to label a
device with its location, or to perhaps give a signal some property like
"reliability", or some category like "light", "motor", "shaker", etc.

Some GUI could then use this information to display information about the
network in an intelligent manner.

Any time there may be extra knowledge about a signal or device, it is a good
idea to represent it by adding such properties, which can be of any
OSC-compatible type.  (So, numbers and strings, etc.)

The property interface is through the functions,

~~~java
<object>.setProperty(String key, Value value);
~~~

where the value can any OSC-compatible type. This function can be called for
devices or signals.

For example, to store a `float vector` indicating the 2D position of a device
`dev`, you can call it like this:

~~~java
dev.setProperty("position", new Value(new float[] {12.5f, 40.f}));
~~~

To specify a string property of a signal `sig`:

~~~java
sig.setProperty("sensingMethod", new Value("resistive"));
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
