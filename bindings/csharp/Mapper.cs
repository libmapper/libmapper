using System;
using System.Threading;
using System.Runtime.InteropServices;
using System.Collections;
using System.Collections.Generic;

namespace Mapper
{
    [Flags]
    public enum Type
    {
        Device    = 0x01,             //!< Devices only.
        SignalIn  = 0x02,             //!< Input signals.
        SignalOut = 0x04,             //!< Output signals.
        Signal    = 0x06,             //!< All signals.
        MapIn     = 0x08,             //!< Incoming maps.
        MapOut    = 0x10,             //!< Outgoing maps.
        Map       = 0x18,             //!< All maps.
        Object    = 0x1F,             //!< All objects: devices, signale, and maps
        List      = 0x40,             //!< object query.
        Graph     = 0x41,             //!< Graph.
        Boolean   = 'b',  /* 0x62 */  //!< Boolean value.
        Type      = 'c',  /* 0x63 */  //!< libmapper data type.
        Double    = 'd',  /* 0x64 */  //!< 64-bit float.
        Float     = 'f',  /* 0x66 */  //!< 32-bit float.
        Int64     = 'h',  /* 0x68 */  //!< 64-bit integer.
        Int32     = 'i',  /* 0x69 */  //!< 32-bit integer.
        String    = 's',  /* 0x73 */  //!< String.
        Time      = 't',  /* 0x74 */  //!< 64-bit NTP timestamp.
        Pointer   = 'v',  /* 0x76 */  //!< pointer.
        Null      = 'N'   /* 0x4E */  //!< NULL value.
    }

    public enum Operator {
        DoesNotExist            = 0x01, //!< Property does not exist.
        IsEqual                 = 0x02, //!< Property value == query value.
        Exists                  = 0x03, //!< Property exists for this entity.
        IsGreaterThan           = 0x04, //!< Property value > query value.
        IsGreaterThanOrEqual    = 0x05, //!< Property value >= query value
        IsLessThan              = 0x06, //!< Property value < query value
        IsLessThanOrEqual       = 0x07, //!< Property value <= query value
        IsNotEqual              = 0x08, //!< Property value != query value
        All                     = 0x10, //!< Applies to all elements of value
        Any                     = 0x20  //!< Applies to any element of value
    }

    public enum Direction {
        Incoming    = 0x01, //!< Signal is an input
        Outgoing    = 0x02, //!< Signal is an output
        Any         = 0x03, //!< Either incoming or outgoing
        Both        = 0x04  /*!< Both directions apply.  Currently signals cannot be both inputs
                             *   and outputs, so this value is only used for querying device maps
                             *   that touch only local signals. */
    }

    public class Time
    {
        // internal long _time;

        [StructLayout(LayoutKind.Explicit)]
        internal struct timeStruct
        {
            [FieldOffset(0)]
            internal long ntp;
            [FieldOffset(0)]
            internal UInt32 sec;
            [FieldOffset(4)]
            internal UInt32 frac;
        }
        internal timeStruct data;

