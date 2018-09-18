
package mpr;

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
            private long _listcopy = mprListCopy(_list);

            protected void finalize() {
                if (_listcopy != 0)
                    mprListFree(_listcopy);
            }

            @Override
            public boolean hasNext() { return _listcopy != 0; }

            @Override
            public T next() {
                if (0 == _listcopy)
                    return null;

                long temp = mprListDeref(_listcopy);
                _listcopy = mprListNext(_listcopy);
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
        _list = mprListJoin(_list, l._list);
        return this;
    }
    public List intersect(List l) {
        _list = mprListIntersect(_list, l._list);
        return this;
    }
    public List subtract(List l) {
        _list = mprListSubtract(_list, l._list);
        return this;
    }

    public List filter(String propName, java.lang.Object propValue) {
        mprListFilter(_list, 0, propName, propValue);
        return this;
    }
    public List filter(Property propID, java.lang.Object propValue) {
        mprListFilter(_list, propID.value(), null, propValue);
        return this;
    }

    public int size() { return mprListLength(_list); }

    protected void finalize() {
        if (_list != 0)
            mprListFree(_list);
    }

    private native T newObject(long ptr);
    
    private native long mprListCopy(long list);
    private native long mprListDeref(long list);
    private native void mprListFilter(long list, int propID, String propName,
                                      java.lang.Object propValue);
    private native void mprListFree(long list);
    private native long mprListIntersect(long lhs, long rhs);
    private native long mprListJoin(long lhs, long rhs);
    private native int  mprListLength(long list);
    private native long mprListNext(long list);
    private native long mprListSubtract(long lhs, long rhs);

    private long _list;
}
