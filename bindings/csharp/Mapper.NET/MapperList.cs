using System.Collections;
using System.Runtime.InteropServices;

namespace Mapper.NET;

   public abstract class _MapperList {
        protected Type _type;
        private IntPtr _list;
        private bool _started;

        public _MapperList()
        {
            _type = Type.Null;
            _list = IntPtr.Zero;
            _started = false;
        }

        public _MapperList(IntPtr list, Type type)
        {
            _type = type;
            _list = list;
            _started = false;
        }

        /* copy constructor */
        public _MapperList(_MapperList original)
        {
            _list = mpr_list_get_cpy(original._list);
            _type = original._type;
            _started = false;
        }

        // TODO: probably need refcounting to check if we should free the underlying mpr_list
        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern void mpr_list_free(IntPtr list);
        public void Free()
        {
            mpr_list_free(_list);
            _list = IntPtr.Zero;
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern int mpr_list_get_size(IntPtr list);
        public int Count()
        {
            return mpr_list_get_size(_list);
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_idx(IntPtr list, int index);
        public IntPtr GetIdx(int index)
        {
            return mpr_list_get_idx(_list, index);
        }

        public IntPtr Deref()
        {
            unsafe {
                return new IntPtr(_list != IntPtr.Zero ? *(void**)_list : null);
            }
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_cpy(IntPtr list);

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_union(IntPtr list1, IntPtr list2);
        public _MapperList Join(_MapperList rhs)
        {
            _list = mpr_list_get_union(_list, mpr_list_get_cpy(rhs._list));
            return this;
        }

        public static IntPtr Union(_MapperList a, _MapperList b)
        {
            return mpr_list_get_union(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_isect(IntPtr list1, IntPtr list2);
        public _MapperList Intersect(_MapperList rhs)
        {
            _list = mpr_list_get_isect(_list, mpr_list_get_cpy(rhs._list));
            return this;
        }
        public static IntPtr Intersection(_MapperList a, _MapperList b)
        {
            return mpr_list_get_isect(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_diff(IntPtr list1, IntPtr list2);
        public _MapperList Subtract(_MapperList rhs)
        {
            _list = mpr_list_get_diff(_list, mpr_list_get_cpy(rhs._list));
            return this;
        }
        public static IntPtr Difference(_MapperList a, _MapperList b)
        {
            return mpr_list_get_diff(mpr_list_get_cpy(a._list), mpr_list_get_cpy(b._list));
        }

        [DllImport("mapper", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        private static extern IntPtr mpr_list_get_next(IntPtr list);
        public bool GetNext()
        {
            if (_started)
            {
                _list = mpr_list_get_next(_list);
            }
            else
                _started = true;
            return _list != IntPtr.Zero;
        }

        public override string ToString() => $"Mapper.List<{_type}>";
    }

    public class MapperList<T> : _MapperList, IEnumerator, IEnumerable, IDisposable
        where T : MapperObject, new()
    {
        internal MapperList(IntPtr list, Type type) : base(list, type) {}

        public T this[int index]
        {
            get { T t = new T(); t._obj = GetIdx(index); return t; }
        }

        /* Overload some arithmetic operators */
        public static MapperList<T> operator +(MapperList<T> a, MapperList<T> b)
            => new MapperList<T>( Union(a, b), a._type );

        public static MapperList<T> operator *(MapperList<T> a, MapperList<T> b)
            => new MapperList<T>( Intersection(a, b), a._type );

        public static MapperList<T> operator -(MapperList<T> a, MapperList<T> b)
            => new MapperList<T>( Difference(a, b), a._type );

        /* Methods for enumeration */
        public IEnumerator GetEnumerator()
        {
            return this;
        }

        public void Reset()
        {
            // TODO: throw NotSupportedException;
        }

        public bool MoveNext()
        {
            return GetNext();
        }

        void IDisposable.Dispose()
        {
            Free();
        }

        public T Current
        {
            get
            {
                T t = new T();
                t._obj = Deref();
                return t;
            }
        }

        object IEnumerator.Current
        {
            get
            {
                return Current;
            }
        }
    }