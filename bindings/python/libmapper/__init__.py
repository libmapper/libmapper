"""
libmapper bindings for Python

This module provides Python bindings for using the C library _libmapper_. Libmapper implements a
system for representing input and output signals on a network and allowing arbitrary "mappings" to
be dynamically created between them.

A "mapping" represents a data-streaming association between one or more source signals and a
destination signal, in which data is transported using either shared memory or Open Sound Control
(OSC) streams over a network. In addition to traffic management, libmapper automatically handles
address and datatype translation, and enables the use of arbitrary mathematical expressions for
conditioning, combining and transforming the source values as desired. This can be used for example
to connect a set of sensors to a synthesizer's input parameters.

For more information please visit [libmapper.org](libmapper.org)

Classes and subclasses:
    Object: A generic representation of libmapper objects (Devices, Signals, Maps, and Graphs).
        Status (Enum): Describes the possible statuses for a libmapper object.

    Device: A named collection of Signals (and possibly other metadata).

    Signal: A named datastream; an input or output for a Device.
        Direction (IntFlag): The set of possible Signal directions, used for querying.
        Event (IntFlag): The set of possible signal events.
        InstanceStatus (IntFlag): The set of possible status flags for a signal instance.
        Stealing (Enum): The set of possible instance-stealing modes.

    Map: A dataflow configuration defined between a set of signals.
        Location (IntFlag): The set of possible Map endpoints/domains.
        Protocol (Enum): The set of possible network protocols used by a Map.

    Graph: A queriable representation of the distributed network of libmapper peers.
        Event (Enum): The set of possible graph events, used to inform callbacks.

    Time: An NTP timetag used for communication and synchronization.
    List: A list of Objects (Devices, Signals, or Maps) resulting from a query.

    Operator (IntFlag): Possible operations for composing queries.
    Property (Enum): Symbolic representation of recognized properties.
    Type (IntFlag): Describes the possible data types used by libmapper.

"""

from .mapper import *
