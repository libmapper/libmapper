Introduction
============

These tutorials introduce new users to _libmapper_, providing steps to construct software programs that are compatible with the _libmapper_ network.  A program typically implements a libmapper _device_, and declares _signals_, which can be inputs or outputs. After this, run-time _connections_ can be easily created and configured between running devices and signals. libmapper takes care of announcing and discovering available resources on the network, creating and maintaining connections between entities, and various other functions.

Often, a device will possess only outputs (e.g. a program that gets information from a human input device like a joystick), or only inputs (e.g. a software-controlled synthesizer).  For convenience, in these tutorials we will call devices with outputs "controllers", or "senders", and devices with inputs "synthesizers", or "receivers".  This betrays the use case that was in mind when the _libmapper_ system was conceived, but of course receivers could just as well be programs that control motors, lights, or anything else that might need control information.  Similarly, senders could easily be programs that generate trajectory data based on algorithmic composition, or whatever you can imagine.

It is also possible to create devices which have inputs and outputs, and these can be mapped "in between" senders and receivers in order to perform some intermediate processing for example.  However, this is a more advanced topic that won't be covered in this tutorial.

Essentially, each device only needs to do a few things:

  - **start a libmapper "device"**
  - **add some signals**
  - **update any outputs periodically**
  - **poll the device to process incoming messages**

Detailed tutorials are provided for using libmapper in the following programming languages and environments:

  - C
  - Python
  - Java
  - Max/MSP
  - Puredata