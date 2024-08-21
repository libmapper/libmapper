using System.Runtime.InteropServices;

namespace Mapper;

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
    [Flags]
    public enum StatusFlags
    {
        IsActive = 0x0020,
        HasValue = 0x0040,
        NewValue = 0x0080,
        UpdateLocal = 0x0100,
        UpdateRemote = 0x0200,
        ReleaseUpstream = 0x0400,
        ReleaseDownstream = 0x0400
    }
    
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
        New = 0x0001,

        /// <summary>
        ///     Instance value updated
        /// </summary>
        Update = 0x0200,

        /// <summary>
        ///     Instance was released upstream
        /// </summary>
        UpstreamRelease = 0x0400,

        /// <summary>
        ///     Instance was released downstream
        /// </summary>
        DownstreamRelease = 0x0800,

        /// <summary>
        ///     No local instances left
        /// </summary>
        Overflow = 0x1000,

        /// <summary>
        ///     All events
        /// </summary>
        All = 0x1FFF
    }

    [Flags]
    public enum Status
    {
        /// <summary>
        ///     Instance was newly created since last check
        /// </summary>
        New = 0x0001,

        /// <summary>
        ///     Instance is reserved but not active
        /// </summary>
        Staged = 0x0010,

        /// <summary>
        ///     Instance is active
        /// </summary>
        Active = 0x0020,

        /// <summary>
        ///     Instance has a value
        /// </summary>
        HasValue = 0x0040,

        /// <summary>
        ///     Instance value has changed since last check
        /// </summary>
        NewValue = 0x0080,

        /// <summary>
        ///     Instance was updated locally since last check
        /// </summary>
        LocalUpdate = 0x0100,

        /// <summary>
        ///     Instance was updated remotely since last check
        /// </summary>
        RemoteUpdate = 0x0200,

        /// <summary>
        ///     Instance was released upstream since last check
        /// </summary>
        UpstreamRelease = 0x0400,

        /// <summary>
        ///     Instance was released downstream since last check
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
    
    /// <summary>
    ///     Event handler for when a signal's value changes.
    /// </summary>
    public event EventHandler<(ulong instanceId, object? value, MapperType objectType, Time changed)>? ValueChanged; 
    

    public Signal()
    {
    }

    // construct from mpr_sig pointer
    internal Signal(IntPtr sig) : base(sig)
    {
        unsafe
        { 
            var exists = mpr_obj_get_prop_by_key(sig, "cb_ptr", null, null, null, null) != 0;
            if (!exists)
            {
                var handler = new HandlerDelegate(_handler);
                mpr_sig_set_cb(sig, Marshal.GetFunctionPointerForDelegate(handler), (int) Event.All);
                
                // Create a GCHandle to keep the delegate alive
                var handlePtr = GCHandle.Alloc(handler, GCHandleType.Normal);
                var val = GCHandle.ToIntPtr(handlePtr).ToInt64(); 
                mpr_obj_set_prop(sig, 0, "cb_ptr", 1, (int) MapperType.Int64, &val, 0);
            }
            
        }
    }

    /// <summary>
    ///     The device that this signal belongs to
    /// </summary>
    public Device Device => new(mpr_sig_get_dev(NativePtr));


    /// <summary>
    ///     Get a handle to the oldest active instance of this signal.
    /// </summary>
    public Instance OldestInstance => new(NativePtr, mpr_sig_get_oldest_inst_id(NativePtr));

    /// <summary>
    ///     Get a handle to the newest active instance of this signal.
    /// </summary>
    public Instance GetNewestInstance => new(NativePtr, mpr_sig_get_newest_inst_id(NativePtr));

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
        mpr_sig_set_value(NativePtr, instanceId, 1, (int)MapperType.Int32, &value);
    }

    private unsafe void _SetValue(float value, ulong instanceId)
    {
        mpr_sig_set_value(NativePtr, instanceId, 1, (int)MapperType.Float, &value);
    }

    private unsafe void _SetValue(double value, ulong instanceId)
    {
        mpr_sig_set_value(NativePtr, instanceId, 1, (int)MapperType.Double, &value);
    }

    private unsafe void _SetValue(int[] value, ulong instanceId)
    {
        fixed (int* temp = &value[0])
        {
            var intPtr = new IntPtr(temp);
            mpr_sig_set_value(NativePtr, instanceId, value.Length, (int)MapperType.Int32, (void*)intPtr);
        }
    }

    private unsafe void _SetValue(float[] value, ulong instanceId)
    {
        fixed (float* temp = &value[0])
        {
            var intPtr = new IntPtr(temp);
            mpr_sig_set_value(NativePtr, instanceId, value.Length, (int)MapperType.Float, (void*)intPtr);
        }
    }

    private unsafe void _SetValue(double[] value, ulong instanceId)
    {
        fixed (double* temp = &value[0])
        {
            var intPtr = new IntPtr(temp);
            mpr_sig_set_value(NativePtr, instanceId, value.Length, (int)MapperType.Double, (void*)intPtr);
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
        var len = mpr_obj_get_prop_as_int32(NativePtr, (int)Property.Length, null);
        var type = mpr_obj_get_prop_as_int32(NativePtr, (int)Property.Type, null);
        long time = 0;
        var val = mpr_sig_get_value(NativePtr, instanceId, ref time);
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
    private static extern unsafe int mpr_sig_reserve_inst(IntPtr sig, int num, int* ids, IntPtr data);

    /// <summary>
    ///     Preallocates space to store `number` instances of this signal.
    /// </summary>
    /// <param name="number">Number of instances to preallocate memory for</param>
    /// <returns>This signal for chaining</returns>
    public Signal ReserveInstances(int number = 1)
    {
        unsafe
        {
            mpr_sig_reserve_inst(NativePtr, number, null, IntPtr.Zero);
        }
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_sig_get_inst_status(IntPtr sig, long id = 0);
    
    
    /// <summary>
    /// Gets and then clears status flags attached to this signal.
    /// The returned value can be used to see if the signal has been updated remotely.a
    /// <param name="id">Instance id to get status for, default 0</param>
    /// </summary>
    /// <returns>Status flags</returns>
    public StatusFlags FetchStatus(long id = 0) => (StatusFlags)mpr_sig_get_inst_status(NativePtr, id);

    /// <summary>
    /// Reserve a single instance, with a specific id.
    /// </summary>
    /// <param name="id">Identifier used to refer to this specific instance</param>
    public void ReserveInstance(int id)
    {
        unsafe
        {
            mpr_sig_reserve_inst(NativePtr, 1, &id, IntPtr.Zero);
        }
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
        mpr_sig_remove_inst(NativePtr, instanceId);
        return this;
    }

    /// <summary>
    ///     Get a reference to an instance of this signal.
    /// </summary>
    /// <param name="id">The instance id</param>
    public Instance GetInstance(ulong id)
    {
        return new Instance(NativePtr, id);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern ulong mpr_sig_get_oldest_inst_id(IntPtr sig);

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern ulong mpr_sig_get_newest_inst_id(IntPtr sig);

    private unsafe void _handler(IntPtr sig, int evt, ulong inst, int length,
        int type, IntPtr value, long time)
    {
        var e = (Event)evt;
        var t = new Time(time);
        object? val = BuildValue(length, type, value.ToPointer(), 0);
        ValueChanged?.Invoke(this, (inst, val, (MapperType)type, t));
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
        return new MapperList<Map>(mpr_sig_get_maps(NativePtr, (int)direction), MapperType.Map);
    }

    private delegate void HandlerDelegate(IntPtr sig, int evt, ulong instanceId, int length,
        int type, IntPtr value, long time);

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
            mpr_sig_release_inst(NativePtr, id);
        }
    }
    
}
