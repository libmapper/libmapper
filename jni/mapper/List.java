
package mapper;

import java.util.Iterator;

public class List<T extends AbstractObject> implements Iterable<T> {

    /* constructor */
    public List(long listptr)
    {
        _list = listptr;
    }

    @Override
    public Iterator<T> iterator() {
        Iterator<T> it = new Iterator<T>() {
            private long _listcopy = mapperListCopy(_list);

            protected void finalize() {
                if (_listcopy != 0)
                    mapperListFree(_listcopy);
            }

            @Override
            public boolean hasNext() { return _listcopy != 0; }

            @Override
            public T next() {
                if (0 == _listcopy)
                    return null;

                long temp = mapperListDeref(_listcopy);
                _listcopy = mapperListNext(_listcopy);
                return newObject(temp);
            }

            @Override
            public void remove() {
                throw new UnsupportedOperationException();
            }
        };
        return it;
    }

    public List join(List l) {
        _list = mapperListJoin(_list, l._list);
        return this;
    }
    public List intersect(List l) {
        _list = mapperListIntersect(_list, l._list);
        return this;
    }
    public List subtract(List l) {
        _list = mapperListSubtract(_list, l._list);
        return this;
    }

    public List filter(String propName, java.lang.Object propValue) {
        mapperListFilter(_list, 0, propName, propValue);
        return this;
    }
    public List filter(Property propID, java.lang.Object propValue) {
        mapperListFilter(_list, propID.value(), null, propValue);
        return this;
    }

    public int size() { return mapperListLength(_list); }

    protected void finalize() {
        if (_list != 0)
            mapperListFree(_list);
    }

    private native T newObject(long ptr);
    
    private native long mapperListCopy(long list);
    private native long mapperListDeref(long list);
    private native void mapperListFilter(long list, int propID, String propName,
                                         java.lang.Object propValue);
    private native void mapperListFree(long list);
    private native long mapperListIntersect(long lhs, long rhs);
    private native long mapperListJoin(long lhs, long rhs);
    private native int  mapperListLength(long list);
    private native long mapperListNext(long list);
    private native long mapperListSubtract(long lhs, long rhs);

    private long _list;
}
