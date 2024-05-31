using System.Runtime.InteropServices;

namespace Mapper;

/// <summary>
/// Graphs are the primary interface through which a program may observe the distributed
/// graph and store information about devices and signals that are present.
/// Each Graph stores records of devices, signals, and maps, which can be queried.
/// </summary>
public class Graph : MapperObject
{
    /// <summary>
    /// Events fired by Graphs
    /// </summary>
    public enum Event
    {
        /// <summary>
        /// New record has been added to the graph
        /// </summary>
        New,
        /// <summary>
        /// An existing record has been modified
        /// </summary>
        Modified,
        /// <summary>
        /// An existing record has been removed
        /// </summary>
        Removed,
        /// <summary>
        /// The graph has lost contact with the remote object
        /// </summary>
        Expired
    }

    private readonly List<Handler> handlers = new();

    internal Graph(IntPtr obj) : base(obj)
    {
    }

    /// <summary>
    /// Create a new graph, specifying the types of objects to automatically subscribe to.
    /// </summary>
    /// <param name="autosubscribe_types">A combination of <see cref="MapperType" /> values indicating what this graph should automatically subscribe to</param>
    public Graph(MapperType autosubscribe_types) : base(mpr_graph_new((int)autosubscribe_types))
    {
        _owned = true;
    }

