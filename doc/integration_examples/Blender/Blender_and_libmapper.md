# Blender + libmapper

[Blender](https://www.blender.org/) is a free and open-source software tool for 3D computer graphics. Blender ships with built-in Python scripting capabilities which we will use to create a Blender-libmapper bridge in this tutorial. For more information about Python scripting in Blender refer to the [Blender API Documentation](https://docs.blender.org/api/current/index.html#).

## Getting started

In order to help with debugging while you get started with Blender and libmapper, we recommend starting Blender from the command line. This causes Python `print()` to print to the system console (where you started the application) rather than an inaccessible Blender console.

On MacOS, simply open the **Terminal** application and browse to the installed Blender application using tab completion. On my machine it is:

~~~bash
% /Applications/Blender.app/Contents/MacOS/Blender
~~~

Although you may have already installed libmapper bindings for Python on your machine, Blender uses its own copy of Python so we will install it again! This is easily accomplished using `pip`. Start by selecting the 'Scripting' tab.

~~~ python
try:
    import libmapper as mpr
except:
    print('installing libmapper using pip')
    import sys
    import subprocess
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'libmapper'])
    import libmapper as mpr
~~~

Check the console for error messages, and feel free to [create an issue](https://github.com/libmapper/libmapper/issues) on our GitHub repository if you encounter any problems. Once libmapper has been installed in your copy of Blender you will not have to include the code above any more, and you can simply write:

~~~ python
import libmapper as mpr
~~~

## A simple example

Download: [simple_example.blend](./simple_example.blend)

The default scene when Blender is loaded includes a single object named "Cube" – for this example we will simply animate its position (`location` in Blender's terminology) using libmapper. We will have to make use of the `Threading` module to run our libmapper device in a separate thread so that Blender will remain responsive while we are polling. The example below creates a single libmapper device named "Blender" with one `float[3]` signal named "cube/location". The program will run for 20 seconds and then free the device and exit.

~~~ python
import libmapper as mpr
import threading
import bpy

class MapperThread (threading.Thread):
    def __init__(self, threadID, name):
        threading.Thread.__init__(self)
        self.threadID = threadID
        self.name = name
        
        self.dev = mpr.Device('Blender')
        self.cube = self.dev.add_signal(mpr.Signal.Direction.INCOMING,
                                        'cube/location', 3, mpr.Type.FLOAT)
        self.cube.set_callback(self.callback)
    
    def callback(self, sig, evt, inst, val, time):
        bpy.data.objects["Cube"].location = val
    
    def run (self):
        is_running = True
        loop = 0
        while is_running is True:
            self.dev.poll(1000)
            loop += 1
            if loop >= 20:
                is_running = False
        print('freeing device')
        self.dev.free()

# create thread
thread = MapperThread(1, "mapper_thread")
thread.start()
~~~

