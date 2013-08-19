Getting started
===============

Note: this tutorial will introduce the original bindings for MaxMSP (before July 2013). You can access the tutorial for the revised multi-object bindings [here](./maxmsp_multiobj.html).

To start using the _libmapper_ with MaxMSP you will need to:

* Install _libmapper_ for your platform (Linux, OSX, Windows)
* Download the `[mapper]` external object from our [downloads page](../downloads.html).
Alternatively, you can build the object from
[source](http://github.com/malloch/mapper-max-pd) instead.
* Download a [graphical user interface](../downloads.html#GUIs) for
  creating and editing mapping connections.

Devices
=======

Creating a device
-----------------

To create a _libmapper_ device, it is necessary to provide a device
name to the `[mapper]` object using the property `@alias`.
There is an initialization period after a device is created where
a unique ordinal is chosen to append to the device name.
This allows multiple devices with the same name to
exist on the network. If no name is given libmapper will choose a
name for your device starting with the string "maxmsp".

If desired, a device definition file can be specified with the property
`@definition` or `@def`, which the external will use to add signals
to your device. Otherwise, you device will start with no inputs
or outputs and you will need to add them using messages. In this tutorial,
we will assume that you do not have a prepared device definition file.

A third optional parameter of the `[mapper]` object is a network
interface name.  By default, libmapper will try to guess which network
interface to use for mapping, defaulting to the local loopback interface
ethernet or wifi is not available.  You can force the object to use a
particular interface by using the `@interface` property.

An example of creating a device:

![Creating a device.](./images/maxmsp_central1.png)

Once the object has initialized, it will output its metadata from the right
outlet:

* The complete device name, including an appended ordinal for distinguishing
between multiple active devices with the same name.
* The IP address and port in use by the object.
* The number of input and output signals associated with the object.
* The network interface in use by the object.

Signals
=======

Now that we have created a device, we only need to know how to
add signals in order to give our program some input/output functionality.

We'll start with creating a "sender", so we will first talk about how
to update output signals.

Creating a signal
-----------------

Creating a signal requires two pieces of information: 

* a name for the signal (must be unique within a devices inputs or outputs)
* the signal's data type expressed as a character 'i' for `integer`, 'f'
for `float`

Additional signal properties can also (optionally) be added:

* the signal's vector length
* the signal's unit
* the signal's minimum value
* the signal's maximum value

examples:

![Adding signals to a device](./images/maxmsp_central2.png)

The only _required_ parameters here are the signal name,
and data type.  If no `length` property is provided, the signal is
assumed to have length 1.  A signal name should start with "/",
as this is how it is represented in the OSC address.
(One will be added if you forget to do this.)
Finally, supported types are currently 'i' or 'f' for `int` or
`float` values, respectively.

The other parameters are not strictly required, but the more
information you provide, the more the mapper can do some things
automatically.  For example, if `minimum` and `maximum` are provided,
it will be possible to create linear-scaled connections very quickly.
If `unit` is provided, the mapper will be able to similarly figure out
a linear scaling based on unit conversion. (Centimeters to inches for
example.) Currently automatic unit-based scaling is not a supported
feature, but will be added in the future.  You can take advantage of
this future development by simply providing unit information whenever
it is available.  It is also helpful documentation for users.

An example of creating a "barebones" `int` scalar output signal with
no unit, minimum, or maximum information:

![Adding a simple integer output signal](./images/maxmsp_central3.png)

An example of a `float` signal where some more information is provided:

![Adding a floating-point output signal with some optional properties](./images/maxmsp_central4.png)

So far we know how to create a device and to specify an output signal
for it.


Updating signals
----------------

We can imagine the above program getting sensor information in a loop.
It could be running on a computer and reading data from an Arduino
over a USB serial port, or it could just be a mouse-controlled GUI
slider.  However it's getting the data, it must provide it to
_libmapper_ so that it will be sent to other devices if that signal
is mapped.

This is accomplished by passing messages to the `[mapper]` object
starting with the signal name:

    (<signal_name> <value>)
     |

So in the "sensor 1 voltage" example, assuming that we have some code
which reads sensor 1's value into a float variable in `[p read_sensor]`,
the patch becomes:

![Updating a signal](./images/maxmsp_central5.png)

This is about all that is needed to expose sensor 1's voltage to the
network as a mappable parameter.  The _libmapper_ GUI can now be used
to create a mapping between this value and a receiver, where it could
control a synthesizer parameter or change the brightness of an LED,
or whatever else you want to do.

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
electrical properties of the sensing technique.  Some sensors can
benefit from low-pass filtering or other measures to reduce noise.
Some sensor data may need to be combined in order to derive physical
meaning.  What you choose to expose as outputs of your device is
entirely application-dependent.

You can even publish both "/sensor1/position" and "/sensor1/voltage"
if desired, in order to expose both processed and raw data.  Keep in
mind that these will not take up significant processing time, and
_zero_ network bandwidth, if they are not mapped.

Receiving signals
-----------------

Receiving signals is even easier: after adding an input using the `add input`
message, updates for this signal will be routed to the left output
of the `[mapper]` object. Let's try making two devices in the same patch
for testing.

![Sending and receiving signal updates](./images/maxmsp_central6.png)

If you use your mapping GUI to create a link between the two devices
_sender_ and _receiver_ and a connection between your two signals
_/sendsig_ and _/recvsig_, any change made to the float value on the left
will cause a corresponding output on the right.

Congratulations - you have created your first mapping connection! This
probably seems quite simplistic, since you could have made a patch-cord
between the two float objects and accomplished the same thing, but your
"mapping" represents something more:

* It can be edited remotely from another machine on the network.
* It can connect signals on different computers.
* It can connect different signals implemented in different programming
 languages such as C, C++, Python, Java, and SuperCollider.
* It can be edited to provide signal processing, including automatic
 linear scaling, calibration, muting, clipping, or an arbitrary
 expression - even FIR and IIR filters.
 

Learn mode
----------

For patches with only outputs, lazy users can also declare the signals
automatically using `learn` mode. You can either send the message
"learn 1" to turn on learning ("learn 0" to turn it off) or you can
instantiate your `[mapper]` object with the learn property set as an
argument:

![Enabling learn mode on a device](./images/maxmsp_central7.png)

the object will watch its inlet for messages formatted as `/<name> value(s)`
and add any unknown signals automatically. Signals added this way will not
have associated minimum or maximum values, however, so it is usually
recommended to add the signals explicitly using messages.

Device definitions
------------------

If you wish, you can declare your libmapper signals using a device definition
file instead of sending messages to the `[mapper]` object. This file should be
correctly-formatted JSON with the following structure:

	{
	     "device" : {
	        "fileversion" : "dot-1",
	        "name" : "tester",
	        "inputs" : [
	            {
	                "name" : "/in1",
	                "type" : "f"
	            }
	        ],
	        "outputs" : [
	            {
	                "name" : "/out1",
	                "type" : "f",
	                "units" : "cm",
	                "minimum" : 4.0,
	                "maximum" : 5.1
	            }
	        ]
	    }
	}

To load your device definition, simply instantiate your `[mapper]` object with
the file name as a property:

![Loading a device definition file](./images/maxmsp_central8.png)

Publishing metadata
===================

Things like device names, signal units, and ranges, are examples of
metadata--information about the data you are exposing on the network.

_libmapper_ also provides the ability to specify arbitrary extra
metadata in the form of name-value pairs.  These are not interpreted
by _libmapper_ in any way, but can be retrieved over the network.
This can be used for instance to label a device with its location, or
to perhaps give a signal some property like "reliability", or some
category like "light", "motor", "shaker", etc.

Some GUI implementing a Monitor could then use this information to
display information about the network in an intelligent manner.

Any time there may be extra knowledge about a signal or device, it is
a good idea to represent it by adding such properties, which can be of
any OSC-compatible type.  (So, numbers and strings, etc.)

The MaxMSP bindings for libmapper do not currently allow dynamically
changing the properties of a device or signal, however they can be
declared when the entity is created by using jitter-style property
arguments

For example, to store a `float` indicating the X position of a device
`dev`, you could instantiate your object like this:

![Adding extra properties to a device](./images/maxmsp_central9.png)

To specify a string property of a signal:

![Adding extra properties to a signal](./images/maxmsp_central10.png)

In general you can use any property name not already in use by the
device or signal data structure.  Reserved words for signals are: 
`device_name` `direction` `length` `max` `min` `name` `type` `unit`

for devices, they are: `host` `port` `name`