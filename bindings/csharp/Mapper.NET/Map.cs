using System.Runtime.InteropServices;

namespace Mapper.NET;

/// <summary>
///     Maps define dataflow connections between sets of signals.
///     A map consists of one or more sources, one destination, and properties which determine how the source data is
///     processed.
/// </summary>
public class Map : MapperObject
{
    public enum Location
    {
        Source = 0x01, //!< Source signal(s) for this map.
        Destination = 0x02, //!< Destination signal(s) for this map.
        Any = 0x03 //!< Either source or destination signals.
    }

    /// <summary>
    ///     How map updates are sent.
    /// </summary>
    public enum Protocol
    {
        /// <summary>
        ///     Updates sent via UDP
        /// </summary>
        Udp,

        /// <summary>
        ///     Updates sent via TCP
        /// </summary>
        Tcp
    }

    public Map()
    {
    }

    /// <summary>
    ///     Create a direct map from one signal to another.
    /// </summary>
    /// <param name="source">Signal producing a value</param>
    /// <param name="destination">Signal consuming the value</param>
    public Map(Signal source, Signal destination)
    {
        unsafe
        {
            fixed (void* s = &source._obj)
            {
                fixed (void* d = &destination._obj)
                {
                    _obj = mpr_map_new(1, s, 1, d);
                }
            }
        }
    }

    /// <summary>
    ///     Create a wrapper around an already existing map.
    /// </summary>
    /// <param name="map">Pointer to already existing map</param>
    internal Map(IntPtr map) : base(map)
    {
    }

    /// <summary>
    ///     Creates a map from a string expression. `%y` is used as the target signal and %x is used for a source.
    /// </summary>
    /// <example>
    ///     <code>
    ///     var map = new Map("%y=%x", signal1, signal2); // identity map
    /// 
    ///     var map2 = new Map("%y=%x*2.0", signal1, signal2); // double the input
    /// </code>
    /// </example>
    /// <param name="expression"></param>
    /// <param name="signals"></param>
    public Map(string expression, params Signal[] signals)
    {
        var a = new IntPtr[10];
        for (var i = 0; i < 10; i++)
            if (i < signals.Length)
                a[i] = signals[i]._obj;
            else
                a[i] = default;
        _obj = mpr_map_new_from_str(expression, a[0], a[1], a[2], a[3], a[4], a[5],
            a[6], a[7], a[8], a[9], default);
    }

    /// <summary>
    ///     If this map has been completely initialized.
    /// </summary>
    public bool IsReady => mpr_map_get_is_ready(_obj) != 0;

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern unsafe IntPtr mpr_map_new(int num_srcs, void* srcs, int num_dsts, void* dsts);

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_map_new_from_str([MarshalAs(UnmanagedType.LPStr)] string expr,
        IntPtr sig0, IntPtr sig1, IntPtr sig2,
        IntPtr sig3, IntPtr sig4, IntPtr sig5,
        IntPtr sig6, IntPtr sig7, IntPtr sig8,
        IntPtr sig9, IntPtr sig10);

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_map_get_is_ready(IntPtr map);

    public new Map SetProperty<TProperty, TValue>(TProperty property, TValue value, bool publish)
    {
        base.SetProperty(property, value, publish);
        return this;
    }

    public new Map SetProperty<TProperty, TValue>(TProperty property, TValue value)
    {
        base.SetProperty(property, value, true);
        return this;
    }

    /// <summary>
    ///     Pushes this map out to the distributed graph, allowing it to become active
    /// </summary>
    /// <returns>The same map to allow for chaining</returns>
    public new Map Push()
    {
        base.Push();
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_map_refresh(IntPtr map);

    /// <summary>
    ///     Re-create stale map if necessary.
    /// </summary>
    /// <returns>The same instance to allow for chaining</returns>
    public Map Refresh()
    {
        mpr_map_refresh(_obj);
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_map_release(IntPtr map);

    /// <summary>
    ///     Release this map, removing it from the distributed graph.
    /// </summary>
    public void Release()
    {
        mpr_map_release(_obj);
        _obj = IntPtr.Zero;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_map_get_sigs(IntPtr map, int loc);

    /// <summary>
    ///     Get a list of signals connected with this map.
    /// </summary>
    /// <param name="location">Filter the returned list by signal location</param>
    /// <returns>A possibly filtered list of signals connected by this map</returns>
    public MapperList<Signal> GetSignals(Location location = Location.Any)
    {
        return new MapperList<Signal>(mpr_map_get_sigs(_obj, (int)location), Type.Signal);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_map_get_sig_idx(IntPtr map, IntPtr sig);

    /// <summary>
    ///     Retrieve the index for a specific map signal.
    /// </summary>
    /// <param name="signal">The signal to get the numerical index of</param>
    /// <returns>Numerical signal index</returns>
    public int GetSignalIndex(Signal signal)
    {
        return mpr_map_get_sig_idx(_obj, signal._obj);
    }
}