# Bela Installation

The [Bela](https://Bela.io) board is a "*maker platform for creating beautiful interaction*". This tutorial document will walk through the process of installing and using [libmapper](http://libmapper.github.io/) on the Bela.

Topics covered include:

* Installing libmapper
* Using libmapper with Bela (C++)
* Installing MapperUGen (Super Collider)
* *MORE TBD*

## libmapper

Installing libmapper on Bela follows the same steps as installing libmapper on any other linux based computer. A walk through for the Bela specific installation is as follows.

### Getting the Source Code

You can clone the source code from the [github](https://github.com/libmapper/libmapper) repository onto the computer which you have connected to the Bela board.

Once the repo has been cloned, you can copy it to the root folder of the Bela board. Using software such as FileZila makes this process easy. To connect to the Bela, use the following ssh parameters:

```txt
username: root
host: bela.local
port: 22
password: <none>
```

*Note: You can clone the libmapper repo directly onto the Bela if you have connected the board to the internet via a WiFi adapter or ethernet.*

### Run AutoGen

Once you have copied the libmapper source code to the Bela, navigate into the libmapper folder using `cd libmapper` and then run the autogen script with `./autogen.sh`. Once this is finished, the make files will be successfully generated and you can continue to the next step.

#### Locales Issues

It is possible that the autogen script will fail when using perl, as the locales were not set in the Bela by default. To fix this, set the locales to C using `export LC_ALL=C`. One this is done, re-run the autogen script using the same `./autogen.sh` command.

### Install liblo

libmapper depends on the liblo library to create and manage OSC connections. You can install liblo on the Bela by once again copying the [source code](http://liblo.sourceforge.net/) into the root directory and running `./autogen.sh && make && make install`.

### Make & Make Install

To build libmapper, run the command `make`.

To install libmapper, run the command `make install`.

### Installation Complete

Once these steps are finished, you should have access to the **c** & **c++** libraries for libmapper.

## Using libmapper on Bela

To use libmapper with Bela (with C++), you can include the library directly in the web-IDE.

```c
/*
 ____  _____ _        _    
| __ )| ____| |      / \   
|  _ \|  _| | |     / _ \  
| |_) | |___| |___ / ___ \ 
|____/|_____|_____/_/   \_\

The platform for ultra-low latency audio and sensor processing
*/

#include <Bela.h>
#include <cmath>

/* Ensure that you have included the library, update make params as necessary*/
#include <mapper/mapper_cpp.h>

...
```

### Make Parameters

In order to link the libmapper library with the c++ project in the **bela IDE**, you must include the following "Make Parameters":

```txt
LDFLAGS=-L/usr/local/lib; CPPFLAGS=-I/root/liblo/include -I/root/libmapper/include; LDLIBS=-lmapper -llo;
```

This will ensure that CMake finds both libmapper and its dependency (liblo).

### Programming Recommendations

As with any programming tasks, there are several ways you can choose to interact with libmapper on bela using c++. The following section provides some recommendations based on our own usage of the system.

#### Global Variables

In order to have access to a device / signals throughout the entirety of a c++ program, we recommend that you define libmapper devices and signals as global variables, and instantiate them in the `Setup(...)` function. For example:

```cpp
/* Define Globals*/
mapper::Device* mapper_dev;
mapper::Signal output_sig;

...

bool setup(BelaContext *context, void *userData)
{
    ...

    mapper_dev = new mapper::Device("bela");
    output_sig = mapper_dev->add_signal(mapper::Direction::OUTGOING, "trill-bar", 1, mapper::Type::INT32, "pos", &min, &max, &num_inst);

    ...
}

...
```

#### Auxiliary Tasks

The main feature of the bela is it's **ultra low latency** in the `Render(...)` function. This is achieved through usage of the Xenomai kernel for realtime processing. This means that the Kernel will prioritize only the tasks necessary for that processing in order to achieve the desired performance.

If you decide to simply call `dev->poll()` in the `Render(...)` function, you will be forcing the Xenomai kernel to change modes at a rapid pace. (These mode changes are necessary due to libmapper's poll function interacting with the network and other non-audio rendering processes on the OS). While this method will "work", you will be faced with two error messages in the Bela IDE if you attempt to do it this way.

These errors are:

* ***486411 mode switches detected on the audio thread.***
* ***Underrun detected: 1 blocks dropped.***

To fix these, you can set up an **Auxiliary Task**, and ensure that it loops continuously throughout the execution of the c++ program on the Bela.

```cpp
/* Define a function to be called as an Aux-Task. */
void poll(void*)
{
 while(!Bela_stopRequested())
 {
   dev->poll();
   consumer->poll();
 }
}

...

bool setup(BelaContext *context, void *userData)
{
    ...
    
    // Set and schedule aux task for handling libmapper device & signal updates.
    Bela_runAuxiliaryTask(poll);
    rt_printf("libmapper device is ready!\n");

}
```

This will ensure that the libmapper devices are polled continuously, but will not interfere with the processing being done in the `Render(...)` function.

## MapperUGen

MapperUGen is a SuperCollider extension that brings libmapper functionality to the SuperCollider project.

GitHub Link: [https://github.com/mathiasbredholt/MapperUGen](https://github.com/mathiasbredholt/MapperUGen)

### Boost 1.71.0

*Note: Installing the Boost library is done using a modified version of the instructions found [here](https://opencoursehub.cs.sfu.ca/bfraser/grav-cms/cmpt433/links/files/2015-student-howtos/CompileBoostForBBB.pdf).*

Download the boost 1.71.0 `tar.gz` file from the [boost downloads page](https://www.boost.org/users/history/version_1_71_0.html).

Copy the `boost_1_71_0.tar.gz` file to `/root` of the Bela board. This can be done using FileZila or any other appropriate method.

Next, extract the files using the command:

```bash
tar -xvf boost_1_71_0.tar.gz -C /usr/local
```

This command will take a few moments to full unpack the compressed files.

You can (& should) confirm that the unzipping was successful by issuing the command `ls /usr/local/boost_1_71_0` to ensure that the files are in the right location.

### Update CMakelists

In order for the MapperUGen project to build using CMAKE with the newly extracted files, the `Cmakelist.txt` file needs to be updated.

Add the following lines **before** the `find_package(boost ...)` line.

```makefile
# ADD THESE LINES

SET (BOOST_ROOT "/usr/local/boost_1_71_0")
SET (BOOST_INCLUDEDIR "/usr/local/boost_1_71_0/boost")
SET (BOOST_LIBRARYDIR "/usr/local/boost_1_71_0/libs")

INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )
```

### Install MapperUGen

Once you have copied the source code and updated the CMakeLists. You can install MapperUGen as per the instructions found [here](https://github.com/mathiasbredholt/MapperUGen/blob/master/README.md).

**To install:**

```bash
mkdir build && cd build
cmake -DSUPERNOVA=ON ..
cmake --build . --target install
```

Once this has been completed, you are ready to use MapperUGen with SuperCollider on the Bela.

## Using MapperUGen

The basic example from the MapperUGen help file combined with the simplest of bela SuperCollider examples is as follows:

```SuperCollider
s = Server.default;

s.options.numAnalogInChannels = 8;
s.options.numAnalogOutChannels = 8;
s.options.numDigitalChannels = 16;

s.options.blockSize = 16;
s.options.numInputBusChannels = 2;
s.options.numOutputBusChannels = 2;

s.options.postln;
Platform.userExtensionDir.postln;

s.waitForBoot({

    Mapper.enable;

    {
        RLPF.ar(Saw.ar(50), MapIn.kr(\ffreq, 20, 20000), 0.2).dup * 0.2;
    }.play
});
```

## Trill Sensors with libmapper

The [trill sensor](https://bela.io/products/trill/) is a subsequent product from the Bela group that allows makers to use capacitive touch sensing in their projects.


### Example Code

This file should be copied into your `render.cpp` file in the Bela web IDE. It uses the Trill Bar to generate values that updates a libmapper signal (with 5 instances, one for each multi-touch supported by the trill bar) according to those values.

```cpp
/*
 ____  _____ _        _    
| __ )| ____| |      / \   
|  _ \|  _| | |     / _ \  
| |_) | |___| |___ / ___ \ 
|____/|_____|_____/_/   \_\

*/

#include <Bela.h>
#include <cmath>

#include <libraries/Trill/Trill.h>
#define NUM_TOUCH 5 // Number of touches on Trill sensor

// Include libmapper in this project
#include <mapper/mapper_cpp.h>

float gInterval = 0.5;
float gSecondsElapsed = 0;
int gCount = 0;

// libmapper object declarations
mapper::Device* dev;
mapper::Signal trill_touches;

// Trill object declaration
Trill touchSensor;

// Location of touches on Trill Bar
float gTouchLocation[NUM_TOUCH] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
// Size of touches on Trill bar
float gTouchSize[NUM_TOUCH] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
// Number of active touches
int gNumActiveTouches = 0;

// Sleep time for auxiliary task
unsigned int gTaskSleepTime = 12000; // microseconds
// Time period (in seconds) after which data will be sent to the GUI
float gTimePeriod = 0.015;

/*
* Function to be run on an auxiliary task that reads data from the Trill sensor.
* Here, a loop is defined so that the task runs recurrently for as long as the
* audio thread is running.
*/
void loop(void*)
{
 while(!Bela_stopRequested())
 {
   // Read locations from Trill sensor
   touchSensor.readI2C();
   gNumActiveTouches = touchSensor.getNumTouches();
   for(unsigned int i = 0; i < gNumActiveTouches; i++) {
    gTouchLocation[i] = touchSensor.touchLocation(i);
    gTouchSize[i] = touchSensor.touchSize(i);
    
    /*! Update libmapper signal instances !*/
   trill_touches.instance(i).set_value(gTouchSize[i]);
    
   }
   // For all inactive touches, set location and size to 0
   for(unsigned int i = gNumActiveTouches; i <  NUM_TOUCH; i++) {
    gTouchLocation[i] = 0.0;
    gTouchSize[i] = 0.0;
    
    /*! Update libmapper signal instances !*/
   trill_touches.instance(i).set_value(gTouchSize[0]);
   }
   usleep(gTaskSleepTime);
 }
}

/*
* Function to be run on an auxiliary task that polls a libmapper device for as long as the audio thread is running.
*/
void poll(void*)
{
 while(!Bela_stopRequested())
 {
   dev->poll();
   usleep(gTaskSleepTime);
 }
}

bool setup(BelaContext *context, void *userData)
{
    rt_printf("This Bela project has started running!\n");
    
    float min = 0;
    float max = 1;
    int num_inst = NUM_TOUCH;
    
    // Instantiate libmapper device
    dev = new mapper::Device("bela");

 // Consider making this a 2D vector signal, one for position and one for size.
 trill_touches = dev->add_signal(mapper::Direction::OUTGOING, "trill-bar", 1, mapper::Type::INT32, "pos", &min, &max, &num_inst);
 
 while(!dev->ready()){
  dev->poll(500);
 }
 
 // Set and schedule aux task for handling libmapper device & signal updates.
 Bela_runAuxiliaryTask(poll);
 rt_printf("libmapper device is ready!\n");
 
 
 // Setup a Trill Bar sensor on i2c bus 1, using the default mode and address
 if(touchSensor.setup(1, Trill::BAR) != 0) {
  fprintf(stderr, "Unable to initialise Trill Bar\n");
  return false;
 }
 touchSensor.printDetails();
 
 // Set and schedule auxiliary task for reading sensor data from the I2C bus
 Bela_runAuxiliaryTask(loop);
 rt_printf("Trill Bar is ready!\n");

 return true;
}

void render(BelaContext *context, void *userData)
{
 /* Do nothing, for now. */
}

void cleanup(BelaContext *context, void *userData)
{
 /* Do nothing, for now. */
}
```

## Todo List

There are several more libmapper related tasks for Bela that require development. A non-exhaustive list is as follows:

* Confirm installation compatibility with Bela Mini.
* Add tutorials / examples for other trill sensors.
* Add tutorials / examples with render loop support.
* more TBD!
  