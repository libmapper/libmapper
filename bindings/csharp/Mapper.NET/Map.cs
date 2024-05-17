using System.Runtime.InteropServices;

namespace Mapper.NET;

public class Map : MapperObject
    {
        public enum Location
        {
            Source      = 0x01, //!< Source signal(s) for this map.
            Destination = 0x02, //!< Destination signal(s) for this map.
            Any         = 0x03  //!< Either source or destination signals.
        }

        public enum Protocol {
            UDP,              //!< Map updates are sent using UDP.
            TCP               //!< Map updates are sent using TCP.
        }

        public Map()
        {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern unsafe IntPtr mpr_map_new(int num_srcs, void* srcs, int num_dsts, void* dsts);
        public unsafe Map(Signal source, Signal destination)
        {
            fixed (void* s = &source._obj)
            {
                fixed (void* d = &destination._obj)
                {
                    _obj = mpr_map_new(1, s, 1, d);
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
            var a = new IntPtr[10];
            for (var i = 0; i < 10; i++)
            {
                if (i < signals.Length)
                    a[i] = signals[i]._obj;
                else
                    a[i] = default(IntPtr);
            }
            _obj = mpr_map_new_from_str(expression, a[0], a[1], a[2], a[3], a[4], a[5],
                                             a[6], a[7], a[8], a[9], default(IntPtr));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_map_get_is_ready(IntPtr map);

        /// <summary>
        /// If this map has been completely initialized.
        /// </summary>
        public bool IsReady => mpr_map_get_is_ready(_obj) != 0;
        
        public new Map SetProperty<P, T>(P property, T value, bool publish)
        {
            base.SetProperty(property, value, publish);
            return this;
        }

        public new Map SetProperty<P, T>(P property, T value)
        {
            base.SetProperty(property, value, true);
            return this;
        }

        public new Map Push()
        {
            base.Push();
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_map_refresh(IntPtr map);
        public Map Refresh()
        {
            mpr_map_refresh(_obj);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_map_release(IntPtr map);
        public void Release()
        {
            mpr_map_release(_obj);
            _obj = IntPtr.Zero;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_map_get_sigs(IntPtr map, int loc);
        public MapperList<Signal> GetSignals(Location location = Location.Any)
        {
            return new MapperList<Signal>(mpr_map_get_sigs(_obj, (int)location), Type.Signal);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_map_get_sig_idx(IntPtr map, IntPtr sig);
        public int GetSignalIndex(Signal signal)
        {
            return mpr_map_get_sig_idx(_obj, signal._obj);
        }
    }