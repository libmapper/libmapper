Using libmapper and C++
============

C++ bindings are supplied

    #include <mapper/mapper_cpp.h>


Devices
=======

Creating a device
-----------------

To create a _libmapper_ device, it is necessary to provide a few
parameters the constructor:

    mapper::Device dev( const char *name, mapper.Admin admin );
    mapper::Device dev( std::string name, mapper.Admin admin );

In regular usage only the first argument is needed. The optional
"admin" parameter can be used to specify different networking
parameters, such as specifying the name of the network interface to use.

An example of creating a device:

    mapper::Device dev( "test" );

Polling the device
------------------

The device lifecycle looks like this, in terrible ASCII diagram art:

    creation --> poll --+--> destruction
                  |     |
                  +--<--+

In other words, after a device is created, it must be continuously
polled during its lifetime.

The polling is necessary for several reasons: to respond to requests
on the admin bus; to check for incoming signals; to update outgoing
signals.  Therefore even a device that does not have signals must be
polled.  The user program must organize to have a timer or idle
handler which can poll the device often enough.  Polling interval is
not extremely sensitive, but should be at least 100 ms or less.  The
faster it is polled, the faster it can handle incoming and outgoing
signals.

The `poll()` function can be blocking or non-blocking, depending on
how you want your application to behave.  It takes an optional number
of milliseconds during which it should do some work before returning:

    int dev.poll( int block_ms );

An example of calling it with non-blocking behaviour:

    dev.poll();

If your polling is in the middle of a processing function or in
response to a GUI event for example, non-blocking behaviour is
desired.  On the other hand if you put it in the middle of a loop
which reads incoming data at intervals or steps through a simulation
for example, you can use `poll()` as your "sleep" function, so that
it will react to network activity while waiting.

It returns the number of messages handled, so optionally you could
continue to call it until there are no more messages waiting.  Of
course, you should be careful doing that without limiting the time it
will loop for, since if the incoming stream is fast enough you might
never get anything else done!

Note that an important difference between blocking and non-blocking
polling is that during the blocking period, messages will be handled
immediately as they are received.  On the other hand, if you use your
own sleep, messages will be queued up until you can call poll();
stated differently, it will "time-quantize" the message handling.
This is not necessarily bad, but you should be aware of this effect.

Since there is a delay before the device is completely initialized, it
is sometimes useful to be able to determine this using `ready()`.
Only when `dev.ready()` returns non-zero is it valid to use the
device's name.

Signals
=======

Now that we know how to create a device, poll it, and free it, we only
need to know how to add signals in order to give our program some
input/output functionality.

We'll start with creating a "sender", so we will first talk about how
to update output signals.

Creating a signal
-----------------

A signal requires a bit more information than a device, much of which
is optional:

* a name for the signal (must be unique within a devices inputs or outputs)
* the signal's vector length
* the signal's data type expressed as a character 'i', 'f', 'd'
* the signal's unit (optional)
* the signal's minimum value (optional)
* the signal's maximum value (optional)

for input signals there is an additional argument:

* a function to be called when the signal is updated

examples:

    mapper::Signal sig_in = dev.add_input( "/my_input", 1, 'f', "m/s", 0, 0, h )

    int min[4] = {1,2,3,4};
    int max[4] = {10,11,12,13};
    mapper::Signal sig_out = dev.add_output( "/my_output", 4, 'i', 0, min, max )

The only _required_ parameters here are the signal "length", its name,
and data type.  Signals are assumed to be vectors of values, so for
usual single-valued signals, a length of 1 should be specified.  A
signal name should start with "/", as this is how it is represented in
the OSC address.  (One will be added if you forget to do this.)
Finally, supported types are currently 'i', 'f', or 'd' (specified as
characters in C, not strings), for `int`, `float`, or `double` values,
respectively.

The other parameters are not strictly required, but the more
information you provide, the more the mapper can do some things
automatically.  For example, if `minimum` and `maximum` are provided,
it will be possible to create linear-scaled connections very quickly.
If `unit` is provided, the mapper will be able to similarly figure out
a linear scaling based on unit conversion (from centimeters to inches for
example). Currently automatic unit-based scaling is not a supported
feature, but will be added in the future.  You can take advantage of
this future development by simply providing unit information whenever
it is available.  It is also helpful documentation for users.

