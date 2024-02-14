"""
libmapper bindings for Python

This module provides Python bindings for using the C library _libmapper_. Libmapper implements a
system for representing input and output signals on a network and allowing arbitrary "mappings" to
be dynamically created between them.

A "mapping", or "connection" associated with relational properties, consists of an Open Sound
Control (OSC) stream being established between one or more source signals and a destination signal.
Libmapper automatically handles address and datatype translation, and enables the use of arbitrary
mathematical expression for conditioning and transforming the transmitted values as desired. This
can be used for example to connect a set of sensors to a synthesizer's input parameters.

For more information please visit [libmapper.org](libmapper.org)
"""

from .mapper import *
