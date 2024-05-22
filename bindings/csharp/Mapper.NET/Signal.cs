using System.Runtime.InteropServices;

namespace Mapper.NET;

/// <summary>
///     Signals define inputs or outputs for devices. A signal consists of a scalar or vector value of some integer or
///     floating-point type.
///     A signal must be associated with a parent device. It can optionally be provided with some metadata such as range,
///     unit, or other properties.
///     Signals can be dynamically connected together in a dataflow graph by creating Maps using the libmapper API or an
///     external session manager.
/// </summary>
public class Signal : MapperObject
{
    public enum Direction
    {
        /// <summary>
        ///     Signal is an input
        /// </summary>
        Incoming = 0x01,

        /// <summary>
        ///     Signal is an output
        /// </summary>
        Outgoing = 0x02,

        /// <summary>
        ///     Signal is either an input or an output
        /// </summary>
        Any = 0x03,

        /// <summary>
        ///     Currently not possible to have a signal that is both an input and an output.
        ///     This value is only used for querying device maps
        ///     that touch only local signals.
        /// </summary>
        [Obsolete("You likely want to use 'Any' instead of 'Both'")]
        Both = 0x04
    }


    [Flags]
    public enum Event
    {
        /// <summary>
        ///     New instance created
        /// </summary>
        New = 0x01,

        /// <summary>
        ///     Instance was released upstream
        /// </summary>
        UpstreamRelease = 0x02,

        /// <summary>
        ///     Instance was released downstream
        /// </summary>
        DownstreamRelease = 0x04,

        /// <summary>
        ///     No local instances left
        /// </summary>
        Overflow = 0x08,

        /// <summary>
        ///     Instance value updated
        /// </summary>
        Update = 0x10,

        /// <summary>
        ///     All events
        /// </summary>
        All = 0x1F
    }

    public enum Stealing
    {
        /// <summary>
        ///     No stealing at all
        /// </summary>
        None,

        /// <summary>
        ///     Steal the oldest instance
        /// </summary>
        Oldest,

        /// <summary>
        ///     Steal the newest instance
        /// </summary>
        Newest
    }

    private Handlers handlers;
    private HandlerType handlerType = HandlerType.None;

    public Signal()
    {
    }

    // construct from mpr_sig pointer
    internal Signal(IntPtr sig) : base(sig)
    {
    }

    /// <summary>
    ///     The device that this signal belongs to
    /// </summary>
    public Device Device => new(mpr_sig_get_dev(_obj));


    /// <summary>
    ///     Get a handle to the oldest active instance of this signal.
    /// </summary>
    public Instance OldestInstance => new(_obj, mpr_sig_get_oldest_inst_id(_obj));

    /// <summary>
    ///     Get a handle to the newest active instance of this signal.
    /// </summary>
    public Instance GetNewestInstance => new(_obj, mpr_sig_get_newest_inst_id(_obj));

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_sig_get_dev(IntPtr sig);