Notice that optional values are provided as `void*` pointers.  This is
because a signal can either be `int`, `float` or `double`, and your
maximum and minimum values should correspond in type.  So you should
pass in a `int*`, `*float` or `*double` by taking the address of a
local variable.

Lastly, it is usually necessary to be informed when input signal
values change.  This is done by providing a function to be called
whenever its value is modified by an incoming message.  It is passed
in the `handler` parameter, with context information to be passed to
that function during callback in `user_data`.

An example of creating a "barebones" `int` scalar output signal with
no unit, minimum, or maximum information:

    mapper::Signal outputA = dev.add_output( "/outA", 1, 'i', 0, 0, 0 );

An example of a `float` signal where some more information is provided:

    float minimum = 0.0f;
    float maximum = 5.0f;
    mapper::Signal sensor1_voltage = mdev.add_output( "/sensor1", 1, 'f',
                                                      "V", &minimum, &maximum );

So far we know how to create a device and to specify an output signal
for it.  To recap, let's review the code so far:
 
    mapper::Device dev( "test_sender");
    mapper::Signal sensor1_voltage = dev( "/sensor1", 1, 'f', "V",
                                          &minimum, &maximum );
    
    while ( !done ) {
        dev.poll( 50 );
        ... do stuff ...
        ... update signals ...
    }

It is possible to retrieve a device's inputs or outputs by name or by
index at a later time using the functions `dev.input()` or `dev.output()`
with either the signal name or index as an argument. The functions
`dev.inputs()` and `dev.outputs()` return an object of type  `mapper::Signal::Iterator` which can be used to retrieve all of the
input/output signals belonging to a particular device:

    std::cout << "Signals belonging to " << dev.name() << std::endl;

    mapper::Signal::Iterator iter = dev.inputs().begin();
    for (; iter != dev.inputs().end(); iter++) {
        std::cout << "input: " << (*iter).full_name() << std::endl;
    }

Updating signals
----------------

We can imagine the above program getting sensor information in a loop.
It could be running on an network-enable ARM device and reading the
ADC register directly, or it could be running on a computer and
reading data from an Arduino over a USB serial port, or it could just
be a mouse-controlled GUI slider.  However it's getting the data, it
must provide it to _libmapper_ so that it will be sent to other
devices if that signal is mapped.

This is accomplished by the `update()` function:

    void sig.update( void *value,
                     int count,
                     mapper.Timetag timetag );

The `count` and `timetag` arguments can be ommited, and the `update()`
function is overloaded to accept scalars, arrays, and vectors as appropriate for the datatype and lengthof the signal in question.
In other words, if the signal is a 10-vector of `int`, then `value`
should point to an array or vector of 10 `int`s.  If it is a
scalar `float`, it should be provided with a `float`
variable. The `count` argument allows you to specify the number of
value samples that are being updated - for now we will set this to 1.
Lastly the `timetag` argument allows you to specify a time associated with
the signal update. If your value update was generated locally, or if your
program does not have access to upstream timing information (e.g., from a
microcontroller sampling sensor values), you can omit the argument
and libmapper will tag the update with the current time.

So in the "sensor 1 voltage" example, assuming in "do stuff" we have
some code which reads sensor 1's value into a float variable called
`v1`, the loop becomes:

    while ( !done ) {
        dev.poll( 50 );
        float v1 = read_sensor_1();
        sensor1_voltage.update( v1 );
    }

This is about all that is needed to expose sensor 1's voltage to the
network as a mappable parameter.  The _libmapper_ GUI can now map this
value to a receiver, where it could control a synthesizer parameter or
change the brightness of an LED, or whatever else you want to do.

Signal conditioning
-------------------

Most synthesizers of course will not know what to do with
"voltage"--it is an electrical property that has nothing to do with
sound or music.  This is where _libmapper_ really becomes useful.

Scaling or other signal conditioning can be taken care of _before_
exposing the signal, or it can be performed as part of the mapping.
Since the end user can demand any mathematical operation be performed
on the signal, he can perform whatever mappings between signals as he
wishes.

