
package mapper;

import java.util.Iterator;
import java.util.Collection;

public class List<T extends AbstractObject> implements Iterable<T> {

    private native T _newObject(long ptr);
    private native long _deref(long list);
    private native long _copy(long list);
    private native void _free(long list);
    private native long _next(long list);
    private native long _get(long list, int index);
    private native int _size(long list);

    private native long _diff(long lhs, long rhs);
    private native long _isect(long lhs, long rhs);
    private native long _union(long lhs, long rhs);

    private native void _filter(long list, int id, String key,
                                java.lang.Object value, int op);

    /* constructor */
    public List(long listptr) {
        _list = listptr;
    }

    public T get(int index) {
        return _newObject(_get(_list, index));
    }

    public int size() {
        return _size(_list);
    }

    public boolean isEmpty() {
        return _list == 0;
    }

    private native boolean _contains(long listptr, T t);
    public boolean contains(T t) {
        return _contains(_list, t);
    }

    private native boolean _containsAll(long listptr_1, long listptr_2);
    public boolean containsAll(List<T> l) {
        return _containsAll(_list, l._list);
    }

    public List join(List<T> l) {
        _list = _union(_list, l._list);
        return this;
    }

    public List difference(List<T> l) {
        _list = _diff(_list, l._list);
        return this;
    }

    public List intersect(List<T> l) {
        _list = _isect(_list, l._list);
        return this;
    }

    private native T[] _toArray(long listptr);
    public T[] toArray()
        { return _toArray(_list); }

    public Iterator<T> iterator() {
        Iterator<T> it = new Iterator<T>() {
            private long _listcopy = _copy(_list);

//            protected void finalize() {
//                if (_listcopy != 0)
//                    mapperListFree(_listcopy);
//            }

            @Override
            public boolean hasNext() { return _listcopy != 0; }

            @Override
            public T next() {
                if (0 == _listcopy)
                    return null;

                long temp = _deref(_listcopy);
                _listcopy = _next(_listcopy);
                return _newObject(temp);
            }

            @Override
            public void remove() {
                throw new UnsupportedOperationException();
            }
        };
        return it;
    }

    public List filter(String key, java.lang.Object value, Operator operator) {
        _filter(_list, 0, key, value, operator.value());
        return this;
    }
    public List filter(Property id, java.lang.Object value, Operator operator) {
        _filter(_list, id.value(), null, value, operator.value());
        return this;
    }
    //TODO: add version passing filter function

//    protected void finalize() {
//        if (_list != 0)
//            mapperListFree(_list);
//    }

    private long _list;
}
