
# libmapper NEWS

Changes from 0.2 to 0.3
-----------------------

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
    that the mapper signal is actually non-existant, i.e. no value is
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



Changes from 0.1 to 0.2
-----------------------

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

- Crash when non-existant signal receives /disconnect.

Initial release 0.1 
-------------------

Released Dec. 10, 2010.
