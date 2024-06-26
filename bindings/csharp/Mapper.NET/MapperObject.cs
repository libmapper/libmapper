using System.Runtime.InteropServices;

namespace Mapper;

public abstract class MapperObject
{
    [Flags]
    public enum Status
    {
        Expired = 0x01,
        New = 0x02,
        Modified = 0x04,
        Staged = 0x08,
        Active = 0x10,
        Removed = 0x04,
        Any = 0xFF
    }

    public IntPtr NativePtr { get; internal set; }
    internal bool _owned;

    /// <summary>
    /// Get the unique graph ID for this object.
    /// </summary>
    public long Id => (long)GetProperty(Property.Id);

    public MapperObject()
    {
        NativePtr = IntPtr.Zero;
        _owned = false;
    }

    protected MapperObject(IntPtr nativePtr)
    {
        NativePtr = nativePtr;
        _owned = false;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    protected static extern int mpr_obj_get_type(IntPtr obj);

    public new MapperType GetType()
    {
        return (MapperType)mpr_obj_get_type(NativePtr);
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
            case (int)MapperType.Int32:
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
                            return (MapperType)i;
                        default:
                            return i;
                    }
                }

            {
                var arr = new int[len];
                Marshal.Copy((IntPtr)value, arr, 0, len);
                return arr;
            }
            case (int)MapperType.Float:
                if (1 == len)
                    return *(float*)value;
            {
                var arr = new float[len];
                Marshal.Copy((IntPtr)value, arr, 0, len);
                return arr;
            }
            case (int)MapperType.Double:
                if (1 == len)
                    return *(double*)value;
            {
                var arr = new double[len];
                Marshal.Copy((IntPtr)value, arr, 0, len);
                return arr;
            }
            case (int)MapperType.Boolean:
                if (1 == len)
                    return *(int*)value != 0;
            {
                var arr = new bool[len];
                for (var i = 0; i < len; i++)
                    arr[i] = ((int*)value)[i] != 0;
                return arr;
            }
            case (int)MapperType.Int64:
                if (1 == len)
                    return *(long*)value;
            {
                var arr = new long[len];
                Marshal.Copy((IntPtr)value, arr, 0, len);
                return arr;
            }
            case (int)MapperType.Time:
                if (1 == len)
                    return new Time(*(long*)value);
            {
                var arr = new Time[len];
                for (var i = 0; i < len; i++)
                    arr[0] = new Time(((long*)value)[i]);
                return arr;
            }
            case (int)MapperType.String:
                if (1 == len)
                    return new string(Marshal.PtrToStringAnsi((IntPtr)value));
            {
                var arr = new string[len];
                for (var i = 0; i < len; i++)
                    arr[i] = new string(Marshal.PtrToStringAnsi((IntPtr)((char**)value)[i]));
                return arr;
            }
            case (int)MapperType.Device:
                return new Device((IntPtr)value);
            case (int)MapperType.Map:
                return new Map((IntPtr)value);
            case (int)MapperType.Signal:
                return new Signal((IntPtr)value);
            case (int)MapperType.List:
                if (1 == len)
                    switch (property)
                    {
                        case (int)Property.Device:
                        case (int)Property.Linked:
                        case (int)Property.Scope:
                            return new MapperList<Device>((IntPtr)value, MapperType.Device);
                        case (int)Property.Signal:
                            return new MapperList<Signal>((IntPtr)value, MapperType.Signal);
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
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)MapperType.Int32, &i, _pub);
                break;
            case float f:
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)MapperType.Float, &f, _pub);
                break;
            case double d:
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)MapperType.Double, &d, _pub);
                break;
            case bool b:
            {
                var i = Convert.ToInt32(b);
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)MapperType.Boolean, &i, _pub);
            }
                break;
            case string s:
                mpr_obj_set_prop(NativePtr, _prop, _key, 1, (int)MapperType.String, (void*)Marshal.StringToHGlobalAnsi(s), _pub);
                break;
            case int[] i:
                fixed (int* temp = &i[0])
                {
                    var intPtr = new IntPtr(temp);
                    mpr_obj_set_prop(NativePtr, _prop, _key, i.Length, (int)MapperType.Int32, (void*)intPtr, _pub);
                }

                break;
            case float[] f:
                fixed (float* temp = &f[0])
                {
                    var intPtr = new IntPtr(temp);
                    mpr_obj_set_prop(NativePtr, _prop, _key, f.Length, (int)MapperType.Float, (void*)intPtr, _pub);
                }

                break;
            case double[] d:
                fixed (double* temp = &d[0])
                {
                    var intPtr = new IntPtr(temp);
                    mpr_obj_set_prop(NativePtr, _prop, _key, d.Length, (int)MapperType.Double, (void*)intPtr, _pub);
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
}
