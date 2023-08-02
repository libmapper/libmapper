
import libmapper as mpr
import bpy
from mathutils import Euler

dev = None

bl_info = {
    "name": "libmapper",
    "blender": (2, 80, 0),
    "category": "Object",
}

def animation_play_cb(sig, evt, inst, val, time):
    print('animation handler')
    is_playing = bpy.context.screen.is_animation_playing
    if val == 0:
        if is_playing:
            bpy.ops.screen.animation_cancel(restore_frame=False)
    elif not is_playing:
        # TODO: need to check if play direction is reversed!
        bpy.ops.screen.animation_play(reverse = (val < 0), sync = True)

def location_cb(sig, evt, inst, val, time):
    print('location handler')
    obj_name = sig[mpr.Property.NAME].split('/', 1)
    obj = bpy.data.objects[obj_name[0]]
    if obj is not None:
        obj.location = val
    else:
        print('could not find Blender object matching name', obj_name[0])
        sig.free()

def rot_euler_cb(sig, evt, inst, val, time):
    print('euler rotation handler')
    obj_name = sig[mpr.Property.NAME].split('/', 1)
    obj = bpy.data.objects[obj_name[0]]
    if obj is not None:
        obj.rotation_euler = Euler(val, 'XYZ')
    else:
        print('could not find Blender object matching name', obj_name[0])
        sig.free()

def rot_quat_cb(sig, evt, inst, val, time):
    print('quat rotation handler')
    obj_name = sig[mpr.Property.NAME].split('/', 1)
    obj = bpy.data.objects[obj_name[0]]
    if obj is not None:
        obj.rotation_mode = 'QUATERNION'
        obj.rotation_quaternion = val
    else:
        print('could not find Blender object matching name', obj_name[0])
        sig.free()

def scale_cb(sig, evt, inst, val, time):
    print('scale handler')
    obj_name = sig[mpr.Property.NAME].split('/', 1)
    obj = bpy.data.objects[obj_name[0]]
    if obj is not None:
        obj.scale = val

def libmapper_poll():
    global dev
    if dev:
        dev.poll()
    return 0.1

class MappingSettings(bpy.types.PropertyGroup):
    location : bpy.props.BoolProperty(
        name="Enable or Disable",
        description="Expose object location for signal mapping",
        default = False
        )
    rotation : bpy.props.BoolProperty(
        name="Enable or Disable",
        description="Expose object rotation for signal mapping",
        default = False
        )
    scale : bpy.props.BoolProperty(
        name="Enable or Disable",
        description="Expose object scale for signal mapping",
        default = False
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

        # check property values
        # TODO: if the object is renamed we should probably also rename the signals
        if (my_tool.location == True):
            print('location mapping enabled')
            dev.add_signal(mpr.Direction.INCOMING, obj.name+"/location", 3, mpr.Type.FLOAT,
                           "meters", callback=location_cb, events=mpr.Signal.Event.UPDATE)
        else:
            print('location mapping disabled')
            sig = dev.signals().filter(mpr.Property.NAME, obj.name+"/location")
            if sig:
                sig.next().free()
        if (my_tool.rotation == True):
            print('rotation mapping enabled')
            sig_euler = dev.add_signal(mpr.Direction.INCOMING, obj.name+"/rotation/euler", 3,
                                       mpr.Type.FLOAT, None, -180, 180, callback=rot_euler_cb,
                                       events=mpr.Signal.Event.UPDATE)
#                dev.add_signal(mpr.Direction.INCOMING, obj.name+"/rotation/axis", 3, mpr.Type.FLOAT,
#                               None, -1, 1, None, rot_axis_cb)
#                dev.add_signal(mpr.Direction.INCOMING, obj.name+"/rotation/angle", 1, mpr.Type.FLOAT,
#                               "degrees", 0, 180, None, rot_angle_cb)
            sig_quat = dev.add_signal(mpr.Direction.INCOMING, obj.name+"/rotation/quaternion",
                                      4, mpr.Type.FLOAT, None, -1, 1, callback=rot_quat_cb,
                                      events=mpr.Signal.Event.UPDATE)
        else:
            print('rotation mapping disabled')
            sig = dev.signals().filter(mpr.Property.NAME, obj.name+"/rotation/euler")
            if sig:
                sig.next().free()
#            sig = dev.signals().filter(mpr.Property.NAME, obj.name+"/rotation/axis")
#            if sig:
#                sig.next().free()
#            sig = dev.signals().filter(mpr.Property.NAME, obj.name+"/rotation/angle")
#            if sig:
#                sig.next().free()
            sig = dev.signals().filter(mpr.Property.NAME, obj.name+"/rotation/quaternion")
            if sig:
                sig.next().free()
        if (my_tool.scale == True):
            print('scale mapping enabled')
            dev.add_signal(mpr.Direction.INCOMING, obj.name+"/scale", 3, mpr.Type.FLOAT,
                           callback=scale_cb, events=mpr.Signal.Event.UPDATE)
        else:
            print('scale mapping disabled')
            sig = dev.signals().filter(mpr.Property.NAME, obj.name+"/scale")
            if sig:
                sig.next().free()

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
    dev.add_signal(mpr.Direction.INCOMING, 'animation/play', 1, mpr.Type.INT32, None, -1, 1, None,
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
