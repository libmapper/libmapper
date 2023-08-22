<h1 align=center>
  <a href="http://libmapper.github.io" title="libmapper Documentation">
    <img alt="libmapper" src="http://libmapper.github.io/images/libmapper_logo_black_512px.png" style="width:200px">
  </a>
  <br>
  libmapper
</h1>

<p align=center>
  <a href="./LICENSE">
    <img
      alt="license:lgpl"
      src="https://img.shields.io/badge/license-LGPL v2.1-green.svg?style=flat-square"
    />
  </a>
  <a href="https://github.com/libmapper/libmapper/releases">
    <img
      alt="tag:?"
      src="https://img.shields.io/github/tag/libmapper/libmapper.svg?style=flat-square"
    />
  </a>
  <a href="https://github.com/libmapper/libmapper/releases">
    <img
      alt="downloads:?"
      src="https://img.shields.io/github/downloads/libmapper/libmapper/total.svg?style=flat-square"
    />
  </a>
  <a href="https://github.com/libmapper/libmapper/actions/workflows/ci.yml">
    <img
      alt="status:?"
      src="https://github.com/libmapper/libmapper/actions/workflows/ci.yml/badge.svg"
    />
  </a>
  <br/>
  <a href="https://github.com/libmapper/libmapper">
    <img
      alt="forks:?"
      src="https://img.shields.io/github/forks/libmapper/libmapper.svg?style=social"
    />
  </a>
  <a href="https://github.com/libmapper/libmapper">
    <img
      alt="followers:?"
      src="https://img.shields.io/github/stars/libmapper/libmapper.svg?style=social"
    />
  </a>
  <a href="https://github.com/libmapper/libmapper">
    <img
      alt="watchers:?"
      src="https://img.shields.io/github/watchers/libmapper/libmapper.svg?style=social"
    />
  </a>
</p>

This library is a system for representing input and output signals on a network
and allowing arbitrary "mappings" to be dynamically created between them.