As a developer, it is therefore your job to provide information that
will be useful to the end user.

For example, if sensor 1 is a position sensor, instead of publishing
"voltage", you could convert it to centimeters or meters based on the
known dimensions of the sensor, and publish a "/sensor1/position"
signal instead, providing the unit information as well.

We call such signals "semantic", because they provide information with
more meaning than a relatively uninformative value based on the
electrical properties of the sensing technqiue.  Some sensors can
benefit from low-pass filtering or other measures to reduce noise.
Some sensors may need to be combined in order to derive physical
meaning.  What you choose to expose as outputs of your device is
entirely application-dependent.

You can even publish both "/sensor1/position" and "/sensor1/voltage"
if desired, in order to expose both processed and raw data.  Keep in
mind that these will not take up significant processing time, and
_zero_ network bandwidth, if they are not mapped.

Receiving signals
-----------------

Now that we know how to create a sender, it would be useful to also
know how to receive signals, so that we can create a sender-receiver
pair to test out the provided mapping functionality.

As mentioned above, the `add_input()` function takes an optional
`handler` and `user_data`.  This is a function that will be called
whenever the value of that signal changes.  To create a receiver for a
synthesizer parameter "pulse width" (given as a ratio between 0 and
1), specify a handler when calling `add_input()`.  We'll imagine
there is some C++ synthesizer implemented as a class `Synthesizer`
which has functions `setPulseWidth()` which sets the pulse width in a
thread-safe manner, and `startAudioInBackground()` which sets up the
audio thread.

Create the handler function, which is fairly simple,

    void pulsewidth_handler ( mapper::Signal msig,
                              int instance_id,
                              void *value,
                              int count,
                              mapper::Timetag tt )
    {
        Synthesizer *s = (Synthesizer*) msig.properties()->user_data;
        s->setPulseWidth( *(float*)v );
    }

First, the pointer to the `Synthesizer` instance is extracted from the
`user_data` pointer, then it is dereferenced to set the pulse width
according to the value pointed to by `v`.

Then `main()` will look like,

    void main()
    {
        Synthesizer synth;
        synth.startAudioInBackground();
        
        float min_pw = 0.0f;
        float max_pw = 1.0f;
        
        mapper::Device my_receiver( "test_receiver" );
        
        mapper::Signal synth_pulsewidth =
            dev.add_input( "/synth/pulsewidth", 1, 'f', 0, &min_pw,
                           &max_pw, pulsewidth_handler, &synth );
        
        while ( !done )
            dev.poll( 50 );
    }

