using System;
using System.Threading;
using System.Runtime.InteropServices;

namespace Mapper
{
    public enum Type
    {
        Device    = 0x01,             //!< Devices only.
        SignalIn  = 0x02,             //!< Input signals.
        SignalOut = 0x04,             //!< Output signals.
        Signal    = 0x06,             //!< All signals.
        MapIn     = 0x08,             //!< Incoming maps.
        MapOut    = 0x10,             //!< Outgoing maps.
        Map       = 0x18,               //!< All maps.
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
        // TODO: port time implementation instead of using unsafe library calls
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe private static extern int mpr_time_set_dbl(void* obj, double sec);
        unsafe public Time setDouble(Double seconds)
        {
            fixed (void* t = &_time) {
                mpr_time_set_dbl(t, seconds);
            }
            return this;
        }
        internal long _time;
    }

    public class List
    {
        public List() {}
        ~List() {}

        // cast to vector

        // Iterator functions

        // public getSize();
        // public join();
        // public intersect();
        // public filter();
        // public subtract();
    }

    public enum Property
    {
        Data                = 0x0200,
        Device              = 0x0300,
        Direction           = 0x0400,
        Expression          = 0x0500,
        Host                = 0x0600,
        Id                  = 0x0700,
        Instance            = 0x0800,
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
            Reserved     = 0x80
        }

        // Graph getGraph()
        //     { return new Graph(_obj); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_obj_get_type(IntPtr obj);
        public Type getType()
            { return (Type) mpr_obj_get_type(_obj); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_obj_get_num_props(IntPtr obj);
        public int getNumProperties()
            { return mpr_obj_get_num_props(_obj); }

        // public Property getProperty(int idx) {}
        // public Property getProperty([MarshalAs(UnmanagedType.LPStr)] string name) {}
        // public Property getProperty() {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        internal static extern int mpr_obj_set_prop(IntPtr obj, int prop,
                                                    [MarshalAs(UnmanagedType.LPStr)] string key,
                                                    int len, int type, IntPtr value, int publish);
        // public Object setProperty() {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_obj_push(IntPtr obj);
        public Object push()
        {
            mpr_obj_push(_obj);
            return this;
        }

