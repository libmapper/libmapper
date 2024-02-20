
import bpy
from mathutils import Euler

try:
    import libmapper as mpr
except:
    print('installing libmapper using pip')
    import sys
    import subprocess
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'libmapper'])
    import libmapper as mpr

dev = None

bl_info = {
    "name": "libmapper",
    "blender": (2, 80, 0),
    "category": "Object",
}

def animation_play_cb(sig, evt, id, val, time):
#    print('animation handler')
    is_playing = bpy.context.screen.is_animation_playing
    if val == 0:
        if is_playing:
            bpy.ops.screen.animation_cancel(restore_frame=False)
    elif not is_playing:
        # TODO: need to check if play direction is reversed!
        bpy.ops.screen.animation_play(reverse = (val < 0), sync = True)

def location_cb(sig, evt, id, val, time):
#    print('location handler:', sig)
    obj_name = sig[mpr.Property.NAME].split('/', 1)
    obj = bpy.data.objects[obj_name[0]]
    if obj is not None:
        obj.location = val
    else:
        print('could not find Blender object matching name', obj_name[0])
        sig.free()

def rot_euler_cb(sig, evt, id, val, time):
#    print('euler rotation handler:', sig)
    obj_name = sig[mpr.Property.NAME].split('/', 1)
    obj = bpy.data.objects[obj_name[0]]
    if obj is not None:
        obj.rotation_euler = Euler(val, 'XYZ')
    else:
        print('could not find Blender object matching name', obj_name[0])
        sig.free()

def rot_quat_cb(sig, evt, id, val, time):
#    print('quat rotation handler:', sig)
    obj_name = sig[mpr.Property.NAME].split('/', 1)
    obj = bpy.data.objects[obj_name[0]]
    if obj is not None:
        obj.rotation_mode = 'QUATERNION'
        obj.rotation_quaternion = val
    else:
        print('could not find Blender object matching name', obj_name[0])
        sig.free()

def scale_cb(sig, evt, id, val, time):
#    print('scale handler:', sig)
    obj_name = sig[mpr.Property.NAME].split('/', 1)
    obj = bpy.data.objects[obj_name[0]]
    if obj is not None:
        obj.scale = val
    else:
        print('could not find Blender object matching name', obj_name[0])
        sig.free()

def libmapper_poll():
    global dev
    if dev:
        dev.poll()
    return 0.1

def locationBoxUpdated(self, context):
    global dev
    if self.location == True:
        print('location mapping enabled')
        dev.add_signal(mpr.Signal.Direction.INCOMING, context.object.name+"/location", 3, mpr.Type.FLOAT,
                       "meters", callback=location_cb, events=mpr.Signal.Event.UPDATE)
    else:
        print('location mapping disabled')
        sig = dev.signals().filter(mpr.Property.NAME, context.object.name+"/location")
        if sig:
            sig = sig.next()
            print('  freeing signal:', sig)
            sig.free()

def rotationBoxUpdated(self, context):
    global dev
    if self.rotation == True:
        print('rotation mapping enabled')
        dev.add_signal(mpr.Signal.Direction.INCOMING, context.object.name+"/rotation/euler", 3, mpr.Type.FLOAT,
                       None, -180, 180, callback=rot_euler_cb, events=mpr.Signal.Event.UPDATE)
#        dev.add_signal(mpr.Signal.Direction.INCOMING, context.object.name+"/rotation/axis", 3, mpr.Type.FLOAT,
#                       None, -1, 1, None, rot_axis_cb)
#        dev.add_signal(mpr.Signal.Direction.INCOMING, context.object.name+"/rotation/angle", 1, mpr.Type.FLOAT,
#                       "degrees", 0, 180, None, rot_angle_cb)
        dev.add_signal(mpr.Signal.Direction.INCOMING, context.object.name+"/rotation/quaternion",
                       4, mpr.Type.FLOAT, None, -1, 1, callback=rot_quat_cb,
                       events=mpr.Signal.Event.UPDATE)
    else:
        print('rotation mapping disabled')
        sig = dev.signals().filter(mpr.Property.NAME, context.object.name+"/rotation/euler")
        if sig:
            sig.next().free()
#        sig = dev.signals().filter(mpr.Property.NAME, context.object.name+"/rotation/axis")
#        if sig:
#            sig.next().free()
#        sig = dev.signals().filter(mpr.Property.NAME, context.object.name+"/rotation/angle")
#        if sig:
#            sig.next().free()
        sig = dev.signals().filter(mpr.Property.NAME, context.object.name+"/rotation/quaternion")
        if sig:
            sig = sig.next()
            print('  freeing signal:', sig)
            sig.free()

def scaleBoxUpdated(self, context):
    global dev
    if self.scale == True:
        print('scale mapping enabled')
        dev.add_signal(mpr.Signal.Direction.INCOMING, context.object.name+"/scale", 3, mpr.Type.FLOAT,
                       callback=scale_cb, events=mpr.Signal.Event.UPDATE)
    else:
        print('scale mapping disabled')
        sig = dev.signals().filter(mpr.Property.NAME, context.object.name+"/scale")
        if sig:
            sig = sig.next()
            print('  freeing signal:', sig)
            sig.free()

class MappingSettings(bpy.types.PropertyGroup):
    location : bpy.props.BoolProperty(
        name = "Enable or Disable",
        description = "Expose object location for signal mapping",
        default = False,
        update = locationBoxUpdated
        )
    rotation : bpy.props.BoolProperty(
        name="Enable or Disable",
        description="Expose object rotation for signal mapping",
        default = False,
        update = rotationBoxUpdated
        )
    scale : bpy.props.BoolProperty(
        name="Enable or Disable",
        description="Expose object scale for signal mapping",
        default = False,
        update = scaleBoxUpdated
        )

class MappingPanel(bpy.types.Panel):
    bl_label = "Signal Mapping"
    bl_idname = "OBJECT_PT_mappable"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = 'object'

    def draw(self, context):
        global dev
        layout = self.layout
        obj = context.object
        my_tool = obj.my_tool

        # display properties
        layout.prop(my_tool, "location", text="location")
        layout.prop(my_tool, "rotation", text="rotation")
        layout.prop(my_tool, "scale", text="scale")

        # add hidden property for tracking changes?

        if not dev:
            print("error: device not found in panel draw function!")
            return

        # TODO: if the object is renamed we should probably also rename the signals

classes = (
    MappingSettings,
    MappingPanel
)

def register():
    global dev
    print('registering libmapper addon')

    for cls in classes:
        bpy.utils.register_class(cls)

    bpy.types.Object.my_tool = bpy.props.PointerProperty(type=MappingSettings)

    dev = mpr.Device('Blender')

    # add libmapper signal for Blender transport
    dev.add_signal(mpr.Signal.Direction.INCOMING, 'animation/play', 1, mpr.Type.INT32, None, -1, 1, None,
                   animation_play_cb, events = mpr.Signal.Event.UPDATE)

    bpy.app.timers.register(libmapper_poll, persistent=True)

def unregister():
    global dev
    print('unregistering libmapper addon')
    bpy.app.timers.unregister(libmapper_poll)

    for cls in classes:
        bpy.utils.unregister_class(cls)
    del bpy.types.Object.my_tool
    dev.free()

# This allows you to run the script directly from Blender's Text editor
# to test the add-on without having to install it.
if __name__ == "__main__":
    register()
