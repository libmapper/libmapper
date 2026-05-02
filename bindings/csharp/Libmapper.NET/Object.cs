using System.Runtime.InteropServices;

namespace Mapper;

public abstract class Object
{
    [Flags]
    public enum Event
    {
        /// <summary>
        ///     Object was newly created since last check
        /// </summary>
        New = 0x0001,

        /// <summary>
        ///     Object was newly created since last check
        /// </summary>
        Modified = 0x0002,

        /// <summary>
        ///     Object was removed
        /// </summary>
        Removed = 0x0004,

        /// <summary>
        ///     Remote object record has expired
        /// </summary>
        Expired = 0x0008,

        /// <summary>
        ///     Object is reserved but not active
        /// </summary>
        Staged = 0x0010,

        /// <summary>
        ///     Object is active
        /// </summary>
        Activated = 0x0020,

        /// <summary>
        ///     Object has a value
        /// </summary>
        HasValue = 0x0040,

        /// <summary>
        ///     Object value has changed since last check
        /// </summary>
        NewValue = 0x0080,

        /// <summary>
        ///     Object was updated locally since last check
        /// </summary>
        LocalUpdate = 0x0100,

        /// <summary>
        ///     Object was updated remotely since last check
        /// </summary>
        RemoteUpdate = 0x0200,

        /// <summary>
        ///     Object was released upstream since last check
        /// </summary>
        UpstreamRelease = 0x0400,

        /// <summary>
        ///     Object was released downstream since last check
        /// </summary>
        DownstreamRelease = 0x0800,

        /// <summary>
        ///     No local instances left
        /// </summary>
        Overflow = 0x1000,

        /// <summary>
        ///     All events
        /// </summary>
        Any = 0x1FFF
    }

    [Flags]
    public enum Status
    {
        /// <summary>
        ///     Object was newly created since last check
        /// </summary>
        New = 0x0001,

        /// <summary>
        ///     Object was newly created since last check
        /// </summary>
        Modified = 0x0002,

        /// <summary>
        ///     Object was removed
        /// </summary>
        Removed = 0x0004,

        /// <summary>
        ///     Remote object record has expired
        /// </summary>
        Expired = 0x0008,

        /// <summary>
        ///     Object is reserved but not active
        /// </summary>
        Staged = 0x0010,

        /// <summary>
        ///     Object is active
        /// </summary>
        Active = 0x0020,

        /// <summary>
        ///     Object has a value
        /// </summary>
        HasValue = 0x0040,

        /// <summary>
        ///     Object value has changed since last check
        /// </summary>
        NewValue = 0x0080,

        /// <summary>
        ///     Object was updated locally since last check
        /// </summary>
        LocalUpdate = 0x0100,

        /// <summary>
        ///     Object was updated remotely since last check
        /// </summary>
        RemoteUpdate = 0x0200,

        /// <summary>
        ///     Object was released upstream since last check
        /// </summary>
        UpstreamRelease = 0x0400,

        /// <summary>
        ///     Object was released downstream since last check
        /// </summary>
        DownstreamRelease = 0x0800,

        /// <summary>
        ///     No local instances left
        /// </summary>
        Overflow = 0x1000,

        /// <summary>
        ///     All events
        /// </summary>
        Any = 0x1FFF
    }

    public IntPtr NativePtr { get; internal set; }
    internal bool _owned;

    /// <summary>
    /// Get the unique graph ID for this object.
    /// Note that the top 32 bits might not be populated for a few seconds after creation.
    /// </summary>
    public long Id => (long)GetProperty(Property.Id);

    public Object()
    {
        NativePtr = IntPtr.Zero;
        _owned = false;
    }

