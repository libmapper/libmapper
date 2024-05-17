using System.Runtime.InteropServices;

namespace Mapper.NET;

public class Graph : MapperObject
    {
        private delegate void HandlerDelegate(IntPtr graph, IntPtr obj, int evt, IntPtr data);

        public enum Event
        {
            New,            //!< New record has been added to the graph.
            Modified,       //!< The existing record has been modified.
            Removed,        //!< The existing record has been removed.
            Expired         //!< The graph has lost contact with the remote entity.
        }

        internal Graph(IntPtr obj) : base(obj) {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_new(int flags);
        public Graph(Type flags) : base(mpr_graph_new((int)flags))
            { _owned = true; }
        public Graph() : base(mpr_graph_new((int)Type.Object))
            { _owned = true; }

        // TODO: check if Graph is used by a Device
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_free(IntPtr graph);
        ~Graph()
            { if (_owned) mpr_graph_free(_obj); }

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
        public Graph SetInterface(string iface)
        {
            mpr_graph_set_interface(_obj, iface);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_get_interface(IntPtr graph);
        public string? GetInterface()
        {
            var iface = mpr_graph_get_interface(_obj);
            return Marshal.PtrToStringAnsi(iface);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_set_address(IntPtr graph,
                                                         [MarshalAs(UnmanagedType.LPStr)] string group,
                                                         int port);
        public Graph SetAddress(string group, int port)
        {
            mpr_graph_set_address(_obj, group, port);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_get_address(IntPtr graph);
        public string? GetAddress()
        {
            var addr = mpr_graph_get_address(_obj);
            return Marshal.PtrToStringAnsi(addr);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_graph_poll(IntPtr graph, int block_ms);
        public int Poll(int block_ms = 0)
            { return mpr_graph_poll(_obj, block_ms); }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_subscribe(IntPtr graph, IntPtr dev, int types, int timeout);
        public Graph Subscribe(Device device, Type types, int timeout = -1)
        {
            mpr_graph_subscribe(_obj, device._obj, (int)types, timeout);
            return this;
        }
        public Graph Subscribe(Type types, int timeout = -1)
        {
            mpr_graph_subscribe(_obj, IntPtr.Zero, (int)types, timeout);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_unsubscribe(IntPtr graph, IntPtr dev);
        public Graph Unsubscribe(Device device)
        {
            mpr_graph_unsubscribe(_obj, device._obj);
            return this;
        }
        public Graph Unsubscribe()
        {
            mpr_graph_unsubscribe(_obj, IntPtr.Zero);
            return this;
        }

        private void _handler(IntPtr graph, IntPtr obj, int evt, IntPtr data)
        {
            var type = (Type) mpr_obj_get_type(obj);
            // Event event = (Event) evt;
            object o;
            var e = (Event)evt;
            switch (type)
            {
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
            handlers.ForEach(delegate(Handler h)
            {
                if ((h._types & type) != 0)
                    h._callback(o, e);
            });
        }

        protected class Handler
        {
            public Action<object, Event> _callback;
            public Type _types;

            public Handler(Action<object, Event> callback, Type types)
            {
                _callback = callback;
                _types = types;
            }
        }
        private List<Handler> handlers = new List<Handler>();

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_graph_add_cb(IntPtr graph, IntPtr handler, int events, IntPtr data);
        public Graph AddCallback(Action<object, Event> callback, Type types = Type.Object)
        {
            // TODO: check if handler is already registered
            if (handlers.Count == 0)
            {
                mpr_graph_add_cb(_obj,
                                 Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(_handler)),
                                 (int)Type.Object,
                                 IntPtr.Zero);
            }
            handlers.Add(new Handler(callback, types));
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
                {
                    mpr_graph_remove_cb(_obj,
                                        Marshal.GetFunctionPointerForDelegate(new HandlerDelegate(_handler)),
                                        IntPtr.Zero);
                }
            }
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_graph_get_list(IntPtr graph, int type);
        public MapperList<Device> GetDevices()
        {
            return new MapperList<Device>(mpr_graph_get_list(_obj, (int)Type.Device), Type.Device);
        }

        public MapperList<Signal> GetSignals()
        {
            return new MapperList<Signal>(mpr_graph_get_list(_obj, (int)Type.Signal), Type.Signal);
        }

        public MapperList<Map> GetMaps()
        {
            return new MapperList<Map>(mpr_graph_get_list(_obj, (int)Type.Map), Type.Map);
        }
    }