        public Time(long ntp) { data.ntp = ntp; }
        public Time(Time time) { data.ntp = time.data.ntp; }
        public Time() { data.sec = 0; data.frac = 1; }
        public Time(double seconds) { this.setDouble(seconds); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe private static extern int mpr_time_set(IntPtr l, long r);
        unsafe public Time set(Time time)
        {
            fixed (void* t = &data) {
                mpr_time_set((IntPtr)t, time.data.ntp);
            }
            return this;
        }

        public Time setDouble(Double seconds) {
            if (seconds > 0.0) {
                data.sec = (UInt32)Math.Floor(seconds);
                seconds -= data.sec;
                data.frac = (UInt32) (((double)seconds) * 4294967296.0);
            }
            else
                data.ntp = 0;
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe private static extern double mpr_time_as_dbl(IntPtr t);
        unsafe private static double as_dbl(Time time) {
            fixed (void *t = &time.data) {
                return mpr_time_as_dbl((IntPtr)t);
            }
        }

        public Time add(Time addend) {
            data.sec += addend.data.sec;
            data.frac += addend.data.frac;
            if (data.frac < addend.data.frac) /* overflow */
                ++data.sec;
            return this;
        }

        public Time subtract(Time subtrahend) {
            if (data.sec > subtrahend.data.sec) {
                data.sec -= subtrahend.data.sec;
                if (data.frac < subtrahend.data.frac) /* overflow */
                    --data.sec;
                data.frac -= subtrahend.data.frac;
            }
            else
                data.ntp = 0;
            return this;
        }

        public Time multiply(double multiplicand) {
            if (multiplicand > 0.0) {
                multiplicand *= Time.as_dbl(this);
                data.sec = (UInt32) Math.Floor(multiplicand);
                multiplicand -= data.sec;
                data.frac = (UInt32) (multiplicand * 4294967296.0);
            }
            else
                data.ntp = 0;
            return this;
        }

        /* casting between Time and double */
        public static implicit operator double(Time t) => Time.as_dbl(t);
        public static explicit operator Time(double d) => new Time(d);

        /* Overload some arithmetic operators */
        public static Time operator +(Time a, Time b)
            => new Time(a).add(b);

        public static Time operator -(Time a, Time b)
            => new Time(a).subtract(b);

        public static Time operator *(Time a, double b)
            => new Time(a).multiply(b);

        public static bool operator ==(Time a, Time b)
            => a.data.ntp == b.data.ntp;
        public static bool operator !=(Time a, Time b)
            => a.data.ntp != b.data.ntp;
        public static bool operator >(Time a, Time b)
            => a.data.ntp > b.data.ntp;
        public static bool operator <(Time a, Time b)
            => a.data.ntp < b.data.ntp;
        public static bool operator >=(Time a, Time b)
            => a.data.ntp >= b.data.ntp;
        public static bool operator <=(Time a, Time b)
            => a.data.ntp <= b.data.ntp;

        public override bool Equals(object o) {
            if (o == null)
                return false;
            var second = o as Time;
            return second != null && data.ntp == second.data.ntp;
        }

        public override int GetHashCode() {
            return (int)(data.sec ^ data.frac);
        }

        public override string ToString() => $"Mapper.Time:{this.data.sec}:{this.data.frac}";
    }

    public enum Property
    {
        Bundle              = 0x0100,
        Data                = 0x0200,
        Device              = 0x0300,
        Direction           = 0x0400,
        Ephemeral           = 0x0500,
        Expression          = 0x0600,
        Host                = 0x0700,
        Id                  = 0x0800,
        IsLocal             = 0x0900,
        Jitter              = 0x0A00,
        Length              = 0x0B00,
        LibVersion          = 0x0C00,
        Linked              = 0x0D00,
        Max                 = 0x0E00,
        Min                 = 0x0F00,
        Muted               = 0x1000,
        Name                = 0x1100,
        NumInstances        = 0x1200,
        NumMaps             = 0x1300,
        NumMapsIn           = 0x1400,
        NumMapsOut          = 0x1500,
        NumSigsIn           = 0x1600,
        NumSigsOut          = 0x1700,
        Ordinal             = 0x1800,
        Period              = 0x1900,
        Port                = 0x1A00,
        ProcessingLocation  = 0x1B00,
        Protocol            = 0x1C00,
        Rate                = 0x1D00,
        Scope               = 0x1E00,
        Signal              = 0x1F00,
        Status              = 0x2100,
        StealingMode        = 0x2200,
        Synced              = 0x2300,
        Type                = 0x2400,
        Unit                = 0x2500,
        UseInstances        = 0x2600,
        Version             = 0x2700
    }

    public abstract class Object
    {
        public enum Status {
            Expired      = 0x01,
            Staged       = 0x02,
            Ready        = 0x3E,
            Active       = 0x7E,
            Reserved     = 0x80,
            Any          = 0xFF
        }

        public Object() {
            _obj = IntPtr.Zero;
            _owned  = false;
        }
        protected Object(IntPtr obj) {
            _obj = obj;
            _owned  = false;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        protected static extern int mpr_obj_get_type(IntPtr obj);
        public Type getType()
            { return (Type) mpr_obj_get_type(_obj); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_obj_get_num_props(IntPtr obj);
        public int getNumProperties()
            { return mpr_obj_get_num_props(_obj); }

        unsafe internal dynamic buildValue(int len, int type, void *value, int property)
        {
            if (0 == len)
                return null;
            switch (type) {
                case (int)Type.Int32:
                    if (1 == len) {
                        int i = *(int*)value;
                        switch (property) {
                            case (int)Property.Direction:
                                return (Direction)i;
                            case (int)Property.ProcessingLocation:
                                return (Map.Location)i;
                            case (int)Property.Protocol:
                                return (Map.Protocol)i;
                            case (int)Property.Status:
                                return (Status)i;
                            case (int)Property.StealingMode:
                                return (Signal.Stealing)i;
                            case (int)Property.Type:
                                return (Type)i;
                            default:
                                return i;
                        }
                    }
                    else {
                        int[] arr = new int[len];
                        Marshal.Copy((IntPtr)value, arr, 0, len);
                        return arr;
                    }
                case (int)Type.Float:
                    if (1 == len)
                        return *(float*)value;
                    else {
                        float[] arr = new float[len];
                        Marshal.Copy((IntPtr)value, arr, 0, len);
                        return arr;
                    }
                case (int)Type.Double:
                    if (1 == len)
                        return *(double*)value;
                    else {
                        double[] arr = new double[len];
                        Marshal.Copy((IntPtr)value, arr, 0, len);
                        return arr;
                    }
                case (int)Type.Boolean:
                    if (1 == len)
                        return *(int*)value != 0;
                    else {
                        bool[] arr = new bool[len];
                        for (int i = 0; i < len; i++)
                            arr[i] = ((int*)value)[i] != 0;
                        return arr;
                    }
                case (int)Type.Int64:
                    if (1 == len)
                        return *(long*)value;
                    else {
                        long[] arr = new long[len];
                        Marshal.Copy((IntPtr)value, arr, 0, len);
                        return arr;
                    }
                case (int)Type.Time:
                    if (1 == len)
                        return new Time(*(long*)value);
                    else {
                        Time[] arr = new Time[len];
                        for (int i = 0; i < len; i++)
                            arr[0] = new Time(((long*)value)[i]);
                        return arr;
                    }
                case (int)Type.String:
                    if (1 == len)
                        return new String(Marshal.PtrToStringAnsi((IntPtr)value));
                    else {
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
                    if (1 == len) {
                        switch (property) {
                            case (int)Property.Device:
                            case (int)Property.Linked:
                            case (int)Property.Scope:
                                return new List<Device>((IntPtr)value, Type.Device);
                            case (int)Property.Signal:
                                return new List<Signal>((IntPtr)value, Type.Signal);
                            default:
                                Console.WriteLine("error: missing List type.");
                                return null;
                        }
                    }
                    else {
                        Console.WriteLine("error: arrays of List are not supported.");
                        return null;
                    }
                default:
                    Console.WriteLine("error: unhandled data type in Object.buildValue().");
                    return null;
            }
        }

        // TODO: implement IDictionary?

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe internal static extern int mpr_obj_get_prop_by_idx(IntPtr obj, int idx, ref char *key,
                                                                  ref int len, ref int type,
                                                                  ref void *value, ref int publish);

        unsafe public (String, dynamic) getProperty(int index)
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
            return (new string(Marshal.PtrToStringAnsi((IntPtr)key)), buildValue(len, type, val, idx));
        }

        unsafe public dynamic getProperty(Property property)
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
            return buildValue(len, type, val, idx);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe internal static extern int mpr_obj_get_prop_by_key(IntPtr obj,
                                                                  [MarshalAs(UnmanagedType.LPStr)] string key,
                                                                  ref int len, ref int type,
                                                                  ref void *value, ref int publish);
        unsafe public dynamic getProperty(string key)
        {
            int len = 0;
            int type = 0;
            void *val = null;
            int pub = 0;
            int idx;
            idx = mpr_obj_get_prop_by_key(this._obj, key, ref len, ref type, ref val, ref pub);
            if (0 == idx || 0 == len)
                return null;
            return buildValue(len, type, val, idx);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe internal static extern
        int mpr_obj_get_prop_as_int32(IntPtr obj, int prop, [MarshalAs(UnmanagedType.LPStr)] string key);

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe internal static extern
        int mpr_obj_set_prop(IntPtr obj, int prop, [MarshalAs(UnmanagedType.LPStr)] string key,
                             int len, int type, void* value, int publish);
        unsafe public Object setProperty<P, T>(P property, T value)
        {
            int _prop = 0;
            string _key = null;

            switch (property) {
                case Property p:    _prop = (int)p; break;
                case string s:      _key = s;       break;
                default:
                    Console.WriteLine("error: unknown property specifier in method setProperty().");
                    return this;
            }

            switch (value) {
                case int i:
                    mpr_obj_set_prop(_obj, _prop, _key, 1, (int)Type.Int32, (void*)&i, 1);
                    break;
                case float f:
                    mpr_obj_set_prop(_obj, _prop, _key, 1, (int)Type.Float, (void*)&f, 1);
                    break;
                case double d:
                    mpr_obj_set_prop(_obj, _prop, _key, 1, (int)Type.Double, (void*)&d, 1);
                    break;
                case int[] i:
                    fixed(int *temp = &i[0]) {
                        IntPtr intPtr = new IntPtr((void*)temp);
                        mpr_obj_set_prop(_obj, _prop, _key, i.Length, (int)Type.Int32, (void*)intPtr, 1);
                    }
                    break;
                case float[] f:
                    fixed(float *temp = &f[0]) {
                        IntPtr intPtr = new IntPtr((void*)temp);
                        mpr_obj_set_prop(_obj, _prop, _key, f.Length, (int)Type.Float, (void*)intPtr, 1);
                    }
                    break;
                case double[] d:
                    fixed(double *temp = &d[0]) {
                        IntPtr intPtr = new IntPtr((void*)temp);
                        mpr_obj_set_prop(_obj, _prop, _key, d.Length, (int)Type.Double, (void*)intPtr, 1);
                    }
                    break;
                default:
                    Console.WriteLine("error: unhandled type in setProperty().");
                    break;
            }
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_obj_remove_prop(IntPtr obj, int prop,
                                                      [MarshalAs(UnmanagedType.LPStr)] string key);
        public bool removeProperty<P>(P property) {
            int _prop = 0;
            string _key = null;

            switch (property) {
                case Property p:    _prop = (int)p; break;
                case string s:      _key = s;       break;
                default:
                    Console.WriteLine("error: unknown property specifier in method setProperty().");
                    return false;
            }
            return 0 != mpr_obj_remove_prop(this._obj, _prop, _key);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_obj_push(IntPtr obj);
        public Object push()
        {
            mpr_obj_push(_obj);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_obj_get_graph(IntPtr obj);
        public Graph graph()
        {
            return new Graph(mpr_obj_get_graph(_obj));
        }

        internal IntPtr _obj;
        internal bool _owned;
    }

    public abstract class _List {
        protected Type _type;
        private IntPtr _list;

        public _List() { _type = Type.Null; _list = IntPtr.Zero; }
        public _List(IntPtr list, Type type) { _type = type; _list = list; }

        // TODO: probably need refcounting to check if we should free the underlying mpr_list
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_list_free(IntPtr list);
        public void free() {
            Console.WriteLine("calling _List::free()");
            mpr_list_free(_list);
            _list = IntPtr.Zero;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_list_get_size(IntPtr list);
        public int Count() {
            return mpr_list_get_size(_list);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_idx(IntPtr list, int index);
        public IntPtr get_idx(int index) {
            return mpr_list_get_idx(_list, index);
        }
        public IntPtr deref() {
            unsafe {
                return new IntPtr(*(void**)_list);
            }
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_cpy(IntPtr list);

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_union(IntPtr list1, IntPtr list2);
        public _List join(_List rhs) {
            _list = mpr_list_get_union(_list, mpr_list_get_cpy(rhs._list));
            return this;
        }
        public static IntPtr get_union(_List a, _List b) {
            return mpr_list_get_union(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_isect(IntPtr list1, IntPtr list2);
        public _List intersect(_List rhs) {
            _list = mpr_list_get_isect(_list, mpr_list_get_cpy(rhs._list));
            return this;
        }
        public static IntPtr get_isect(_List a, _List b) {
            return mpr_list_get_isect(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_diff(IntPtr list1, IntPtr list2);
        public _List subtract(_List rhs) {
            _list = mpr_list_get_diff(_list, mpr_list_get_cpy(rhs._list));
            return this;
        }
        public static IntPtr get_diff(_List a, _List b) {
            return mpr_list_get_diff(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_next(IntPtr list);
        public bool get_next() {
            _list = mpr_list_get_next(_list);
            return _list != IntPtr.Zero;
        }

        public override string ToString() => $"Mapper.List<{_type}>";
    }

    public class List<T> : _List, IEnumerator, IEnumerable, IDisposable
        where T : Object, new()
    {
        internal List(IntPtr list, Type type) : base(list, type) {}

        public T this[int index] {
            get { T t = new T(); t._obj = this.get_idx(index); return t; }
        }

        /* Overload some arithmetic operators */
        public static List<T> operator +(List<T> a, List<T> b)
            => new List<T>( get_union(a, b), a._type );

        public static List<T> operator *(List<T> a, List<T> b)
            => new List<T>( get_isect(a, b), a._type );

        public static List<T> operator -(List<T> a, List<T> b)
            => new List<T>( get_diff(a, b), a._type );

        /* Methods for enumeration */
        public IEnumerator GetEnumerator() {
            return (IEnumerator)this;
        }

        public void Reset() {
            // TODO: throw NotSupportedException;
        }

        public bool MoveNext() {
            return this.get_next();
        }

        void IDisposable.Dispose() {
            this.free();
            // Dispose(true);
            // GC.SuppressFinalize(this);
        }

        public T Current {
            get { T t = new T(); t._obj = this.deref(); return t; }
        }

        object IEnumerator.Current {
            get { return Current; }
        }
    }

    public class Graph : Object
    {
        private delegate void HandlerDelegate(IntPtr graph, IntPtr obj, int evt, IntPtr data);

        public enum Event {
            New,            //!< New record has been added to the graph.
            Modified,       //!< The existing record has been modified.
            Removed,        //!< The existing record has been removed.
            Expired         //!< The graph has lost contact with the remote entity.
        }

        internal Graph(IntPtr obj) : base(obj) {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_new(int flags);
        public Graph(Type flags) : base(mpr_graph_new((int)flags)) { _owned = true; }
        public Graph() : base(mpr_graph_new((int)Type.Object)) { _owned = true; }

        // TODO: check if Graph is used by a Device
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_free(IntPtr graph);
        ~Graph()
            { if (_owned) mpr_graph_free(this._obj); }

        public new Graph setProperty<P, T>(P property, T value) {
            base.setProperty(property, value);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_set_interface(IntPtr graph,
                                                           [MarshalAs(UnmanagedType.LPStr)] string iface);
        public Graph setInterface(string iface) {
            mpr_graph_set_interface(this._obj, iface);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_get_interface(IntPtr graph);
        public string getInterface() {
            IntPtr iface = mpr_graph_get_interface(this._obj);
            return Marshal.PtrToStringAnsi(iface);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_set_address(IntPtr graph,
                                                         [MarshalAs(UnmanagedType.LPStr)] string group,
                                                         int port);
        public Graph setAddress(string group, int port) {
            mpr_graph_set_address(this._obj, group, port);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_get_address(IntPtr graph);
        public string getAddress() {
            IntPtr addr = mpr_graph_get_address(this._obj);
            return Marshal.PtrToStringAnsi(addr);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_graph_poll(IntPtr graph, int block_ms);
        public int poll(int block_ms = 0)
            { return mpr_graph_poll(this._obj, block_ms); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_subscribe(IntPtr graph, IntPtr dev, int types, int timeout);
        public Graph subscribe(Device device, Type types, int timeout = -1) {
            mpr_graph_subscribe(this._obj, device._obj, (int)types, timeout);
            return this;
        }
        public Graph subscribe(Type types, int timeout = -1) {
            mpr_graph_subscribe(this._obj, IntPtr.Zero, (int)types, timeout);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_unsubscribe(IntPtr graph, IntPtr dev);
        public Graph unsubscribe(Device device) {
            mpr_graph_unsubscribe(this._obj, device._obj);
            return this;
        }
        public Graph unsubscribe() {
            mpr_graph_unsubscribe(this._obj, IntPtr.Zero);
            return this;
        }

        private void _handler(IntPtr graph, IntPtr obj, int evt, IntPtr data) {
            Type type = (Type) mpr_obj_get_type(obj);
            // Event event = (Event) evt;
            Object o;
            Event e = (Event)evt;
            switch (type) {
                case Type.Device:
                    o = new Device(obj);
                    break;
                case Type.Signal:
                    o = new Signal(obj);
                    break;
                case Type.Map:
                    o = new Map(obj);
                    break;
                default:
                    return;
            }
            handlers.ForEach(delegate(Handler h) {
                if ((h._types & type) != 0)
                    h._callback(o, e);
            });
        }

        protected class Handler
        {
            public Action<Object, Graph.Event> _callback;
            public Type _types;

            public Handler(Action<Object, Graph.Event> callback, Type types)
            {
                _callback = callback;
                _types = types;
            }
        }
        private System.Collections.Generic.List<Handler> handlers = new System.Collections.Generic.List<Handler>();

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_add_cb(IntPtr graph, IntPtr handler, int events, IntPtr data);
        public Graph addCallback(Action<Object, Graph.Event> callback, Type types)
        {
            // TODO: check if handler is already registered
            if (handlers.Count == 0) {
                mpr_graph_add_cb(this._obj,
                                 Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(_handler)),
                                 (int)Type.Object,
                                 IntPtr.Zero);
            }
            handlers.Add(new Handler(callback, types));
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_remove_cb(IntPtr graph, IntPtr cb, IntPtr data);
        public Graph removeCallback(Action<Object, Graph.Event> callback)
        {
            int i = -1, found = -1;
            handlers.ForEach(delegate(Handler h) {
                if (h._callback == callback)
                    found = i;
                ++i;
            });
            if (i >= 0) {
                handlers.RemoveAt(found);
                if (handlers.Count == 0) {
                    mpr_graph_remove_cb(this._obj,
                                        Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(_handler)),
                                        IntPtr.Zero);
                }
            }
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_get_list(IntPtr graph, int type);
        public List<Device> devices() {
            return new List<Device>(mpr_graph_get_list(this._obj, (int)Type.Device), Type.Device);
        }
        public List<Signal> signals() {
            return new List<Signal>(mpr_graph_get_list(this._obj, (int)Type.Signal), Type.Signal);
        }
        public List<Map> maps() {
            return new List<Map>(mpr_graph_get_list(this._obj, (int)Type.Map), Type.Map);
        }
    }

    public class Signal : Object
    {
        private delegate void HandlerDelegate(IntPtr sig, int evt, UInt64 instanceId, int length,
                                              int type, IntPtr value, IntPtr time);
        [Flags]
        public enum Event {
            New                 = 0x01, //!< New instance has been created.
            UpstreamRelease     = 0x02, //!< Instance was released upstream.
            DownstreamRelease   = 0x04, //!< Instance was released downstream.
            Overflow            = 0x08, //!< No local instances left.
            Update              = 0x10, //!< Instance value has been updated.
            All                 = 0x1F
        }

        public enum Stealing {
            None,       //!< No stealing will take place.
            Oldest,     //!< Steal the oldest instance.
            Newest,     //!< Steal the newest instance.
        }

        private enum HandlerType {
            None,
            SingletonInt,
            SingletonFloat,
            SingletonDouble,
            SingletonIntVector,
            SingletonFloatVector,
            SingletonDoubleVector,
            InstancedInt,
            InstancedFloat,
            InstancedDouble,
            InstancedIntVector,
            InstancedFloatVector,
            InstancedDoubleVector
        }

        public Signal()
            : base() {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_sig_get_dev(IntPtr sig);
        public Device getDevice() {
            return new Device(mpr_sig_get_dev(this._obj));
        }

        public override string ToString() => $"Mapper.Signal:{this.getDevice().getProperty(Property.Name)}:{this.getProperty(Property.Name)}";

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_sig_set_cb(IntPtr sig, IntPtr handler, int events);

        // construct from mpr_sig pointer
        internal Signal(IntPtr sig) : base(sig) {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe private static extern void mpr_sig_set_value(IntPtr sig, UInt64 id, int len, int type, void* val);
        unsafe private void _setValue(int value, UInt64 instanceId) {
            mpr_sig_set_value(this._obj, instanceId, 1, (int)Type.Int32, (void*)&value);
        }
        unsafe private void _setValue(float value, UInt64 instanceId) {
            mpr_sig_set_value(this._obj, instanceId, 1, (int)Type.Float, (void*)&value);
        }
        unsafe private void _setValue(double value, UInt64 instanceId) {
            mpr_sig_set_value(this._obj, instanceId, 1, (int)Type.Double, (void*)&value);
        }
        unsafe private void _setValue(int[] value, UInt64 instanceId) {
            fixed(int* temp = &value[0]) {
                IntPtr intPtr = new IntPtr((void*)temp);
                mpr_sig_set_value(this._obj, instanceId, value.Length, (int)Type.Int32, (void*)intPtr);
            }
        }
        unsafe private void _setValue(float[] value, UInt64 instanceId) {
            fixed(float* temp = &value[0]) {
                IntPtr intPtr = new IntPtr((void*)temp);
                mpr_sig_set_value(this._obj, instanceId, value.Length, (int)Type.Float, (void*)intPtr);
            }
        }
        unsafe private void _setValue(double[] value, UInt64 instanceId) {
            fixed(double* temp = &value[0]) {
                IntPtr intPtr = new IntPtr((void*)temp);
                mpr_sig_set_value(this._obj, instanceId, value.Length, (int)Type.Double, (void*)intPtr);
            }
        }
        public Signal setValue<T>(T value) {
            dynamic temp = value;
            _setValue(temp, 0);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe private static extern void* mpr_sig_get_value(IntPtr sig, UInt64 id, ref long time);
        unsafe public (dynamic, Time) getValue(UInt64 instanceId = 0) {
            int len = mpr_obj_get_prop_as_int32(this._obj, (int)Property.Length, null);
            int type = mpr_obj_get_prop_as_int32(this._obj, (int)Property.Type, null);
            long time = 0;
            void *val = mpr_sig_get_value(this._obj, instanceId, ref time);
            return (buildValue(len, type, val, 0), new Time(*(long*)time));
        }
        // unsafe public dynamic getValue(UInt64 instanceId = 0) {
        //     int len = mpr_obj_get_prop_as_int32(this._obj, (int)Property.Length, null);
        //     int type = mpr_obj_get_prop_as_int32(this._obj, (int)Property.Type, null);
        //     void *val = mpr_sig_get_value(this._obj, instanceId, IntPtr.Zero);
        //     return buildValue(len, type, val, 0);
        // }

        public class Instance : Signal
        {
            internal Instance(IntPtr sig, UInt64 inst) : base(sig) {
                this.id = inst;
            }

            public (dynamic, Time) getValue() {
                return getValue(id);
            }

            public new Instance setValue<T>(T value) {
                dynamic temp = value;
                _setValue(temp, id);
                return this;
            }

            [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
            unsafe private static extern void mpr_sig_release_inst(IntPtr sig, UInt64 id);
            public void release() {
                mpr_sig_release_inst(this._obj, id);
            }

            public readonly UInt64 id;
        }

        public Instance instance(UInt64 id) {
            return new Instance(this._obj, id);
        }
        public Instance instance(int id) {
            return new Instance(this._obj, (UInt64)id);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern ulong mpr_sig_get_oldest_inst_id(IntPtr sig);
        public Instance oldestInstance() {
            return new Instance(this._obj, mpr_sig_get_oldest_inst_id(this._obj));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern ulong mpr_sig_get_newest_inst_id(IntPtr sig);
        public Instance newestInstance() {
            return new Instance(this._obj, mpr_sig_get_newest_inst_id(this._obj));
        }

        private void _handler(IntPtr sig, int evt, UInt64 inst, int length,
                              int type, IntPtr value, IntPtr time) {
            Event e = (Event)evt;
            switch (this.handlerType) {
                case HandlerType.SingletonInt: {
                    unsafe {
                        int ivalue = 0;
                        if (value != IntPtr.Zero)
                            ivalue = *(int*)value;
                        this.handlers.singletonInt(new Signal(sig), e, ivalue);
                    }
                    break;
                }
                case HandlerType.SingletonFloat:
                    unsafe {
                        float fvalue = 0;
                        if (value != IntPtr.Zero)
                            fvalue = *(float*)value;
                        this.handlers.singletonFloat(new Signal(sig), e, fvalue);
                    }
                    break;
                case HandlerType.SingletonDouble:
                    unsafe {
                        double dvalue = 0;
                        if (value != IntPtr.Zero)
                            dvalue = *(double*)value;
                        this.handlers.singletonDouble(new Signal(sig), e, dvalue);
                    }
                    break;
                case HandlerType.SingletonIntVector: {
                    if (value == IntPtr.Zero)
                        this.handlers.singletonIntVector(new Signal(sig), e, new int[0]);
                    unsafe {
                        int[] arr = new int[length];
                        Marshal.Copy((IntPtr)value, arr, 0, length);
                        this.handlers.singletonIntVector(new Signal(sig), e, arr);
                    }
                    break;
                }
                case HandlerType.SingletonFloatVector:
                    if (value == IntPtr.Zero)
                        this.handlers.singletonFloatVector(new Signal(sig), e, new float[0]);
                    unsafe {
                        float[] arr = new float[length];
                        Marshal.Copy((IntPtr)value, arr, 0, length);
                        this.handlers.singletonFloatVector(new Signal(sig), e, arr);
                    }
                    break;
                case HandlerType.SingletonDoubleVector:
                    if (value == IntPtr.Zero)
                        this.handlers.singletonDoubleVector(new Signal(sig), e, new double[0]);
                    unsafe {
                        double[] arr = new double[length];
                        Marshal.Copy((IntPtr)value, arr, 0, length);
                        this.handlers.singletonDoubleVector(new Signal(sig), e, arr);
                    }
                    break;
                case HandlerType.InstancedInt:
                    unsafe {
                        int ivalue = 0;
                        if (value != IntPtr.Zero)
                            ivalue = *(int*)value;
                        this.handlers.instancedInt(new Signal.Instance(sig, inst), e, ivalue);
                    }
                    break;
                case HandlerType.InstancedFloat:
                    unsafe {
                        float fvalue = 0;
                        if (value != IntPtr.Zero)
                            fvalue = *(float*)value;
                        this.handlers.instancedFloat(new Signal.Instance(sig, inst), e, fvalue);
                    }
                    break;
                case HandlerType.InstancedDouble:
                    unsafe {
                        double dvalue = 0;
                        if (value != IntPtr.Zero)
                            dvalue = *(double*)value;
                        this.handlers.instancedDouble(new Signal.Instance(sig, inst), e, dvalue);
                    }
                    break;
                case HandlerType.InstancedIntVector: {
                    if (value == IntPtr.Zero)
                        this.handlers.instancedIntVector(new Signal.Instance(sig, inst), e, new int[0]);
                    unsafe {
                        int[] arr = new int[length];
                        Marshal.Copy((IntPtr)value, arr, 0, length);
                        this.handlers.instancedIntVector(new Signal.Instance(sig, inst), e, arr);
                    }
                    break;
                }
                case HandlerType.InstancedFloatVector:
                    if (value == IntPtr.Zero)
                        this.handlers.instancedFloatVector(new Signal.Instance(sig, inst), e, new float[0]);
                    unsafe {
                        float[] arr = new float[length];
                        Marshal.Copy((IntPtr)value, arr, 0, length);
                        this.handlers.instancedFloatVector(new Signal.Instance(sig, inst), e, arr);
                    }
                    break;
                case HandlerType.InstancedDoubleVector:
                    if (value == IntPtr.Zero)
                        this.handlers.instancedDoubleVector(new Signal.Instance(sig, inst), e, new double[0]);
                    unsafe {
                        double[] arr = new double[length];
                        Marshal.Copy((IntPtr)value, arr, 0, length);
                        this.handlers.instancedDoubleVector(new Signal.Instance(sig, inst), e, arr);
                    }
                    break;
                default:
                    break;
            }
        }

        ~Signal()
            {}

        private Boolean _setCallback(Action<Signal, Signal.Event, int> h, int type)
        {
            if (type != (int)Type.Int32)
                return false;
            handlerType = HandlerType.SingletonInt;
            handlers.singletonInt = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal, Signal.Event, int[]> h, int type)
        {
            if (type != (int)Type.Int32)
                return false;
            handlerType = HandlerType.SingletonIntVector;
            handlers.singletonIntVector = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal, Signal.Event, float> h, int type)
        {
            if (type != (int)Type.Float)
                return false;
            handlerType = HandlerType.SingletonFloat;
            handlers.singletonFloat = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal, Signal.Event, float[]> h, int type)
        {
            if (type != (int)Type.Float)
                return false;
            handlerType = HandlerType.SingletonFloatVector;
            handlers.singletonFloatVector = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal, Signal.Event, double> h, int type)
        {
            if (type != (int)Type.Double)
                return false;
            handlerType = HandlerType.SingletonDouble;
            handlers.singletonDouble = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal, Signal.Event, double[]> h, int type)
        {
            if (type != (int)Type.Double)
                return false;
            handlerType = HandlerType.SingletonDoubleVector;
            handlers.singletonDoubleVector = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal.Instance, Signal.Event, int> h, int type)
        {
            if (type != (int)Type.Int32)
                return false;
            handlerType = HandlerType.InstancedInt;
            handlers.instancedInt = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal.Instance, Signal.Event, int[]> h, int type)
        {
            if (type != (int)Type.Int32)
                return false;
            handlerType = HandlerType.InstancedIntVector;
            handlers.instancedIntVector = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal.Instance, Signal.Event, float> h, int type)
        {
            if (type != (int)Type.Float)
                return false;
            handlerType = HandlerType.InstancedFloat;
            handlers.instancedFloat = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal.Instance, Signal.Event, float[]> h, int type)
        {
            if (type != (int)Type.Float)
                return false;
            handlerType = HandlerType.InstancedFloatVector;
            handlers.instancedFloatVector = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal.Instance, Signal.Event, double> h, int type)
        {
            if (type != (int)Type.Double)
                return false;
            handlerType = HandlerType.InstancedDouble;
            handlers.instancedDouble = h;
            return true;
        }

        private Boolean _setCallback(Action<Signal.Instance, Signal.Event, double[]> h, int type)
        {
            if (type != (int)Type.Double)
                return false;
            handlerType = HandlerType.InstancedDoubleVector;
            handlers.instancedDoubleVector = h;
            return true;
        }

        public Signal setCallback<T>(T handler, Event events = Event.All)
        {
            dynamic temp = handler;
            int type = mpr_obj_get_prop_as_int32(this._obj, (int)Property.Type, null);
            if (!_setCallback(temp, type)) {
                Console.WriteLine("error: wrong data type in signal handler.");
                return this;
            }
            mpr_sig_set_cb(this._obj,
                           Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(_handler)),
                           (int)events);
            return this;
        }

        public new Signal setProperty<P, T>(P property, T value) {
            base.setProperty(property, value);
            return this;
        }
        public new Signal push() {
            base.push();
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_sig_get_maps(IntPtr sig, int dir);
        public List<Map> maps(Direction direction = Direction.Any) {
            return new List<Map>(mpr_sig_get_maps(this._obj, (int)direction), Type.Map);
        }

        [StructLayout(LayoutKind.Explicit)]
        struct Handlers
        {
            [FieldOffset(0)]
            internal Action<Signal, Signal.Event, int> singletonInt;
            [FieldOffset(0)]
            internal Action<Signal, Signal.Event, float> singletonFloat;
            [FieldOffset(0)]
            internal Action<Signal, Signal.Event, double> singletonDouble;
            [FieldOffset(0)]
            internal Action<Signal, Signal.Event, int[]> singletonIntVector;
            [FieldOffset(0)]
            internal Action<Signal, Signal.Event, float[]> singletonFloatVector;
            [FieldOffset(0)]
            internal Action<Signal, Signal.Event, double[]> singletonDoubleVector;
            [FieldOffset(0)]
            internal Action<Signal.Instance, Signal.Event, int> instancedInt;
            [FieldOffset(0)]
            internal Action<Signal.Instance, Signal.Event, float> instancedFloat;
            [FieldOffset(0)]
            internal Action<Signal.Instance, Signal.Event, double> instancedDouble;
            [FieldOffset(0)]
            internal Action<Signal.Instance, Signal.Event, int[]> instancedIntVector;
            [FieldOffset(0)]
            internal Action<Signal.Instance, Signal.Event, float[]> instancedFloatVector;
            [FieldOffset(0)]
            internal Action<Signal.Instance, Signal.Event, double[]> instancedDoubleVector;
        }
        private Handlers handlers;
        private HandlerType handlerType = HandlerType.None;
    }

    public class Device : Object
    {
        public Device()
            : base() {}
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_dev_new([MarshalAs(UnmanagedType.LPStr)] string devname, IntPtr graph);
        public Device(string name)
            : base(mpr_dev_new(name, IntPtr.Zero)) { this._owned = true; }
        public Device(string name, Graph graph)
            : base(mpr_dev_new(name, graph._obj)) { this._owned = true; }

        // construct from mpr_dev pointer
        internal Device(IntPtr dev) : base(dev) {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_dev_free(IntPtr dev);
        ~Device()
            { if (this._owned) mpr_dev_free(this._obj); }

        public override string ToString() => $"Mapper.Device:{this.getProperty(Property.Name)}";

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_dev_get_is_ready(IntPtr dev);
        public int getIsReady()
            { return mpr_dev_get_is_ready(this._obj); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_dev_poll(IntPtr dev, int block_ms);
        public int poll(int block_ms)
            { return mpr_dev_poll(this._obj, block_ms); }
        public int poll()
            { return mpr_dev_poll(this._obj, 0); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_dev_update_maps(IntPtr dev);
        public Device updateMaps() {
            mpr_dev_update_maps(this._obj);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_sig_new(IntPtr dev, int direction,
                                                 [MarshalAs(UnmanagedType.LPStr)] string name,
                                                 int length, int type,
                                                 [MarshalAs(UnmanagedType.LPStr)] string unit,
                                                 IntPtr min, IntPtr max, IntPtr num_inst,
                                                 IntPtr handler, int events);
        public Signal addSignal(Direction direction, string name, int length, Type type,
                                string unit = null, int numInstances = -1)
        {
            IntPtr instPtr = IntPtr.Zero;
            if (numInstances != -1) {
                unsafe {
                    instPtr = new IntPtr(&numInstances);
                }
            }
            IntPtr sigptr = mpr_sig_new(this._obj, (int) direction, name, length, (int) type, unit,
                                        IntPtr.Zero, IntPtr.Zero, instPtr, IntPtr.Zero, 0);
            return new Signal(sigptr);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_sig_free(IntPtr sig);
        public Device removeSignal(Signal signal) {
            mpr_sig_free(signal._obj);
            signal._obj = IntPtr.Zero;
            return this;
        }

        public new Device setProperty<P, T>(P property, T value) {
            base.setProperty(property, value);
            return this;
        }
        public new Device push() {
            base.push();
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_dev_get_sigs(IntPtr dev, int dir);
        public List<Signal> signals(Direction direction = Direction.Any) {
            return new List<Signal>(mpr_dev_get_sigs(this._obj, (int) direction), Type.Signal);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_dev_get_maps(IntPtr dev, int dir);
        public List<Map> maps(Direction direction = Direction.Any) {
            return new List<Map>(mpr_dev_get_maps(this._obj, (int) direction), Type.Map);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern long mpr_dev_get_time(IntPtr dev);
        public Time getTime() {
            return new Time(mpr_dev_get_time(this._obj));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern long mpr_dev_set_time(IntPtr dev, long time);
        public Device setTime(Time time) {
            mpr_dev_set_time(this._obj, time.data.ntp);
            return this;
        }
    }

    public class Map : Object
    {
        public enum Location {
            Source      = 0x01, //!< Source signal(s) for this map.
            Destination = 0x02, //!< Destination signal(s) for this map.
            Any         = 0x03  //!< Either source or destination signals.
        }

        public enum Protocol {
            UDP,              //!< Map updates are sent using UDP.
            TCP               //!< Map updates are sent using TCP.
        }

        public Map()
            : base() {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe private static extern IntPtr mpr_map_new(int num_srcs, void* srcs, int num_dsts, void* dsts);
        unsafe public Map(Signal source, Signal destination)
        {
            fixed (void* s = &source._obj) {
                fixed (void* d = &destination._obj) {
                    this._obj = mpr_map_new(1, s, 1, d);
                }
            }
        }

        // construct from mpr_map pointer
        internal Map(IntPtr map) : base(map) {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_map_new_from_str([MarshalAs(UnmanagedType.LPStr)] string expr,
                                                          IntPtr sig0, IntPtr sig1, IntPtr sig2,
                                                          IntPtr sig3, IntPtr sig4, IntPtr sig5,
                                                          IntPtr sig6, IntPtr sig7, IntPtr sig8,
                                                          IntPtr sig9, IntPtr sig10);
        public Map(string expression, params Signal[] signals)
        {
            IntPtr[] a = new IntPtr[10];
            for (int i = 0; i < 10; i++) {
                if (i < signals.Length)
                    a[i] = signals[i]._obj;
                else
                    a[i] = default(IntPtr);
            }
            this._obj = mpr_map_new_from_str(expression, a[0], a[1], a[2], a[3], a[4], a[5],
                                             a[6], a[7], a[8], a[9], default(IntPtr));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_map_get_is_ready(IntPtr map);
        public int getIsReady()
            { return mpr_map_get_is_ready(this._obj); }

        public new Map setProperty<P, T>(P property, T value) {
            base.setProperty(property, value);
            return this;
        }
        public new Map push() {
            base.push();
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_map_refresh(IntPtr map);
        public Map refresh() {
            mpr_map_refresh(this._obj);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_map_release(IntPtr map);
        public void release() {
            mpr_map_release(this._obj);
            this._obj = IntPtr.Zero;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_map_get_sigs(IntPtr map, int loc);
        public List<Signal> signals(Location location = Location.Any) {
            return new List<Signal>(mpr_map_get_sigs(this._obj, (int)location), Type.Signal);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_map_get_sig_idx(IntPtr map, IntPtr sig);
        public int signalIndex(Signal signal) {
            return mpr_map_get_sig_idx(this._obj, signal._obj);
        }
    }
}
