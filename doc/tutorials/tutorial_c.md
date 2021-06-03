# Getting started with libmapper

Since _libmapper_ uses GNU autoconf, getting started with the library is the
same as any other library on Linux; use `./configure` and then `make` to compile
it.  You'll need `swig` available if you want to compile the Python bindings.
On Mac OS X, we provide a precompiled Framework bundle for 32- and 64-bit Intel
platforms, so using it with XCode should be a matter of including it in your
project.

## Overview of the API structure

If you take a look at the [API documentation](../html/index.html), there is a
section called "modules".  This is divided into the following main sections:

* [Graphs](../html/group__graphs.html)
* [Objects](../html/group__objects.html)
    * [Devices](../html/group__devices.html)
    * [Signals](../html/group__signals.html) and [Signal Instances](../html/group__instances.html)
    * [Maps](../html/group__maps.html)
* [Lists](../html/group__lists.html)

For this tutorial, the only sections to pay attention to are **Devices** and
**Signals**. The other sections are mostly used when building user interfaces for
designing mapping configurations. Functions and types from each module are prefixed
with `mpr_<module>_`, in order to avoid namespace clashing.

## Devices

### Creating a device

To create a _libmapper_ device, it is necessary to provide two arguments to the
function `mpr_dev_new`:

~~~c
mpr_dev mpr_dev_new(const char *name_prefix, mpr_graph graph);
~~~

Every device on the network needs a descriptive name – the name does not have to
be unique since during initialization a unique ordinal will be appended to the
device name. This allows multiple devices with the same name to exist on the
network.

The second argument is an optional graph instance. It is not necessary to provide
this, but can be used to specify different networking parameters, such as specifying
the name of the network interface to use. For this tutorial we will let libmapper
choose a default interface.

An example of creating a device:

~~~c
mpr_dev my_dev = mpr_dev_new("test", 0);
~~~

### Polling the device

The device lifecycle looks like this:

<img style="display:block;margin:auto;padding:0px;width:75%" src="./images/device_lifecyle.png">

In other words, after a device is created, it must be continuously polled during
its lifetime, and then explicitly freed when it is no longer needed.

The polling is necessary for several reasons: to respond to requests on the
admin bus; to check for incoming signals; to update outgoing signals.  Therefore
even a device that does not have signals must be polled.  The user program must
organize to have a timer or idle handler which can poll the device often enough.
Polling interval is not extremely sensitive, but should be at least 100 ms or
less.  The faster it is polled, the faster it can handle incoming and outgoing
signals.

The `mpr_dev_poll` function can be blocking or non-blocking, depending on how
you want your application to behave.  It takes a number of milliseconds during
which it should do some work, or 0 if it should check for any immediate actions
and then return without waiting:

~~~c
int mpr_dev_poll(mpr_dev dev, int block_ms);
~~~

An example of calling it with non-blocking behaviour:

~~~c
mpr_dev_poll(my_dev, 0);
~~~

If your polling is in the middle of a processing function or in response to a
GUI event for example, non-blocking behaviour is desired.  On the other hand if
you put it in the middle of a loop which reads incoming data at intervals or
steps through a simulation for example, you can use `mpr_dev_poll` as your
"sleep" function, so that it will react to network activity while waiting.

It returns the number of messages handled, so optionally you could continue to
call it until there are no more messages waiting.  Of course, you should be
careful doing that without limiting the time it will loop for, since if the
incoming stream is fast enough you might never get anything else done!

Note that an important difference between blocking and non-blocking polling is
that during the blocking period, messages will be handled immediately as they
are received.  On the other hand, if you use your own sleep, messages will be
queued up until you can call poll(); stated differently, it will "time-quantize"
the message handling.  This is not necessarily bad, but you should be aware of
this effect.

Since there is a delay before the device is completely initialized, it is
sometimes useful to be able to determine this using `mpr_dev_ready`.
Only when `mpr_dev_ready` returns non-zero is it valid to use the device's
name.

### Freeing the device

It is necessary to explicitly free the device at the end of your program.  This
not only frees memory, but also sends some messages to "politely" remove itself
from the network.

An example of freeing a device:

~~~c
mpr_dev_free(my_dev);
~~~

## Signals

Now that we know how to create a device, poll it, and free it, we only need to
know how to add signals in order to give our program some input/output
functionality.  While _libmapper_ enables arbitrary connections between _any_
declared signals, we still find it helpful to distinguish between two type of
signals: `inputs` and `outputs`. 