    public override string ToString()
    {
        return $"Mapper.Signal:{Device.GetProperty(Property.Name)}:{GetProperty(Property.Name)}";
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_sig_set_cb(IntPtr sig, IntPtr handler, int events);


    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern unsafe void mpr_sig_set_value(IntPtr sig, ulong id, int len, int type, void* val);

    private unsafe void _SetValue(int value, ulong instanceId)
    {
        mpr_sig_set_value(_obj, instanceId, 1, (int)Type.Int32, &value);
    }

    private unsafe void _SetValue(float value, ulong instanceId)
    {
        mpr_sig_set_value(_obj, instanceId, 1, (int)Type.Float, &value);
    }

    private unsafe void _SetValue(double value, ulong instanceId)
    {
        mpr_sig_set_value(_obj, instanceId, 1, (int)Type.Double, &value);
    }

    private unsafe void _SetValue(int[] value, ulong instanceId)
    {
        fixed (int* temp = &value[0])
        {
            var intPtr = new IntPtr(temp);
            mpr_sig_set_value(_obj, instanceId, value.Length, (int)Type.Int32, (void*)intPtr);
        }
    }

    private unsafe void _SetValue(float[] value, ulong instanceId)
    {
        fixed (float* temp = &value[0])
        {
            var intPtr = new IntPtr(temp);
            mpr_sig_set_value(_obj, instanceId, value.Length, (int)Type.Float, (void*)intPtr);
        }
    }

    private unsafe void _SetValue(double[] value, ulong instanceId)
    {
        fixed (double* temp = &value[0])
        {
            var intPtr = new IntPtr(temp);
            mpr_sig_set_value(_obj, instanceId, value.Length, (int)Type.Double, (void*)intPtr);
        }
    }

    /// <summary>
    ///     Sets the value of the signal. The value can be an int, float, double, int[], float[], or double[].
    /// </summary>
    /// <param name="value">Value to push to the distributed graph</param>
    /// <param name="instanceId">Optional parameter indicating which instance to write the value to</param>
    /// <returns>The same signal for chaining</returns>
    /// <exception cref="ArgumentException">
    ///     If the passed value is not a <see cref="float" />, <see cref="double" />, <see cref="int" />, or an array of those.
    /// </exception>
    public Signal SetValue<T>(T value, ulong instanceId = 0) where T : notnull
    {
        if (value is int i)
            _SetValue(i, instanceId);
        else if (value is float f)
            _SetValue(f, instanceId);
        else if (value is double d)
            _SetValue(d, instanceId);
        else if (value is int[] ia)
            _SetValue(ia, instanceId);
        else if (value is float[] fa)
            _SetValue(fa, instanceId);
        else if (value is double[] da)
            _SetValue(da, instanceId);
        else
            throw new ArgumentException("Unsupported type passed to SetValue");
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern unsafe void* mpr_sig_get_value(IntPtr sig, ulong id, ref long time);

    /// <summary>
    ///     Gets the current value of the signal, set either by the user or by the network.
    /// </summary>
    /// <param name="instanceId">An optional parameter indicating which instance of this signal should be read from.</param>
    /// <returns>A tuple of the value and what time it was last updated at. The value might be null.</returns>
    public unsafe (object?, Time) GetValue(ulong instanceId = 0)
    {
        var len = mpr_obj_get_prop_as_int32(_obj, (int)Property.Length, null);
        var type = mpr_obj_get_prop_as_int32(_obj, (int)Property.Type, null);
        long time = 0;
        var val = mpr_sig_get_value(_obj, instanceId, ref time);
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

    /// <summary>
    ///     Preallocates space to store `number` instances of this signal.
    /// </summary>
    /// <param name="number">Number of instances to preallocate memory for</param>
    /// <returns>This signal for chaining</returns>
    public Signal ReserveInstances(int number = 1)
    {
        mpr_sig_reserve_inst(_obj, number, IntPtr.Zero, IntPtr.Zero);
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_sig_remove_inst(IntPtr sig, ulong id);

    /// <summary>
    ///     Remove an instance from the signal.
    /// </summary>
    /// <param name="instanceId">Instance to be released</param>
    /// <returns>The same signal for chaining</returns>
    public Signal RemoveInstance(ulong instanceId)
    {
        mpr_sig_remove_inst(_obj, instanceId);
        return this;
    }

    /// <summary>
    ///     Get a reference to an instance of this signal.
    /// </summary>
    /// <param name="id">The instance id</param>
    public Instance GetInstance(ulong id)
    {
        return new Instance(_obj, id);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern ulong mpr_sig_get_oldest_inst_id(IntPtr sig);

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern ulong mpr_sig_get_newest_inst_id(IntPtr sig);

    private void _handler(IntPtr sig, int evt, ulong inst, int length,
        int type, IntPtr value, long time)
    {
        var e = (Event)evt;
        var t = new Time(time);
        switch (handlerType)
        {
            case HandlerType.SingletonInt:
                unsafe
                {
                    var ivalue = 0;
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
                var arr = new int[length];
                Marshal.Copy(value, arr, 0, length);
                handlers.singletonIntVector(new Signal(sig), e, arr, t);
            }
                break;
            case HandlerType.SingletonFloatVector:
                if (value == IntPtr.Zero)
                    handlers.singletonFloatVector(new Signal(sig), e, new float[0], t);
            {
                var arr = new float[length];
                Marshal.Copy(value, arr, 0, length);
                handlers.singletonFloatVector(new Signal(sig), e, arr, t);
            }
                break;
            case HandlerType.SingletonDoubleVector:
                if (value == IntPtr.Zero)
                    handlers.singletonDoubleVector(new Signal(sig), e, new double[0], t);
            {
                var arr = new double[length];
                Marshal.Copy(value, arr, 0, length);
                handlers.singletonDoubleVector(new Signal(sig), e, arr, t);
            }
                break;
            case HandlerType.InstancedInt:
                unsafe
                {
                    var ivalue = 0;
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
                var arr = new int[length];
                Marshal.Copy(value, arr, 0, length);
                handlers.instancedIntVector(new Instance(sig, inst), e, arr, t);
            }
                break;
            case HandlerType.InstancedFloatVector:
                if (value == IntPtr.Zero)
                    handlers.instancedFloatVector(new Instance(sig, inst), e, new float[0], t);
            {
                var arr = new float[length];
                Marshal.Copy(value, arr, 0, length);
                handlers.instancedFloatVector(new Instance(sig, inst), e, arr, t);
            }
                break;
            case HandlerType.InstancedDoubleVector:
                if (value == IntPtr.Zero)
                    handlers.instancedDoubleVector(new Instance(sig, inst), e, new double[0], t);
            {
                var arr = new double[length];
                Marshal.Copy(value, arr, 0, length);
                handlers.instancedDoubleVector(new Instance(sig, inst), e, arr, t);
            }
                break;
        }
    }

    private bool _SetCallback(Action<Signal, Event, int, Time> h, int type)
    {
        if (type != (int)Type.Int32)
            return false;
        handlerType = HandlerType.SingletonInt;
        handlers.singletonInt = h;
        return true;
    }

    private bool _SetCallback(Action<Signal, Event, int[], Time> h, int type)
    {
        if (type != (int)Type.Int32)
            return false;
        handlerType = HandlerType.SingletonIntVector;
        handlers.singletonIntVector = h;
        return true;
    }

    private bool _SetCallback(Action<Signal, Event, float, Time> h, int type)
    {
        if (type != (int)Type.Float)
            return false;
        handlerType = HandlerType.SingletonFloat;
        handlers.singletonFloat = h;
        return true;
    }

    private bool _SetCallback(Action<Signal, Event, float[], Time> h, int type)
    {
        if (type != (int)Type.Float)
            return false;
        handlerType = HandlerType.SingletonFloatVector;
        handlers.singletonFloatVector = h;
        return true;
    }

    private bool _SetCallback(Action<Signal, Event, double, Time> h, int type)
    {
        if (type != (int)Type.Double)
            return false;
        handlerType = HandlerType.SingletonDouble;
        handlers.singletonDouble = h;
        return true;
    }

    private bool _SetCallback(Action<Signal, Event, double[], Time> h, int type)
    {
        if (type != (int)Type.Double)
            return false;
        handlerType = HandlerType.SingletonDoubleVector;
        handlers.singletonDoubleVector = h;
        return true;
    }

    private bool _SetCallback(Action<Instance, Event, int, Time> h, int type)
    {
        if (type != (int)Type.Int32)
            return false;
        handlerType = HandlerType.InstancedInt;
        handlers.instancedInt = h;
        return true;
    }

    private bool _SetCallback(Action<Instance, Event, int[], Time> h, int type)
    {
        if (type != (int)Type.Int32)
            return false;
        handlerType = HandlerType.InstancedIntVector;
        handlers.instancedIntVector = h;
        return true;
    }

    private bool _SetCallback(Action<Instance, Event, float, Time> h, int type)
    {
        if (type != (int)Type.Float)
            return false;
        handlerType = HandlerType.InstancedFloat;
        handlers.instancedFloat = h;
        return true;
    }

    private bool _SetCallback(Action<Instance, Event, float[], Time> h, int type)
    {
        if (type != (int)Type.Float)
            return false;
        handlerType = HandlerType.InstancedFloatVector;
        handlers.instancedFloatVector = h;
        return true;
    }

    private bool _SetCallback(Action<Instance, Event, double, Time> h, int type)
    {
        if (type != (int)Type.Double)
            return false;
        handlerType = HandlerType.InstancedDouble;
        handlers.instancedDouble = h;
        return true;
    }

    private bool _SetCallback(Action<Instance, Event, double[], Time> h, int type)
    {
        if (type != (int)Type.Double)
            return false;
        handlerType = HandlerType.InstancedDoubleVector;
        handlers.instancedDoubleVector = h;
        return true;
    }

    public Signal SetCallback<T>(T handler, Event events = Event.All) where T : notnull
    {
        // TODO: smite with fire
        dynamic temp = handler;
        var type = mpr_obj_get_prop_as_int32(_obj, (int)Property.Type, null);
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

    public new Signal SetProperty<TProperty, TValue>(TProperty property, TValue value, bool publish)
    {
        base.SetProperty(property, value, publish);
        return this;
    }

    public new Signal SetProperty<TProperty, TValue>(TProperty property, TValue value)
    {
        base.SetProperty(property, value, true);
        return this;
    }

    /// <summary>
    ///     Push this signal out to the distributed graph, allowing it to become active
    /// </summary>
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

    private delegate void HandlerDelegate(IntPtr sig, int evt, ulong instanceId, int length,
        int type, IntPtr value, long time);

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

    /// <summary>
    ///     A variant of Signal that is bound to a specific instance ID.
    /// </summary>
    public class Instance : Signal
    {
        public readonly ulong id;

        internal Instance(IntPtr sig, ulong inst) : base(sig)
        {
            id = inst;
        }

        public (object?, Time) GetValue()
        {
            return GetValue(id);
        }

        public Instance SetValue<T>(T value) where T : notnull
        {
            SetValue(value, id);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_sig_release_inst(IntPtr sig, ulong id);

        /// <summary>
        ///     Release this instance, keeping the allocated memory allowing a new instance to take it's place.
        /// </summary>
        public void Release()
        {
            mpr_sig_release_inst(_obj, id);
        }
    }

    // TODO: This is HORRIFYING
    [StructLayout(LayoutKind.Explicit)]
    private struct Handlers
    {
        [FieldOffset(0)] internal Action<Signal, Event, int, Time> singletonInt;
        [FieldOffset(0)] internal Action<Signal, Event, float, Time> singletonFloat;
        [FieldOffset(0)] internal Action<Signal, Event, double, Time> singletonDouble;
        [FieldOffset(0)] internal Action<Signal, Event, int[], Time> singletonIntVector;
        [FieldOffset(0)] internal Action<Signal, Event, float[], Time> singletonFloatVector;
        [FieldOffset(0)] internal Action<Signal, Event, double[], Time> singletonDoubleVector;
        [FieldOffset(0)] internal Action<Instance, Event, int, Time> instancedInt;
        [FieldOffset(0)] internal Action<Instance, Event, float, Time> instancedFloat;
        [FieldOffset(0)] internal Action<Instance, Event, double, Time> instancedDouble;
        [FieldOffset(0)] internal Action<Instance, Event, int[], Time> instancedIntVector;
        [FieldOffset(0)] internal Action<Instance, Event, float[], Time> instancedFloatVector;
        [FieldOffset(0)] internal Action<Instance, Event, double[], Time> instancedDoubleVector;
    }
}