    /// <summary>
    /// Create a new graph that will automatically subscribe to all object types.
    /// </summary>
    public Graph() : base(mpr_graph_new((int)MapperType.Object))
    {
        _owned = true;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_graph_new(int flags);

    // TODO: check if Graph is used by a Device
    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_graph_free(IntPtr graph);

    ~Graph()
    {
        if (_owned) mpr_graph_free(_obj);
    }

    public new Graph SetProperty<TProperty, TValue>(TProperty property, TValue value, bool publish)
    {
        base.SetProperty(property, value, publish);
        return this;
    }

    public new Graph SetProperty<TProperty, TValue>(TProperty property, TValue value)
    {
        base.SetProperty(property, value, true);
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_graph_set_interface(IntPtr graph,
        [MarshalAs(UnmanagedType.LPStr)] string iface);

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_graph_get_interface(IntPtr graph);

    /// <summary>
    ///     Network interface the graph is attached to.
    /// </summary>
    public string? Interface
    {
        get => Marshal.PtrToStringAnsi(mpr_graph_get_interface(_obj));
        set
        {
            if (value == null)
            {
                throw new ArgumentException("Cannot set interface to null.");
            }
            mpr_graph_set_interface(_obj, value);
        }
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_graph_set_address(IntPtr graph,
        [MarshalAs(UnmanagedType.LPStr)] string group,
        int port);

    /// <summary>
    /// Set the multicast group and port to use
    /// </summary>
    /// <param name="group">Multicast group</param>
    /// <param name="port">Port to use for communication</param>
    /// <returns>True if successful, false otherwise</returns>
    public bool SetAddress(string group, int port)
    {
        return mpr_graph_set_address(_obj, group, port) == 0;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_graph_get_address(IntPtr graph);
    
    /// <summary>
    /// A string specifying the multicast URL for bus communication with the distributed graph.
    /// </summary>
    public string? Address => Marshal.PtrToStringAnsi(mpr_graph_get_address(_obj));

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern int mpr_graph_poll(IntPtr graph, int block_ms);

    /// <summary>
    /// Synchronize a local graph copy with the distributed graph.
    /// </summary>
    /// <param name="block_ms">Optionally block for this many milliseconds while polling. A value of 0 will not block.</param>
    /// <returns>Number of handled messages</returns>
    public int Poll(int block_ms = 0)
    {
        return mpr_graph_poll(_obj, block_ms);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_graph_subscribe(IntPtr graph, IntPtr dev, int types, int timeout);

    /// <summary>
    /// Subscribe to receive events for a specific device
    /// </summary>
    /// <param name="device">Device to receive events from</param>
    /// <param name="mapperTypes">Types of information to raise events for</param>
    /// <param name="timeout">Length of time in seconds to subscribe for. -1 is indefinite.</param>
    public void Subscribe(Device device, MapperType mapperTypes, int timeout = -1)
    {
        mpr_graph_subscribe(_obj, device._obj, (int)mapperTypes, timeout);
    }

    /// <summary>
    /// Subscribe to receive events for the entire graph
    /// </summary>
    /// <param name="mapperTypes">Types of information to raise events for</param>
    /// <param name="timeout">Length of time in seconds to subscribe for. -1 is indefinite.</param>
    public void Subscribe(MapperType mapperTypes, int timeout = -1)
    {
        mpr_graph_subscribe(_obj, IntPtr.Zero, (int)mapperTypes, timeout);
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_graph_unsubscribe(IntPtr graph, IntPtr dev);

    /// <summary>
    /// Unsubscribe from events for a specific device
    /// </summary>
    /// <param name="device"></param>
    /// <returns></returns>
    public Graph Unsubscribe(Device device)
    {
        mpr_graph_unsubscribe(_obj, device._obj);
        return this;
    }

    /// <summary>
    /// Unsubscribe from events for the entire graph
    /// </summary>
    /// <returns></returns>
    public Graph Unsubscribe()
    {
        mpr_graph_unsubscribe(_obj, IntPtr.Zero);
        return this;
    }

    private void _handler(IntPtr graph, IntPtr obj, int evt, IntPtr data)
    {
        var type = (MapperType)mpr_obj_get_type(obj);
        // Event event = (Event) evt;
        object o;
        var e = (Event)evt;
        switch (type)
        {
            case MapperType.Device:
                o = new Device(obj);
                break;
            case MapperType.Signal:
                o = new Signal(obj);
                break;
            case MapperType.Map:
                o = new Map(obj);
                break;
            default:
                return;
        }

        handlers.ForEach(delegate(Handler h)
        {
            if ((h.MapperTypes & type) != 0)
                h._callback(o, e);
        });
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_graph_add_cb(IntPtr graph, IntPtr handler, int events, IntPtr data);

    public Graph AddCallback(Action<object, Event> callback, MapperType mapperTypes = MapperType.Object)
    {
        // TODO: check if handler is already registered
        if (handlers.Count == 0)
            mpr_graph_add_cb(_obj,
                Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(_handler)),
                (int)MapperType.Object,
                IntPtr.Zero);
        handlers.Add(new Handler(callback, mapperTypes));
        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern void mpr_graph_remove_cb(IntPtr graph, IntPtr cb, IntPtr data);

    public Graph RemoveCallback(Action<object, Event> callback)
    {
        int i = -1, found = -1;
        handlers.ForEach(delegate(Handler h)
        {
            if (h._callback == callback)
                found = i;
            ++i;
        });
        if (i >= 0)
        {
            handlers.RemoveAt(found);
            if (handlers.Count == 0)
                mpr_graph_remove_cb(_obj,
                    Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(_handler)),
                    IntPtr.Zero);
        }

        return this;
    }

    [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
    private static extern IntPtr mpr_graph_get_list(IntPtr graph, int type);

    public MapperList<Device> Devices => new(mpr_graph_get_list(_obj, (int)MapperType.Device), MapperType.Device);
    public MapperList<Signal> Signals => new(mpr_graph_get_list(_obj, (int)MapperType.Signal), MapperType.Signal);
    public MapperList<Map> Maps => new(mpr_graph_get_list(_obj, (int)MapperType.Map), MapperType.Map);
    
    private delegate void HandlerDelegate(IntPtr graph, IntPtr obj, int evt, IntPtr data);

    protected class Handler
    {
        public Action<object, Event> _callback;
        public MapperType MapperTypes;

        public Handler(Action<object, Event> callback, MapperType mapperTypes)
        {
            _callback = callback;
            MapperTypes = mapperTypes;
        }
    }
}