        internal IntPtr _obj;
    }

    public class Graph : Object
    {
        public enum Event {
            New,            //!< New record has been added to the graph.
            Modified,       //!< The existing record has been modified.
            Removed,        //!< The existing record has been removed.
            Expired         //!< The graph has lost contact with the remote entity.
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_new(int flags);
        public Graph(int flags)
            { _graph = mpr_graph_new(flags); }
        public Graph()
            { _graph = mpr_graph_new(0); }

        IntPtr _graph;
    }

    public class Signal : Object
    {
        private delegate void HandlerDelegate(IntPtr sig, int evt, UInt64 instance, int length,
                                              int type, IntPtr value, IntPtr time);
        public enum Event {
            NewInstance         = 0x01, //!< New instance has been created.
            UstreamRelease      = 0x02, //!< Instance was released upstream.
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

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_sig_set_cb(IntPtr sig, IntPtr handler, int events);

        // construct from mpr_sig pointer
        internal Signal(IntPtr sig,
                        Action<Signal, Signal.Event, UInt64, int, Type, IntPtr, IntPtr> h = null,
                        int events = 0)
        {
            this._obj = sig;

            if (h != null && events != 0) {
                _handler = h;

                mpr_sig_set_cb(this._obj,
                               Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(handler)),
                               events);
            }
        }

        private void handler(IntPtr sig, int evt, UInt64 instance, int length,
                             int type, IntPtr value, IntPtr time) {
            // recover signal object
            if (this._handler != null) {
                this._handler(new Signal(sig), (Event)evt, instance, length, (Type)type, value, time);
            }
        }

        ~Signal()
            {}

        public Signal setCallback(Action<Signal, Signal.Event, UInt64, int, Type, IntPtr, IntPtr> h = null,
                                int events = 0)
        {
            if (h != null && events != 0) {
                _handler = h;

                mpr_sig_set_cb(this._obj,
                               Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(handler)),
                               events);
            }
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe private static extern void mpr_sig_set_value(IntPtr sig, UInt64 id, int len, int type, void* val);
        unsafe private void _setValue(int value) {
            mpr_sig_set_value(this._obj, 0, 1, (int)Type.Int32, (void*)&value);
        }
        unsafe private void _setValue(float value) {
            mpr_sig_set_value(this._obj, 0, 1, (int)Type.Float, (void*)&value);
        }
        unsafe private void _setValue(double value) {
            mpr_sig_set_value(this._obj, 0, 1, (int)Type.Double, (void*)&value);
        }
        unsafe private void _setValue(int[] value) {
            fixed(int* temp = &value[0]) {
                IntPtr intPtr = new IntPtr((void*)temp);
                mpr_sig_set_value(this._obj, 0, value.Length, (int)Type.Int32, (void*)intPtr);
            }
        }
        unsafe private void _setValue(float[] value) {
            fixed(float* temp = &value[0]) {
                IntPtr intPtr = new IntPtr((void*)temp);
                mpr_sig_set_value(this._obj, 0, value.Length, (int)Type.Float, (void*)intPtr);
            }
        }
        unsafe private void _setValue(double[] value) {
            fixed(double* temp = &value[0]) {
                IntPtr intPtr = new IntPtr((void*)temp);
                mpr_sig_set_value(this._obj, 0, value.Length, (int)Type.Double, (void*)intPtr);
            }
        }
        public Signal setValue<T>(T value) {
            dynamic temp = value;
            _setValue(temp);
            return this;
        }

        Action<Signal, Signal.Event, UInt64, int, Type, IntPtr, IntPtr> _handler = null;
    }

    public class Device : Object
    {
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_dev_new([MarshalAs(UnmanagedType.LPStr)] string devname, IntPtr graph);
        public Device([MarshalAs(UnmanagedType.LPStr)] string name)
            { this._obj = mpr_dev_new(name, IntPtr.Zero); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_dev_free(IntPtr dev);
        ~Device()
            { mpr_dev_free(this._obj); }

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
        private static extern IntPtr mpr_sig_new(IntPtr dev, int direction,
                                                 [MarshalAs(UnmanagedType.LPStr)] string name,
                                                 int length, int type,
                                                 [MarshalAs(UnmanagedType.LPStr)] string unit,
                                                 IntPtr min, IntPtr max, IntPtr num_inst,
                                                 Action<Signal, Signal.Event, UInt64, int, Type, IntPtr, IntPtr> handler,
                                                 int events);
        public Signal addSignal(Direction dir, [MarshalAs(UnmanagedType.LPStr)] string name,
                                int length, Type type,
                                [MarshalAs(UnmanagedType.LPStr)] string unit = null,
                                IntPtr min = default(IntPtr), IntPtr max = default(IntPtr),
                                IntPtr num_inst = default(IntPtr),
                                Action<Signal, Signal.Event, UInt64, int, Type, IntPtr, IntPtr> handler = null,
                                int events = 0)
        {
            IntPtr sigptr = mpr_sig_new(this._obj, (int) dir, name, length, (int) type, unit,
                                        min, max, num_inst, null, 0);
            return new Signal(sigptr, handler, events);
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

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        unsafe private static extern IntPtr mpr_map_new(int num_srcs, void* srcs, int num_dsts, void* dsts);
        unsafe public Map(Signal src, Signal dst)
        {
            fixed (void* s = &src._obj) {
                fixed (void* d = &dst._obj) {
                    this._obj = mpr_map_new(1, s, 1, d);
                }
            }
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_map_new_from_str([MarshalAs(UnmanagedType.LPStr)] string expr,
                                                          IntPtr sig0, IntPtr sig1, IntPtr sig2,
                                                          IntPtr sig3, IntPtr sig4, IntPtr sig5,
                                                          IntPtr sig6, IntPtr sig7, IntPtr sig8,
                                                          IntPtr sig9, IntPtr sig10);
        public Map([MarshalAs(UnmanagedType.LPStr)] string expr, params Signal[] signals)
        {
            IntPtr[] a = new IntPtr[10];
            for (int i = 0; i < 10; i++) {
                if (i < signals.Length)
                    a[i] = signals[i]._obj;
                else
                    a[i] = default(IntPtr);
            }
            this._obj = mpr_map_new_from_str(expr, a[0], a[1], a[2], a[3], a[4], a[5],
                                             a[6], a[7], a[8], a[9], default(IntPtr));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_map_get_is_ready(IntPtr map);
        public int getIsReady()
            { return mpr_map_get_is_ready(this._obj); }
    }

    // [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    // private static extern IntPtr mpr_get_version();
    // public string getVersion()
    //     { return System.Runtime.InteropServices.Marshal.PtrToStringAnsi(mpr_get_version()); }
}