A "mapping", or "connection" associated with relational properties, consists of
an [Open Sound Control](http://opensoundcontrol.org/) stream being established
between source signals and a destination signal – the source is translated to
the destination's expected format, with some mathematical expression used to
condition the transmitted values as desired.  This can be used for example to
connect a set of sensors to a synthesizer's input parameters.

To get started quickly with libmapper, be sure to read the tutorials, found in
the "doc" folder in this distribution.

History of the mapper project
-----------------------------

This project began life as a tool for helping composers and performers to more
easily play with custom-built musical instruments, such that a program that
reads data from the instrument (e.g. from an [Arduino](http://www.arduino.cc/)
embedded microcontroller) can be connected to the inputs of a sound synthesizer.
The first version of this software was written entirely in Cycling 74's
[Max/MSP](http://www.cycling74.com/), but in order to make it more universally
useful and cross-platform, we decided to re-implement the protocol in C; hence,
libmapper.

We were already using [Open Sound Control](http://opensoundcontrol.org/) for
this purpose, but needed a way to dynamically change the mappings – not only to
modify the destinations of OSC messages, but also to change scaling, or to
introduce derivatives, integration, frequency filtering, smoothing, etc.
Eventually this grew into the use of a multicast bus to publish signal metadata
and to administrate peer-to-peer connections between machines on a local subnet,
with the ability to specify arbitrary mathematical expressions that can be used
to perform signal conditioning.

It has been designed with the idea of decentralization in mind.  We specifically
wanted to avoid keeping a "master" node which is responsible for arbitrating
connections or republishing data, and we also wanted to avoid needing to keep a
central database of serial numbers or other unique identifiers for devices
produced in the lab.  Instead, common communication is performed on a multicast
UDP port, which all nodes listen on, and this is used to implement a
collision-handling protocol that can assign a unique ID to each device that
appears.  Actual signal data is sent directly from a sender device to the
receiver using either UDP or TCP.

Advantages of libmapper
-----------------------

This library is, on one level, our response to the widely acknowledged lack of a
"query" protocol for Open Sound Control.  We are aware for example of
[current work on using the ZeroConf protocol for publishing OSC services]
(http://sourceforge.net/projects/osctools/), and various efforts to standardize
a certain set of OSC address conventions.

However, libmapper covers considerably more ground than simply the ability to
publish namespaces, and handles a direct need that we experience in our daily
work.  We evaluated the use of ZeroConf early on in this work, but we eventually
decided that sending OSC messages over multicast was just as easy and required
the integration of fewer dependencies.  With the addition of being able to
control message routing and to dynamically specify [signal conditioning
equations](./doc/expression_syntax.md), we feel this work represents a significant enough effort that it is
worth making available to the general public.

It can also be seen as an open source alternative to some commercial products
such as Camille Troillard's [Osculator](http://www.osculator.net/) and STEIM's
[junXion](http://www.steim.org/steim/junxion_v4.html), albeit certainly more
"barebones" for the moment.

In addition to passing signal data through mapping connections, libmapper
provides the ability to query or stream the state of the inputs on a destination
device.  This permits the use of intermediate devices that use supervised
machine learning techniques to "learn" mappings automatically.

A major difference in libmapper's approach to handling devices is the idea that
each "driver" can be a separate process, on the same or different machines in
the network.  By creating a C library with a basic interface and by providing
Python and Java bindings, we hope to enable a very wide variety of programming
language bindings.
Another advantage of a C library is portability: we have demonstrated libmapper
working on a Gumstix device, an ethernet-enabled ARM-based microcomputer running
Linux that can be easily embedded directly into an instrument.

We also provide "external" Max/MSP and [PureData](http://puredata.info) objects,
to help integrate libmapper support into programs created in these popular
dataflow languages.

*Convergent* (or *many-to-one*) mapping refers to situations in which multiple
data sources are routed to a single destination.  In early versions of libmapper
the updates from each source would simple be interleaved, but starting with
version 0.4 signal processing can be applied that specifically addresses each
source stream and allows arbitrary combining functions.

Known limitations
-----------------

The "devices and signals" metaphor encapsulated by libmapper is of course not
the be-all and end-all of mapping.  For signal datatypes we only support
homogeneous vectors numbers (integer, float, or double) rather than allowing
more complex structured data, since we believe that configurability and
complatibility are best served when *models* are not embedded in the protocol.
Using libmapper, complex data structures should be represented as collections
of well-labeled signals.

If libmapper is to be used for visual art, transmission of matrixes or even
image formats might be interesting but is not currently supported since there is
no standard way to represent this in Open Sound Control other than as a binary
"blob".  It's possible that in this case a protocol such as HTTP that supports
MIME type headers is simply a more appropriate choice.

That aside, mapping techniques based on table-lookup or other data-oriented
processes don't fit directly into the "connection" paradigm used here.  Of
course, many of these techniques can easily be implemented as "intermediate"
devices that sit between a sender and receiver.

One impact of peer-to-peer messaging is that it may suffer from redundancy.  In
general it may be more efficient to send all data once to a central node, and
then out once more to nodes that request it, at the expense of weighing down the
bandwidth of that particular central node.  In libmapper's case, if 50 nodes
subscribe to a particular signal, it will be repeated that many times by the
originating node.  Dealing with centralized-vs-decentralized efficiency issues
by automatically optimizing decisions on how messages are distributed and where
routing takes place is not impossible, but represents non-trivial work.
Optimizing of message-passing efficiency/network topology is not a near-term
goal, but in the meantime it is entirely possible to use libmapper to explicitly
create a centralized network if desired; this will simply imply more overhead in
managing connections.

Future plans
------------

We chose to use Open Sound Control for this because of its compatibility with a
wide variety of audio and media software.  It is a practical way to transmit
arbitrary data in a well-defined binary format.  In practice, since the actual
connections are peer-to-peer, they could technically be implemented using a
variety of protocols.  High-frequency data for example might be better
transmitted using the [JACK Audio Connection Kit](http://jackaudio.org) or
something similar.  In addition, on embedded devices, it might make sense to
"transmit" data between sensors and synthesizers through shared memory, while
still allowing the use of the libmapper admin protocol and GUIs to dynamically
experiment with mapping.

In the syntax for mathematical expressions, we include a method for indexing
previous values to allow basic filter construction.  This is done by index, but
we also implemented a syntax for accessing previous state according to time in
seconds.  This feature is not yet usable, but in the future interpolation will
be performed to allow referencing of time-accurate values.

The explicit use of timing information may also be useful for certain mapping
scenarios, such as "debouncing" signal updates or adding adaptive delays.  We
are gradually working towards such functionality, developing a syntax for
referring to timing data when configuring connection properties and implementing
a lightweight synchronization scheme between linked devices that will permit
jitter mitigation and correct handling of delays when devices are running on
separate computers.

Getting libmapper
-----------------

The latest version of libmapper source code and binaries can be downloaded from
the libmapper website at:

<http://libmapper.org>

or from the libmapper page on the IDMIL website,

<http://idmil.org/software/libmapper>

Building and using libmapper
----------------------------

Please see the separate [documentation for building libmapper](./doc/how_to_compile_and_run.md), [tutorials](./doc/tutorials) on using
its API in C, C++, Python, Java, MaxMSP and Pure Data, and doxygen-generated [API
documentation](./doc/html/index.html), in the "doc" directory.

License
-------

This software was written in the Input Devices and Music Interaction Laboratory
at McGill University in Montreal, and is copyright those found in the AUTHORS
file.  It is licensed under the GNU Lesser Public General License version 2.1 or
later.  Please see COPYING for details.

In accordance with the LGPL, you are allowed to use it in commercial products
provided it remains dynamically linked, such that libmapper always remains free
to modify.  If you'd like to use it in an outstanding context, please contact
the AUTHORS to seek an agreement.

Dependencies of libmapper are:

* [liblo](http://liblo.sourceforge.net), LGPL

Optional dependencies for the Python bindings:

* [Python](http://www.python.org), Python license, LGPL-compatible

Used only in the examples:

* [RtAudio](http://www.music.mcgill.ca/~gary/rtaudio), MIT license,
  LGPL-compatible
