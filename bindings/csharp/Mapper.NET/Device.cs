using System.Runtime.InteropServices;

namespace Mapper.NET;

   public class Device : MapperObject
    {
        public Device()
        {}
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_dev_new([MarshalAs(UnmanagedType.LPStr)] string devname, IntPtr graph);
        public Device(string name)
            : base(mpr_dev_new(name, IntPtr.Zero)) { _owned = true; }
        public Device(string name, Graph graph)
            : base(mpr_dev_new(name, graph._obj)) { _owned = true; }

        // construct from mpr_dev pointer
        internal Device(IntPtr dev) : base(dev) {}

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_dev_free(IntPtr dev);
        ~Device()
        {
            if (_owned)
                mpr_dev_free(_obj);
        }

        public override string ToString() => $"Mapper.Device:{GetProperty(Property.Name)}";

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_dev_get_is_ready(IntPtr dev);
        
        /// <summary>
        /// If the device is ready to send and receive signals.
        /// </summary>
        public bool Ready => mpr_dev_get_is_ready(_obj) != 0;

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_dev_poll(IntPtr dev, int block_ms);
        public int Poll(int block_ms)
        {
            return mpr_dev_poll(_obj, block_ms);
        }

        public int Poll()
        {
            return mpr_dev_poll(_obj, 0);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_dev_update_maps(IntPtr dev);
        public Device UpdateMaps()
        {
            mpr_dev_update_maps(_obj);
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_sig_new(IntPtr dev, int direction,
                                                 [MarshalAs(UnmanagedType.LPStr)] string name,
                                                 int length, int type,
                                                 [MarshalAs(UnmanagedType.LPStr)] string? unit,
                                                 IntPtr min, IntPtr max, IntPtr num_inst,
                                                 IntPtr handler, int events);
        public Signal AddSignal(Signal.Direction direction, string name, int length, Type type,
                                string? unit = null, int numInstances = -1)
        {
            var instPtr = IntPtr.Zero;
            if (numInstances != -1)
            {
                unsafe
                {
                    instPtr = new IntPtr(&numInstances);
                }
            }
            var sigptr = mpr_sig_new(_obj, (int) direction, name, length, (int) type, unit,
                                        IntPtr.Zero, IntPtr.Zero, instPtr, IntPtr.Zero, 0);
            return new Signal(sigptr);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_sig_free(IntPtr sig);
        public Device RemoveSignal(Signal signal)
        {
            mpr_sig_free(signal._obj);
            signal._obj = IntPtr.Zero;
            return this;
        }

        public new Device SetProperty<P, T>(P property, T value, bool publish)
        {
            base.SetProperty(property, value, publish);
            return this;
        }

        public new Device SetProperty<P, T>(P property, T value)
        {
            base.SetProperty(property, value, true);
            return this;
        }

        public new Device Push()
        {
            base.Push();
            return this;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_dev_get_sigs(IntPtr dev, int dir);
        public MapperList<Signal> GetSignals(Signal.Direction direction = Signal.Direction.Any)
        {
            return new MapperList<Signal>(mpr_dev_get_sigs(_obj, (int) direction), Type.Signal);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_dev_get_maps(IntPtr dev, int dir);
        public MapperList<Map> GetMaps(Signal.Direction direction = Signal.Direction.Any)
        {
            return new MapperList<Map>(mpr_dev_get_maps(_obj, (int) direction), Type.Map);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern long mpr_dev_get_time(IntPtr dev);
        
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern long mpr_dev_set_time(IntPtr dev, long time);

        /// <summary>
        /// The current time according to the backing device.
        /// </summary>
        public Time Time
        {
            get
            {
                return new Time(mpr_dev_get_time(_obj));
            }
            set
            {
                mpr_dev_set_time(_obj, value.data.ntp);
            }
        }
    }