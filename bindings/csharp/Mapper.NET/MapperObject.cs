using System.Runtime.InteropServices;

namespace Mapper.NET;

public abstract class MapperObject
    {
        public enum Status
        {
            Expired      = 0x01,
            Staged       = 0x02,
            Waiting      = 0x0E,
            Ready        = 0x36,
            Active       = 0x7E,
            Reserved     = 0x80,
            Any          = 0xFF
        }

        public MapperObject()
        {
            _obj = IntPtr.Zero;
            _owned  = false;
        }

        protected MapperObject(IntPtr obj)
        {
            _obj = obj;
            _owned  = false;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        protected static extern int mpr_obj_get_type(IntPtr obj);
        public new Type GetType()
            { return (Type) mpr_obj_get_type(_obj); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_obj_get_num_props(IntPtr obj);
        public int GetNumProperties()
            { return mpr_obj_get_num_props(_obj); }

        internal unsafe dynamic BuildValue(int len, int type, void *value, int property)
        {
            if (0 == len)
                return null;
            if (value == null)
                return null;
            switch (type)
            {
                case (int)Type.Int32:
                    if (1 == len)
                    {
                        int i = *(int*)value;
                        switch (property)
                        {
                            case (int)Property.Direction:
                                return (Signal.Direction)i;
                            case (int)Property.ProcessingLocation:
                                return (Map.Location)i;
                            case (int)Property.Protocol:
                                return (Map.Protocol)i;
                            case (int)Property.Status:
                                return (MapperObject.Status)i;
                            case (int)Property.Stealing:
                                return (Signal.Stealing)i;
                            case (int)Property.Type:
                                return (Type)i;
                            default:
                                return i;
                        }
                    }
                    else
                    {
                        int[] arr = new int[len];
                        Marshal.Copy((IntPtr)value, arr, 0, len);
                        return arr;
                    }
                case (int)Type.Float:
                    if (1 == len)
                        return *(float*)value;
                    else
                    {
                        float[] arr = new float[len];
                        Marshal.Copy((IntPtr)value, arr, 0, len);
                        return arr;
                    }
                case (int)Type.Double:
                    if (1 == len)
                        return *(double*)value;
                    else
                    {
                        double[] arr = new double[len];
                        Marshal.Copy((IntPtr)value, arr, 0, len);
                        return arr;
                    }
                case (int)Type.Boolean:
                    if (1 == len)
                        return *(int*)value != 0;
                    else
                    {
                        bool[] arr = new bool[len];
                        for (int i = 0; i < len; i++)
                            arr[i] = ((int*)value)[i] != 0;
                        return arr;
                    }
                case (int)Type.Int64:
                    if (1 == len)
                        return *(long*)value;
                    else
                    {
                        long[] arr = new long[len];
                        Marshal.Copy((IntPtr)value, arr, 0, len);
                        return arr;
                    }
                case (int)Type.Time:
                    if (1 == len)
                        return new Time(*(long*)value);
                    else
                    {
                        Time[] arr = new Time[len];
                        for (int i = 0; i < len; i++)
                            arr[0] = new Time(((long*)value)[i]);
                        return arr;
                    }
                case (int)Type.String:
                    if (1 == len)
                        return new String(Marshal.PtrToStringAnsi((IntPtr)value));
                    else
                    {
                        String[] arr = new String[len];
                        for (int i = 0; i < len; i++)
                            arr[i] = new String(Marshal.PtrToStringAnsi((IntPtr)((char**)value)[i]));
                        return arr;
                    }
                case (int)Type.Device:
                    return new Device((IntPtr)value);
                case (int)Type.Map:
                    return new Map((IntPtr)value);
                case (int)Type.Signal:
                    return new Signal((IntPtr)value);
                case (int)Type.List:
                    if (1 == len)
                    {
                        switch (property)
                        {
                            case (int)Property.Device:
                            case (int)Property.Linked:
                            case (int)Property.Scope:
                                return new MapperList<Device>((IntPtr)value, Type.Device);
                            case (int)Property.Signal:
                                return new MapperList<Signal>((IntPtr)value, Type.Signal);
                            default:
                                Console.WriteLine("error: missing List type.");
                                return null;
                        }
                    }
                    else
                    {
                        Console.WriteLine("error: arrays of List are not currently supported.");
                        return null;
                    }
                default:
                    Console.WriteLine("error: unhandled data type in Object.BuildValue().");
                    return null;
            }
        }

        // TODO: implement IDictionary?

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        internal static extern unsafe int mpr_obj_get_prop_by_idx(IntPtr obj, int idx, ref char *key,
                                                                  ref int len, ref int type,
                                                                  ref void *value, ref int publish);

        public unsafe (String, dynamic) GetProperty(int index)
        {
            char *key = null;
            int len = 0;
            int type = 0;
            void *val = null;
            int pub = 0;
            int idx = mpr_obj_get_prop_by_idx(this._obj, index, ref key, ref len,
                                              ref type, ref val, ref pub);
            if (0 == idx || 0 == len)
                return (new String("unknown"), null);
            return (new string(Marshal.PtrToStringAnsi((IntPtr)key)), BuildValue(len, type, val, idx));
        }

        public unsafe dynamic GetProperty(Property property)
        {
            char *key = null;
            int len = 0;
            int type = 0;
            void *val = null;
            int pub = 0;
            int idx = mpr_obj_get_prop_by_idx(this._obj, (int)property, ref key, ref len,
                                              ref type, ref val, ref pub);
            if (0 == idx || 0 == len)
                return null;
            return BuildValue(len, type, val, idx);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        internal static extern unsafe int mpr_obj_get_prop_by_key(IntPtr obj,
                                                                  [MarshalAs(UnmanagedType.LPStr)] string key,
                                                                  ref int len, ref int type,
                                                                  ref void *value, ref int publish);
        public unsafe dynamic GetProperty(string key)
        {
            int len = 0;
            int type = 0;
            void *val = null;
            int pub = 0;
            int idx;
            idx = mpr_obj_get_prop_by_key(this._obj, key, ref len, ref type, ref val, ref pub);
            if (0 == idx || 0 == len)
                return null;
            return BuildValue(len, type, val, idx);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        internal static extern unsafe
        int mpr_obj_get_prop_as_int32(IntPtr obj, int prop, [MarshalAs(UnmanagedType.LPStr)] string key);

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        internal static extern unsafe
        int mpr_obj_set_prop(IntPtr obj, int prop, [MarshalAs(UnmanagedType.LPStr)] string key,
                             int len, int type, void* value, int publish);
        public unsafe Object SetProperty<P, T>(P property, T value, bool publish)
        {
            int _prop = 0, _pub = Convert.ToInt32(publish);
            string _key = null;

            switch (property)
            {
                case Property p:    _prop = (int)p; break;
                case string s:      _key = s;       break;
                default:
                    Console.WriteLine("error: unknown property specifier in method SetProperty().");
                    return this;
            }

            switch (value)
            {
                case int i:
                    mpr_obj_set_prop(_obj, _prop, _key, 1, (int)Type.Int32, (void*)&i, _pub);
                    break;
                case float f:
                    mpr_obj_set_prop(_obj, _prop, _key, 1, (int)Type.Float, (void*)&f, _pub);
                    break;
                case double d:
                    mpr_obj_set_prop(_obj, _prop, _key, 1, (int)Type.Double, (void*)&d, _pub);
                    break;
                case bool b:
                    {
                        int i = Convert.ToInt32(b);
                        mpr_obj_set_prop(_obj, _prop, _key, 1, (int)Type.Boolean, (void*)&i, _pub);
                    }
                    break;
                case string s:
                    mpr_obj_set_prop(_obj, _prop, _key, 1, (int)Type.String, (void*)Marshal.StringToHGlobalAnsi(s), _pub);
                    break;
                case int[] i:
                    fixed(int *temp = &i[0])
                    {
                        IntPtr intPtr = new IntPtr((void*)temp);
                        mpr_obj_set_prop(_obj, _prop, _key, i.Length, (int)Type.Int32, (void*)intPtr, _pub);
                    }
                    break;
                case float[] f:
                    fixed(float *temp = &f[0])
                    {
                        IntPtr intPtr = new IntPtr((void*)temp);
                        mpr_obj_set_prop(_obj, _prop, _key, f.Length, (int)Type.Float, (void*)intPtr, _pub);
                    }
                    break;
                case double[] d:
                    fixed(double *temp = &d[0])
                    {
                        IntPtr intPtr = new IntPtr((void*)temp);
                        mpr_obj_set_prop(_obj, _prop, _key, d.Length, (int)Type.Double, (void*)intPtr, _pub);
                    }
                    break;
                default:
                    Console.WriteLine("error: unhandled type in SetProperty().");
                    break;
            }
            return this;
        }
        public Object SetProperty<P, T>(P property, T value)
        {
            return SetProperty(property, value, true);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_obj_remove_prop(IntPtr obj, int prop,
                                                      [MarshalAs(UnmanagedType.LPStr)] string key);
        public bool RemoveProperty<P>(P property)
        {
            int _prop = 0;
            string _key = null;

            switch (property)
            {
                case Property p:    _prop = (int)p; break;
                case string s:      _key = s;       break;
                default:
                    Console.WriteLine("error: unknown property specifier in method SetProperty().");
                    return false;
            }
            return 0 != mpr_obj_remove_prop(this._obj, _prop, _key);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_obj_push(IntPtr obj);
        public Object Push()
        {
            mpr_obj_push(_obj);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_obj_get_graph(IntPtr obj);
        public Graph GetGraph()
        {
            return new Graph(mpr_obj_get_graph(_obj));
        }

        internal IntPtr _obj;
        internal bool _owned;
    }