* `outputs` signals are _sources_ of data, updated locally by their parent
  device
* `inputs` signals are _consumers_ of data and are **not** generally updated
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

~~~c
mpr_sig mpr_sig_new(mpr_dev parent, mpr_dir dir, const char *name, int length,
                    mpr_type type, const char *unit, void *min, void *max,
                    int *num_inst, mpr_sig_handler *h, int events);
~~~

The only _required_ parameters here are the signal direction, name, vector length
and data type.  Signals are assumed to be vectors of values, so for usual
single-valued signals, a length of 1 should be specified.  Finally, supported types
are currently `MPR_INT32`, `MPR_FLT`, or `MPR_DBL` for
[integer, float, and double](https://en.wikipedia.org/wiki/C_data_types)
values, respectively.

The other parameters are not strictly required, but the more information you
provide, the more _libmapper_ can do some things automatically.  For example, if
`min` and `max` are provided, it will be possible to create linear-scaled
connections very quickly.  If `unit` is provided, _libmapper_ will be able to
similarly figure out a linear scaling based on unit conversion (from cm to
inches for example). Currently automatic unit-based scaling is not a supported
feature, but will be added in the future.  You can take advantage of this
future development by simply providing unit information whenever it is
available.  It is also helpful documentation for users.

Notice that these optional values are provided as `void*` pointers.  This is
because a signal can either be `int`, `float`, or `double`, and your maximum and
minimum values should correspond in type and length.  So you should pass in a
`int*`, `float*`, or `double*` by taking the address of a local variable.

Lastly, it is usually necessary to be informed when input signal values change.
This is done by providing a function to be called whenever its value is modified
by an incoming message.  It is passed in the `handler` parameter, along with an
`events` parameter specifying which types of events should trigger the handler –
for input signals.

An example of creating a "barebones" `int` scalar output signal with no unit,
minimum, or maximum information and no callback handler:

~~~c
mpr_sig outA = mpr_sig_new(dev, MPR_DIR_OUT, "outA",
                           1, MPR_INT32, 0, 0, 0, 0, 0, 0);
~~~

An example of a `float` signal where some more information is provided:

~~~c
float min = 0.0f;
float max = 5.0f;
mpr_sig s1 = mpr_sig_new(dev, MPR_DIR_OUT, "sensor1", 1, MPR_FLT,
                         "V", &min, &max, 0, 0, 0);
~~~

So far we know how to create a device and to specify an output signal for it.
To recap, let's review the code so far:

~~~c
mpr_dev dev = mpr_dev_new("my_device", 0, 0);
mpr_sig sig = mpr_sig_new(dev, MPR_DIR_OUT, "sensor1", 1, MPR_FLT,
                          "V", &min, &max, 0, 0, 0);
    
while (!done) {
    mpr_dev_poll(dev, 50);
    ... do stuff ...
    ... update signals ...
}
    
mpr_dev_free(dev);
~~~

Note that although you have a pointer to the mpr_sig structure (which was
returned by `mpr_sig_new()`), its memory is "owned" by the
device.  In other words, you should not worry about freeing its memory - this
will happen automatically when the device is destroyed.  It is possible to
retrieve a device's inputs or outputs by name or by index at a later time using
the functions `mpr_dev_get_sigs()`.

### Updating signals

We can imagine the above program getting sensor information in a loop.  It could
be running on an network-enable ARM device and reading the ADC register
directly, or it could be running on a computer and reading data from an Arduino
over a USB serial port, or it could just be a mouse-controlled GUI slider.
However it's getting the data, it must provide it to _libmapper_ so that it will
be sent to other devices if that signal is mapped.

This is accomplished by the function `mpr_sig_set_value()`:

~~~c
void mpr_sig_set_value(mpr_sig sig, mpr_id inst, int length,
                       mpr_type type, void *value);
~~~

As you can see, a `void*` pointer must be provided, which must point to a data
structure matching the `length` and `type` arguments. This data structure is not
required to match the length and type of the signal itself—libmapper will perform
type coercion if necessary.

So in the "sensor 1" example, assuming in `do_stuff()` we have some code which
reads sensor 1's value into a float variable called `v1`, the loop becomes:

~~~c
while (!done) {
    mpr_dev_poll(my_dev, 50);
    
    // call hypothetical user function that reads a sensor
    float v1 = do_stuff();
    mpr_sig_set_value(sig, 0, 1, MPR_FLT, &v1);
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
the sensor, and publish a "sensor1/position" signal instead, providing the unit information as well.

We call such signals "semantic", because they provide information with more
meaning than a relatively uninformative value based on the electrical properties
of the sensing technqiue.  Some sensors can benefit from low-pass filtering or
other measures to reduce noise.  Some sensors may need to be combined in order
to derive physical meaning.  What you choose to expose as outputs of your device
is entirely application-dependent.

Best of all, you can publish both "sensor1/position" and "sensor1/voltage" if
desired, in order to expose both processed and raw data.  Keep in mind that
these will not take up significant processing time, and _zero_ network
bandwidth, if they are not mapped.

### Receiving signals

Now that we know how to create a sender, it would be useful to also know how to
receive signals, so that we can create a sender-receiver pair to test out the
provided mapping functionality. The current value and timestamp for a signal can
be retrieved at any time by calling `mpr_sig_get_value()`, however for
event-driven applications you may want to be informed of new values as they are
received or generated.

As mentioned above, the `mpr_sig_new()` function takes an optional `handler` and
`events`.  This is a function that will be called whenever the value of that
signal changes or instances events occur.  To create a receiver for a
synthesizer parameter "pulse width" (given as a ratio between 0 and 1), specify
a handler when calling `mpr_sig_new()`.  We'll imagine there is some C++
synthesizer implemented as a class `Synth` which has functions `setPulseWidth()`
which sets the pulse width in a thread-safe manner, and `startAudioInBackground()`
which sets up the audio thread.

Create the handler function, which is fairly simple,

~~~c
void pw_handler (mpr_sig sig, mpr_int_evt evt, mpr_id inst, int length,
                 mpr_type type, void *value, mpr_time_t *time)
{
    if (!length || !value)
        return;
    Synth *s = (Synth*) mpr_obj_get_prop_ptr(sig, MPR_PROP_DATA, 0);
    s->setPulseWidth(*(float*)v);
}
~~~

First, the pointer to the `Synth` instance is extracted from the `MPR_PROP_DATA`
property, then it is dereferenced to set the pulse width according to the value
pointed to by `v`.

Then `main()` will look like,

~~~c
void main()
{
    Synth synth;
    synth.startAudioInBackground();
    
    float min_pw = 0.0f;
    float max_pw = 1.0f;
    
    mpr_dev synth_dev = mpr_dev_new("synth", 0);
    
    mpr_sig pw = mpr_sig_new(synth_dev, MPR_DIR_IN, "pulsewidth", 1, 'f', 0,
                             &min_pw, &max_pw, 0, pulsewidth_handler,
                             MPR_SIG_UPDATE);
    mpr_obj_set_prop(pw, MPR_PROP_DATA, 0, 1, MPR_PTR, &synth, 0);
    
    while (!done)
        mpr_dev_poll(synth_dev, 50);
    
    mpr_dev_free(synth_dev);
}
~~~

## Working with timetags

_libmapper_ uses the `mpr_time_t` data structure to store
[NTP timestamps](http://en.wikipedia.org/wiki/Network_Time_Protocol#NTP_timestamps).
For example, the handler function called when a signal update is received
contains a `timetag` argument.  This argument indicates the time at which the
source signal was _sampled_ (in the case of sensor signals) or _generated_ (in
the case of sequenced or algorithimically-generated signals).

_libmapper_ provides helper functions for getting the current device-time,
setting the value of a `mpr_time` structure from other representations, and comparing or
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
* **map once for all instances**: one mapping connection serves to map all of
  its instances.

All signals possess one instance by default. If you would like to reserve more
instances you can use:

~~~c
int mpr_sig_reserve_inst(mpr_sig sig, int num, mpr_id *ids, void **data);
~~~

If the `ids` argument is null _libmapper_ will automatically assign unique ids to
the reserved instances.

After reserving instances you can update a specific instance using:

~~~c
void mpr_sig_set_value(mpr_sig signal, mpr_id instance, int length,
                       mpr_type type, const void *value);
~~~

The `instance` argument does not have to be considered as an array index - it
can be any value that is convenient for labelling your instance. _libmapper_
will internally create a map from your id label to one of the preallocated
instance structures.

### Receiving instances

You might have noticed earlier that the handler function called when a signal
update is received has a argument called `inst`. Here is the function
prototype again:

~~~c
void mpr_sig_handler(mpr_sig sig, mpr_inst_evt evt, mpr_id inst, int len,
                     mpr_type type, const void *value, mpr_time time);
~~~

Under normal usage, this argument will have a value (0 <= n <= num_instances)
and can be used as an array index. Remember that you will need to reserve
instances for your input signal using `mpr_sig_reserve_inst()` if you want
to receive instance updates.

### Instance Stealing

For handling cases in which the sender signal has more instances than the
receiver signal, the _instance allocation mode_ can be set for an input signal
to set an action to take in case all allocated instances are in use and a
previously unseen instance id is received. Use the function `mpr_obj_set_prop()`.


The argument `mode` can have one of the following values:

* `MPR_STEAL_NONE` Default value, in which no stealing of instances will
  occur;
* `MPR_STEAL_OLDEST` Release the oldest active instance and reallocate its
  resources to the new instance;
* `MPR_STEAL_NEWEST` Release the newest active instance and reallocate its
  resources to the new instance;

If you want to use another method for determining which active instance to
release (e.g. the sound with the lowest volume), you can create an handler
for the signal and write the method yourself:

~~~c
void my_handler(mpr_sig sig, mpr_int_evt evt, mpr_id inst, int len,
                mpt_type type, const void *value, mpr_time time)
{
    // hypothetical user code chooses which instance to release
    mpr_id release_me = choose_instance_to_release(sig);

    mpr_sig_release_inst(sig, release_me);
    
    // now an instance is available
}
~~~

For this function to be called when instance stealing is necessary, we need to
register it for `MPR_SIG_INST_OFLW` events:

~~~c
mpr_sig_set_cb(sig, my_handler, MPR_SIG_INST_OFLW);
~~~

## Publishing metadata

Things like device names, signal units, and ranges, are examples of metadata
--information about the data you are exposing on the network.

_libmapper_ also provides the ability to specify arbitrary extra metadata in the
form of name-value pairs.  These are not interpreted by _libmapper_ in any way,
but can be retrieved over the network.  This can be used for instance to give a
device X and Y information, or to perhaps give a signal some property like
"reliability", or some category like "light", "motor", "shaker", etc.

Some GUI could then use this information to display information about the network
in an intelligent manner.

Any time there may be extra knowledge about a signal or device, it is a good
idea to represent it by adding such properties, which can be of any
OSC-compatible type.  (So, numbers and strings, etc.)

The property interface is through the functions,

~~~c
void mpr_obj_set_prop(mpr_obj obj, mpr_prop prop, const char *key,
                      int length, char type, void *value, int publish);
                      
mpr_prop mpr_obj_get_prop_by_idx(mpr_obj obj, mpr_prop prop, const char **key,
                                 int *len, mpr_type *type, const void **val,
                                 int *pub);
mpr_prop mpr_obj_get_prop_by_key(mpr_obj obj, const char *key, int *len,
                                 mpr_type *type, const void **val, int *pub);
~~~

The type of the `value` argument is specified by `type`: floats are MPR_FLT,
32-bit integers are MPR_INT32, doubles are MPR_DBL, and strings are MPR_STR.

For example, to store a `float` indicating the X position of a device, you can
call it like this:

~~~c
float x = 12.5;
mpr_obj_set_prop(my_device, 0, "x", 1, MPR_FLT, &x, 1);

char *sensingMethod = "resistive";
mpr_obj_set_prop(sensor1, 0, "sensingMethod", 1, MPR_STR, sensingMethod);
~~~

If the parent object (a device and a signal in this case) is *local* the property
change takes place immediately. If the object is *remote* the property change is only
staged and must be pushed out to the network using the functions `mpr_obj_push()`.

### Reserved keys

You can use any property name not already reserved by _libmapper_.

#### Reserved keys for devices

`data`, `id`, `is_local`, `lib_version`, `linked`, `name`, `num_maps_in`,
`num_maps_out`, `num_sigs_in`, `num_sigs_out`, `ordinal`, `status`, `synced`,
`version`

#### Reserved keys for signals

`data`, `device`, `direction`, `id`, `is_local`, `jitter`, `length`, `max`, `maximum`,
`min`, `minimum`, `name`, `num_inst`, `num_maps_in`, `num_maps_out`, `period`, `steal`,
`type`, `unit`, `use_inst`, `version`

#### Reserved keys for maps

`data`, `expr`, `id`, `is_local`, `muted`, `num_sigs_in`, `process_loc`, `protocol`,
`scope`, `status`, `use_inst`, `version`