    protected Object(IntPtr nativePtr)
    {
        NativePtr = nativePtr;
        _owned = false;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    protected static extern int mpr_obj_get_type(IntPtr obj);

    public new Mapper.Type GetType()
    {
        return (Mapper.Type)mpr_obj_get_type(NativePtr);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_obj_get_num_props(IntPtr obj);

    public int GetNumProperties()
    {
        return mpr_obj_get_num_props(NativePtr);
    }

    internal unsafe object? BuildValue(int len, int type, void* value, int property)
    {
        if (0 == len)
            return null;
        if (value == null)
            return null;
        switch (type)
        {
            case (int)Mapper.Type.Int32:
                if (1 == len)
                {
                    var i = *(int*)value;
                    switch (property)
                    {
                        case (int)Property.Direction:
                            return (Signal.Direction)i;
                        case (int)Property.ProcessingLocation:
                            return (Map.Location)i;
                        case (int)Property.Protocol:
                            return (Map.Protocol)i;
                        case (int)Property.Status:
                            return (Status)i;
                        case (int)Property.Stealing:
                            return (Signal.Stealing)i;
                        case (int)Property.Type:
                            return (Mapper.Type)i;
                        default:
                            return i;
                    }
                }

            {
                var arr = new int[len];
                Marshal.Copy((IntPtr)value, arr, 0, len);
                return arr;
            }
            case (int)Mapper.Type.Float:
                if (1 == len)
                    return *(float*)value;
            {
                var arr = new float[len];
                Marshal.Copy((IntPtr)value, arr, 0, len);
                return arr;
            }
            case (int)Mapper.Type.Double:
                if (1 == len)
                    return *(double*)value;
            {
                var arr = new double[len];
                Marshal.Copy((IntPtr)value, arr, 0, len);
                return arr;
            }
            case (int)Mapper.Type.Boolean:
                if (1 == len)
                    return *(int*)value != 0;
            {
                var arr = new bool[len];
                for (var i = 0; i < len; i++)
                    arr[i] = ((int*)value)[i] != 0;
                return arr;
            }
            case (int)Mapper.Type.Int64:
                if (1 == len)
                    return *(long*)value;
            {
                var arr = new long[len];
                Marshal.Copy((IntPtr)value, arr, 0, len);
                return arr;
            }
            case (int)Mapper.Type.Time:
                if (1 == len)
                    return new Time(*(long*)value);
            {
                var arr = new Time[len];
                for (var i = 0; i < len; i++)
                    arr[0] = new Time(((long*)value)[i]);
                return arr;
            }
            case (int)Mapper.Type.String:
                if (1 == len)
                    return new string(Marshal.PtrToStringAnsi((IntPtr)value));
            {
                var arr = new string[len];
                for (var i = 0; i < len; i++)
                    arr[i] = new string(Marshal.PtrToStringAnsi((IntPtr)((char**)value)[i]));
                return arr;
            }
            case (int)Mapper.Type.Device:
                return new Device((IntPtr)value);
            case (int)Mapper.Type.Map:
                return new Map((IntPtr)value);
            case (int)Mapper.Type.Signal:
                return new Signal((IntPtr)value);
            case (int)Mapper.Type.List:
                if (1 == len)
                    switch (property)
                    {
                        case (int)Property.Device:
                        case (int)Property.Linked:
                        case (int)Property.AllowOrigin:
                        case (int)Property.BlockOrigin:
                            return new Mapper.List<Device>((IntPtr)value, Mapper.Type.Device);
                        case (int)Property.Signal:
                            return new Mapper.List<Signal>((IntPtr)value, Mapper.Type.Signal);
                        default:
                            Console.WriteLine("error: missing List type.");
                            return null;
                    }

                Console.WriteLine("error: arrays of List are not currently supported.");
                return null;
            default:
                Console.WriteLine("error: unhandled data type in Object.BuildValue().");
                return null;
        }
    }

    // TODO: implement IDictionary?

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    internal static extern unsafe int mpr_obj_get_prop_by_idx(IntPtr obj, int idx, ref char* key,
        ref int len, ref int type,
        ref void* value, ref int publish);

    public unsafe (string, object?) GetProperty(int index)
    {
        char* key = null;
        var len = 0;
        var type = 0;
        void* val = null;
        var pub = 0;
        var idx = mpr_obj_get_prop_by_idx(NativePtr, index, ref key, ref len,
            ref type, ref val, ref pub);
        if (0 == idx || 0 == len)
            return (new string("unknown"), null);
        return (new string(Marshal.PtrToStringAnsi((IntPtr)key)), BuildValue(len, type, val, idx));
    }

    public unsafe object? GetProperty(Property property)
    {
        char* key = null;
        var len = 0;
        var type = 0;
        void* val = null;
        var pub = 0;
        var idx = mpr_obj_get_prop_by_idx(NativePtr, (int)property, ref key, ref len,
            ref type, ref val, ref pub);
        if (0 == idx || 0 == len)
            return null;
        return BuildValue(len, type, val, idx);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    internal static extern unsafe int mpr_obj_get_prop_by_key(IntPtr obj,
        [MarshalAs(UnmanagedType.LPStr)] string key,
        int* len, int* type,
        void** value, int* publish);

    public unsafe object? GetProperty(string key)
    {
        var len = 0;
        var type = 0;
        void* val = null;
        var pub = 0;
        int idx;
        idx = mpr_obj_get_prop_by_key(NativePtr, key, &len, &type, &val, &pub);
        if (0 == idx || 0 == len)
            return null;
        return BuildValue(len, type, val, idx);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    internal static extern int mpr_obj_get_prop_as_int32(IntPtr obj, int prop,
        [MarshalAs(UnmanagedType.LPStr)] string? key);

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    internal static extern unsafe
        int mpr_obj_set_prop(IntPtr obj, int prop, [MarshalAs(UnmanagedType.LPStr)] string? key,
            int len, int type, void* value, int publish);

    public unsafe object SetProperty<TProperty, TValue>(TProperty property, TValue value, bool publish)
    {
        int _prop = 0, _pub = Convert.ToInt32(publish);
        string? _key = null;

        switch (property)
        {
            case Property p:
                _prop = (int)p;
                break;
            case string s:
                _key = s;
                break;
            default:
                Console.WriteLine("error: unknown property specifier in method SetProperty().");
                return this;
        }

        switch (value)
        {
            case int i:
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)Mapper.Type.Int32, &i, _pub);
                break;
            case float f:
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)Mapper.Type.Float, &f, _pub);
                break;
            case double d:
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)Mapper.Type.Double, &d, _pub);
                break;
            case bool b:
            {
                var i = Convert.ToInt32(b);
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)Mapper.Type.Boolean, &i, _pub);
            }
                break;
            case string s:
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)Mapper.Type.String, (void*)Marshal.StringToHGlobalAnsi(s), _pub);
                break;
            case int[] i:
                fixed (int* temp = &i[0])
                {
                    var intPtr = new IntPtr(temp);
                    mpr_obj_set_prop(NativePtr, _prop, _key, i.Length, (int)Mapper.Type.Int32, (void*)intPtr, _pub);
                }

                break;
            case float[] f:
                fixed (float* temp = &f[0])
                {
                    var intPtr = new IntPtr(temp);
                    mpr_obj_set_prop(NativePtr, _prop, _key, f.Length, (int)Mapper.Type.Float, (void*)intPtr, _pub);
                }

                break;
            case double[] d:
                fixed (double* temp = &d[0])
                {
                    var intPtr = new IntPtr(temp);
                    mpr_obj_set_prop(NativePtr, _prop, _key, d.Length, (int)Mapper.Type.Double, (void*)intPtr, _pub);
                }

                break;
            default:
                Console.WriteLine("error: unhandled type in SetProperty().");
                break;
        }

        return this;
    }

    public object SetProperty<TProperty, TValue>(TProperty property, TValue value)
    {
        return SetProperty(property, value, true);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_obj_remove_prop(IntPtr obj, int prop,
        [MarshalAs(UnmanagedType.LPStr)] string? key);

    public bool RemoveProperty<TProperty>(TProperty property)
    {
        var _prop = 0;
        string? _key = null;

        switch (property)
        {
            case Property p:
                _prop = (int)p;
                break;
            case string s:
                _key = s;
                break;
            default:
                Console.WriteLine("error: unknown property specifier in method SetProperty().");
                return false;
        }

        return 0 != mpr_obj_remove_prop(NativePtr, _prop, _key);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_obj_push(IntPtr obj);

    public object Push()
    {
        mpr_obj_push(NativePtr);
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_obj_get_graph(IntPtr obj);

    public Graph GetGraph()
    {
        return new Graph(mpr_obj_get_graph(NativePtr));
    }

    internal virtual object SetInstId(int status, int index)
    {
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_obj_get_status(IntPtr obj, int clear_volatile);

    /// <summary>
    /// Gets and optionally clears status flags attached to this object.
    /// The returned value can be used to see if the object has been updated remotely.
    /// <param name="clearVolatile">Set to true to clear volatile Status flags</param>
    /// </summary>
    /// <returns>Status flags</returns>
    public Status GetStatus(bool clearVolatile = false)
    {
        return (Status)mpr_obj_get_status(NativePtr, Convert.ToInt32(clearVolatile));
    }
}