Working with timetags
=====================
_libmapper_ uses the `Timetag` data structure to store
[NTP timestamps](http://en.wikipedia.org/wiki/Network_Time_Protocol#NTP_timestamps).
For example, the handler function called when a signal update is received
contains a `timetag` argument.  This argument indicates the time at
which the source signal was _sampled_ (in the case of sensor signals)
or _generated_ (in the case of sequenced or algorithimically-generated
signals).

When updating output signals, using the function `update()` without a
timetag argument will automatically label the outgoing signal
update with the current time. In cases where the update should more
properly be labeled with another time, this can be accomplished by
including the timetag argument.  This timestamp should only be
overridden if your program has access to a more accurate measurement
of the real time associated with the signal update, for example if
you are writing a driver for an outboard sensor system that provides
the sampling time.

_libmapper_ also provides helper functions for getting the current
device-time, setting the value of a `Timetag` from other
representations, and comparing or copying timetags.  Check the API
documentation for more information.

Working with signal instances
=============================
_libmapper_ also provides support for signals with multiple _instances_,
for example:

* control parameters for polyphonic synthesizers;
* touches tracked by a multitouch surface;
* "blobs" identified by computer vision systems;
* objects on a tabletop tangible user interface;
* _temporal_ objects such as gestures or trajectories.

The important qualities of signal instances in _libmapper_ are:

* **instances are interchangeable**: if there are semantics attached
  to a specific instance it should be represented with separate signals
  instead.
* **instances can be ephemeral**: signal instances can be dynamically
  created and destroyed. _libmapper_ will ensure that linked devices
  share a common understanding of the relatonships between instances
  when they are mapped.
* **one mapping connection serves to map all of its instances**

All signals possess one instance by default. If you would like to reserve
more instances you can use:

    sig.reserve_instances( int num )

After reserving instances you can update a specific instance, for example:

    sig.update_instance( int instance_id,
                         void *value,
                         int count,
                         Timetag timetag)

All of the arguments except one should be familiar from the
documentation of `update()` presented earlier.
The `instance_id` argument does not have to be considered as an array
index - it can be any integer that is convenient for labelling your
instance. _libmapper_ will internally create a map from your id label
to one of the preallocated instance structures.

Receiving instances
-------------------
You might have noticed earlier that the handler function called when
a signal update is received has a argument called `instance_id`. Here
is the function prototype again:

    void mapper_signal_update_handler(mapper::Signal msig,
                                      int instance_id,
                                      void *value,
                                      int count,
                                      mapper::Timetag tt);

Under normal usage, this argument will have a value (0 <= n <= num_instances)
and can be used as an array index. Remember that you will need to reserve
instances for your input signal using `sig.reserve_instances()` if you
want to receive instance updates.

Instance Stealing
-----------------

For handling cases in which the sender signal has more instances than
the receiver signal, the _instance allocation mode_ can be set for an
input signal to set an action to take in case all allocated instances are in
use and a previously unseen instance id is received. Use the function:

    sig.set_instance_allocation_mode( instance_allocation_type mode );

The argument `mode` can have one of the following values:

* `IN_UNDEFINED` Default value, in which no stealing of instances will occur;
* `IN_STEAL_OLDEST` Release the oldest active instance and reallocate its
  resources to the new instance;
* `IN_STEAL_NEWEST` Release the newest active instance and reallocate its
  resources to the new instance;

If you want to use another method for determining which active instance
to release (e.g. the sound with the lowest volume), you can create an `instance_event_handler` for the signal and write the method yourself:

    void my_handler(mapper::Signal msig,
                    int instance_id,
                    msig_instance_event_t event,
                    mapper::Timetag tt)
    {
        // user code chooses which instance to release
        int id = choose_instance_to_release(msig);

        msig.release_instance(id, tt);
    }

For this function to be called when instance stealing is necessary, we
need to register it for `IN_OVERFLOW` events:

    msig.set_instance_event_callback( my_handler,
                                      IN_OVERFLOW,
                                      *user_context);

Publishing metadata
===================

Things like device names, signal units, and ranges, are examples of
metadata--information about the data you are exposing on the network.

_libmapper_ also provides the ability to specify arbitrary extra
metadata in the form of name-value pairs.  These are not interpreted
by _libmapper_ in any way, but can be retrieved over the network.
This can be used for instance to give a device X and Y information, or
to perhaps give a signal some property like "reliability", or some
category like "light", "motor", "shaker", etc.

Some GUI implementing a Monitor could then use this information to
display information about the network in an intelligent manner.

Any time there may be extra knowledge about a signal or device, it is
a good idea to represent it by adding such properties, which can be of
any OSC-compatible type.  (So, numbers and strings, etc.)

The property interface is through the functions,

    void dev.properties.set( <name>, <value> );

    void sig.set_property( <name>, <value> );

The `<value>` arguments can be a scalar, array or std::vector of type
`int`, `float`, `double`, or `char*`.

For example, to store a `float` indicating the X position of a device,
you can call it like this:

    dev.set_property( "x", 12.5f );
    sig.set_property( "sensingMethod", "resistive" );

In general you can use any property name not already in use by the
device or signal data structure.  Reserved words for signals are:

    device_name, direction, length, max, min, name, type, unit, user_data;

for devices, they are:

    host, port, name, user_data.

By the way, if you query or set signal properties using these
keywords, you will get or modify the same information that is
available directly from the `mapper::DeviceProps` data structure.
Therefore this can provide a unified string-based method for accessing
any signal property:

    mapper::SignalProps props = sig.properties();
    mapper::Property = props.get("sensingMethod");

Primarily this is an interface meant for network monitors, but may
come in useful for an application implementing a device.
