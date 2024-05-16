using System.Runtime.InteropServices;

namespace Mapper.NET;

public class Signal : MapperObject
    {
        public Signal() {}

        private delegate void HandlerDelegate(IntPtr sig, int evt, UInt64 instanceId, int length,
                                              int type, IntPtr value, long time);

        public enum Direction
        {
            /// <summary>
            /// Signal is an input
            /// </summary>
            Incoming    = 0x01,
            /// <summary>
            /// Signal is an output
            /// </summary>
            Outgoing    = 0x02,
            /// <summary>
            /// Signal is either an input or an output
            /// </summary>
            Any         = 0x03,
            /// <summary>
            /// Currently not possible to have a signal that is both an input and an output.
            /// This value is only used for querying device maps
            ///   that touch only local signals.
            /// </summary>
            [Obsolete("You likely want to use 'Any' instead of 'Both'")]
            Both        = 0x04  
        }


        [Flags]
        public enum Event
        {
            /// <summary>
            /// New instance created
            /// </summary>
            New                 = 0x01,
            /// <summary>
            /// Instance was released upstream
            /// </summary>
            UpstreamRelease     = 0x02,
            /// <summary>
            /// Instance was released downstream
            /// </summary>
            DownstreamRelease   = 0x04,
            /// <summary>
            /// No local instances left
            /// </summary>
            Overflow            = 0x08,
            /// <summary>
            ///  Instance value updated
            /// </summary>
            Update              = 0x10,
            /// <summary>
            /// All events
            /// </summary>
            All                 = 0x1F
        }

        public enum Stealing
        {
            /// <summary>
            /// No stealing at all
            /// </summary>
            None,
            /// <summary>
            /// Steal the oldest instance
            /// </summary>
            Oldest,
            /// <summary>
            /// Steal the newest instance
            /// </summary>
            Newest
        }

        private enum HandlerType
        {
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

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_sig_get_dev(IntPtr sig);
        public Device GetDevice()
        {
            return new Device(mpr_sig_get_dev(_obj));
        }

        public override string ToString()
            => $"Mapper.Signal:{GetDevice().GetProperty(Property.Name)}:{GetProperty(Property.Name)}";

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_sig_set_cb(IntPtr sig, IntPtr handler, int events);

        // construct from mpr_sig pointer
        internal Signal(IntPtr sig) : base(sig) {}
        

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern unsafe void mpr_sig_set_value(IntPtr sig, UInt64 id, int len, int type, void* val);
        private unsafe void _SetValue(int value, UInt64 instanceId)
        {
            mpr_sig_set_value(_obj, instanceId, 1, (int)Type.Int32, &value);
        }
        private unsafe void _SetValue(float value, UInt64 instanceId)
        {
            mpr_sig_set_value(_obj, instanceId, 1, (int)Type.Float, &value);
        }
        private unsafe void _SetValue(double value, UInt64 instanceId)
        {
            mpr_sig_set_value(_obj, instanceId, 1, (int)Type.Double, &value);
        }
        private unsafe void _SetValue(int[] value, UInt64 instanceId)
        {
            fixed(int* temp = &value[0])
            {
                IntPtr intPtr = new IntPtr(temp);
                mpr_sig_set_value(_obj, instanceId, value.Length, (int)Type.Int32, (void*)intPtr);
            }
        }
        private unsafe void _SetValue(float[] value, UInt64 instanceId)
        {
            fixed(float* temp = &value[0])
            {
                IntPtr intPtr = new IntPtr(temp);
                mpr_sig_set_value(_obj, instanceId, value.Length, (int)Type.Float, (void*)intPtr);
            }
        }
        private unsafe void _SetValue(double[] value, UInt64 instanceId)
        {
            fixed(double* temp = &value[0])
            {
                IntPtr intPtr = new IntPtr(temp);
                mpr_sig_set_value(_obj, instanceId, value.Length, (int)Type.Double, (void*)intPtr);
            }
        }

        public Signal SetValue<T>(T value, UInt64 instanceId = 0)
        {
            dynamic temp = value;
            _SetValue(temp, instanceId);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern unsafe void* mpr_sig_get_value(IntPtr sig, UInt64 id, ref long time);
        public unsafe (dynamic, Time) GetValue(UInt64 instanceId = 0)
        {
            int len = mpr_obj_get_prop_as_int32(_obj, (int)Property.Length, null);
            int type = mpr_obj_get_prop_as_int32(_obj, (int)Property.Type, null);
            long time = 0;
            void *val = mpr_sig_get_value(_obj, instanceId, ref time);
            return (BuildValue(len, type, val, 0), new Time(time));
        }
        // unsafe public dynamic GetValue(UInt64 instanceId = 0)
        // {
        //     int len = mpr_obj_get_prop_as_int32(this._obj, (int)Property.Length, null);
        //     int type = mpr_obj_get_prop_as_int32(this._obj, (int)Property.Type, null);
        //     void *val = mpr_sig_get_value(this._obj, instanceId, IntPtr.Zero);
        //     return BuildValue(len, type, val, 0);
        // }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_sig_reserve_inst(IntPtr sig, int num, IntPtr ids, IntPtr data);
        public Signal ReserveInstances(int number)
        {
            mpr_sig_reserve_inst(_obj, number, IntPtr.Zero, IntPtr.Zero);
            return this;
        }
        public Signal ReserveInstance()
        {
            mpr_sig_reserve_inst(_obj, 1, IntPtr.Zero, IntPtr.Zero);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_sig_remove_inst(IntPtr sig, UInt64 id);
        public Signal RemoveInstance(UInt64 instanceId)
        {
            mpr_sig_remove_inst(_obj, instanceId);
            return this;
        }

        public class Instance : Signal
        {
            internal Instance(IntPtr sig, UInt64 inst) : base(sig)
            {
                id = inst;
            }

            public (dynamic, Time) GetValue()
            {
                return GetValue(id);
            }

            public Instance SetValue<T>(T value)
            {
                dynamic temp = value;
                _SetValue(temp, id);
                return this;
            }

            [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
            private static extern void mpr_sig_release_inst(IntPtr sig, UInt64 id);
            public void Release()
            {
                mpr_sig_release_inst(_obj, id);
            }

            public readonly UInt64 id;
        }

        public Instance GetInstance(UInt64 id)
        {
            return new Instance(_obj, id);
        }
        public Instance GetInstance(int id)
        {
            return new Instance(_obj, (UInt64)id);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern ulong mpr_sig_get_oldest_inst_id(IntPtr sig);
        public Instance GetOldestInstance()
        {
            return new Instance(_obj, mpr_sig_get_oldest_inst_id(_obj));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern ulong mpr_sig_get_newest_inst_id(IntPtr sig);
        public Instance GetNewestInstance()
        {
            return new Instance(_obj, mpr_sig_get_newest_inst_id(_obj));
        }

        private void _handler(IntPtr sig, int evt, UInt64 inst, int length,
                              int type, IntPtr value, long time)
        {
            Event e = (Event)evt;
            Time t = new Time(time);
            switch (handlerType)
            {
                case HandlerType.SingletonInt:
                    unsafe
                    {
                        int ivalue = 0;
                        if (value != IntPtr.Zero)
                            ivalue = *(int*)value;
                        handlers.singletonInt(new Signal(sig), e, ivalue, t);
                    }
                    break;
                case HandlerType.SingletonFloat:
                    unsafe
                    {
                        float fvalue = 0;
                        if (value != IntPtr.Zero)
                            fvalue = *(float*)value;
                        handlers.singletonFloat(new Signal(sig), e, fvalue, t);
                    }
                    break;
                case HandlerType.SingletonDouble:
                    unsafe
                    {
                        double dvalue = 0;
                        if (value != IntPtr.Zero)
                            dvalue = *(double*)value;
                        handlers.singletonDouble(new Signal(sig), e, dvalue, t);
                    }
                    break;
                case HandlerType.SingletonIntVector:
                    if (value == IntPtr.Zero)
                        handlers.singletonIntVector(new Signal(sig), e, new int[0], t);
                {
                    int[] arr = new int[length];
                    Marshal.Copy(value, arr, 0, length);
                    handlers.singletonIntVector(new Signal(sig), e, arr, t);
                }
                    break;
                case HandlerType.SingletonFloatVector:
                    if (value == IntPtr.Zero)
                        handlers.singletonFloatVector(new Signal(sig), e, new float[0], t);
                {
                    float[] arr = new float[length];
                    Marshal.Copy(value, arr, 0, length);
                    handlers.singletonFloatVector(new Signal(sig), e, arr, t);
                }
                    break;
                case HandlerType.SingletonDoubleVector:
                    if (value == IntPtr.Zero)
                        handlers.singletonDoubleVector(new Signal(sig), e, new double[0], t);
                {
                    double[] arr = new double[length];
                    Marshal.Copy(value, arr, 0, length);
                    handlers.singletonDoubleVector(new Signal(sig), e, arr, t);
                }
                    break;
                case HandlerType.InstancedInt:
                    unsafe
                    {
                        int ivalue = 0;
                        if (value != IntPtr.Zero)
                            ivalue = *(int*)value;
                        handlers.instancedInt(new Instance(sig, inst), e, ivalue, t);
                    }
                    break;
                case HandlerType.InstancedFloat:
                    unsafe
                    {
                        float fvalue = 0;
                        if (value != IntPtr.Zero)
                            fvalue = *(float*)value;
                        handlers.instancedFloat(new Instance(sig, inst), e, fvalue, t);
                    }
                    break;
                case HandlerType.InstancedDouble:
                    unsafe
                    {
                        double dvalue = 0;
                        if (value != IntPtr.Zero)
                            dvalue = *(double*)value;
                        handlers.instancedDouble(new Instance(sig, inst), e, dvalue, t);
                    }
                    break;
                case HandlerType.InstancedIntVector:
                    if (value == IntPtr.Zero)
                        handlers.instancedIntVector(new Instance(sig, inst), e, new int[0], t);
                {
                    int[] arr = new int[length];
                    Marshal.Copy(value, arr, 0, length);
                    handlers.instancedIntVector(new Instance(sig, inst), e, arr, t);
                }
                    break;
                case HandlerType.InstancedFloatVector:
                    if (value == IntPtr.Zero)
                        handlers.instancedFloatVector(new Instance(sig, inst), e, new float[0], t);
                {
                    float[] arr = new float[length];
                    Marshal.Copy(value, arr, 0, length);
                    handlers.instancedFloatVector(new Instance(sig, inst), e, arr, t);
                }
                    break;
                case HandlerType.InstancedDoubleVector:
                    if (value == IntPtr.Zero)
                        handlers.instancedDoubleVector(new Instance(sig, inst), e, new double[0], t);
                {
                    double[] arr = new double[length];
                    Marshal.Copy(value, arr, 0, length);
                    handlers.instancedDoubleVector(new Instance(sig, inst), e, arr, t);
                }
                    break;
            }
        }

        ~Signal()
            {}

        private Boolean _SetCallback(Action<Signal, Event, int, Time> h, int type)
        {
            if (type != (int)Type.Int32)
                return false;
            handlerType = HandlerType.SingletonInt;
            handlers.singletonInt = h;
            return true;
        }

        private Boolean _SetCallback(Action<Signal, Event, int[], Time> h, int type)
        {
            if (type != (int)Type.Int32)
                return false;
            handlerType = HandlerType.SingletonIntVector;
            handlers.singletonIntVector = h;
            return true;
        }

        private Boolean _SetCallback(Action<Signal, Event, float, Time> h, int type)
        {
            if (type != (int)Type.Float)
                return false;
            handlerType = HandlerType.SingletonFloat;
            handlers.singletonFloat = h;
            return true;
        }

        private Boolean _SetCallback(Action<Signal, Event, float[], Time> h, int type)
        {
            if (type != (int)Type.Float)
                return false;
            handlerType = HandlerType.SingletonFloatVector;
            handlers.singletonFloatVector = h;
            return true;
        }

        private Boolean _SetCallback(Action<Signal, Event, double, Time> h, int type)
        {
            if (type != (int)Type.Double)
                return false;
            handlerType = HandlerType.SingletonDouble;
            handlers.singletonDouble = h;
            return true;
        }

        private Boolean _SetCallback(Action<Signal, Event, double[], Time> h, int type)
        {
            if (type != (int)Type.Double)
                return false;
            handlerType = HandlerType.SingletonDoubleVector;
            handlers.singletonDoubleVector = h;
            return true;
        }

        private Boolean _SetCallback(Action<Instance, Event, int, Time> h, int type)
        {
            if (type != (int)Type.Int32)
                return false;
            handlerType = HandlerType.InstancedInt;
            handlers.instancedInt = h;
            return true;
        }

        private Boolean _SetCallback(Action<Instance, Event, int[], Time> h, int type)
        {
            if (type != (int)Type.Int32)
                return false;
            handlerType = HandlerType.InstancedIntVector;
            handlers.instancedIntVector = h;
            return true;
        }

        private Boolean _SetCallback(Action<Instance, Event, float, Time> h, int type)
        {
            if (type != (int)Type.Float)
                return false;
            handlerType = HandlerType.InstancedFloat;
            handlers.instancedFloat = h;
            return true;
        }

        private Boolean _SetCallback(Action<Instance, Event, float[], Time> h, int type)
        {
            if (type != (int)Type.Float)
                return false;
            handlerType = HandlerType.InstancedFloatVector;
            handlers.instancedFloatVector = h;
            return true;
        }

        private Boolean _SetCallback(Action<Instance, Event, double, Time> h, int type)
        {
            if (type != (int)Type.Double)
                return false;
            handlerType = HandlerType.InstancedDouble;
            handlers.instancedDouble = h;
            return true;
        }

        private Boolean _SetCallback(Action<Instance, Event, double[], Time> h, int type)
        {
            if (type != (int)Type.Double)
                return false;
            handlerType = HandlerType.InstancedDoubleVector;
            handlers.instancedDoubleVector = h;
            return true;
        }

        public Signal SetCallback<T>(T handler, Event events = Event.All)
        {
            dynamic temp = handler;
            int type = mpr_obj_get_prop_as_int32(_obj, (int)Property.Type, null);
            if (!_SetCallback(temp, type))
            {
                Console.WriteLine("error: wrong data type in signal handler.");
                return this;
            }
            mpr_sig_set_cb(_obj,
                           Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(_handler)),
                           (int)events);
            return this;
        }

        public new Signal SetProperty<P, T>(P property, T value, bool publish)
        {
            base.SetProperty(property, value, publish);
            return this;
        }

        public new Signal SetProperty<P, T>(P property, T value)
        {
            base.SetProperty(property, value, true);
            return this;
        }

        public new Signal Push()
        {
            base.Push();
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_sig_get_maps(IntPtr sig, int dir);
        public MapperList<Map> GetMaps(Direction direction = Direction.Any)
        {
            return new MapperList<Map>(mpr_sig_get_maps(_obj, (int)direction), Type.Map);
        }

        [StructLayout(LayoutKind.Explicit)]
        struct Handlers
        {
            [FieldOffset(0)]
            internal Action<Signal, Event, int, Time> singletonInt;
            [FieldOffset(0)]
            internal Action<Signal, Event, float, Time> singletonFloat;
            [FieldOffset(0)]
            internal Action<Signal, Event, double, Time> singletonDouble;
            [FieldOffset(0)]
            internal Action<Signal, Event, int[], Time> singletonIntVector;
            [FieldOffset(0)]
            internal Action<Signal, Event, float[], Time> singletonFloatVector;
            [FieldOffset(0)]
            internal Action<Signal, Event, double[], Time> singletonDoubleVector;
            [FieldOffset(0)]
            internal Action<Instance, Event, int, Time> instancedInt;
            [FieldOffset(0)]
            internal Action<Instance, Event, float, Time> instancedFloat;
            [FieldOffset(0)]
            internal Action<Instance, Event, double, Time> instancedDouble;
            [FieldOffset(0)]
            internal Action<Instance, Event, int[], Time> instancedIntVector;
            [FieldOffset(0)]
            internal Action<Instance, Event, float[], Time> instancedFloatVector;
            [FieldOffset(0)]
            internal Action<Instance, Event, double[], Time> instancedDoubleVector;
        }
        private Handlers handlers;
        private HandlerType handlerType = HandlerType.None;
    }