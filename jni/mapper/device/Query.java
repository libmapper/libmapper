
package mapper.device;

import java.util.Iterator;

public class Query implements Iterable<mapper.Device> {
    public Query(long dev) {
        _dev = dev;
    }

    @Override
    public Iterator<mapper.Device> iterator() {
        Iterator<mapper.Device> it = new Iterator() {
            private long _devcopy = mapperDeviceQueryCopy(_dev);

            protected void finalize() {
                if (_devcopy != 0)
                    mapperDeviceQueryDone(_devcopy);
            }

            @Override
            public boolean hasNext() { return _devcopy != 0; }

            @Override
            public mapper.Device next() {
                if (_devcopy != 0) {
                    long temp = mapperDeviceDeref(_devcopy);
                    _devcopy = mapperDeviceQueryNext(_devcopy);
                    return new mapper.Device(temp);
                }
                else
                    return null;
            }

            @Override
            public void remove() {
                throw new UnsupportedOperationException();
            }
        };
        return it;
    }

    public Query join(Query q) {
        _dev = mapperDeviceQueryJoin(_dev, q._dev);
        return this;
    }
    public Query intersect(Query q) {
        _dev = mapperDeviceQueryIntersect(_dev, q._dev);
        return this;
    }
    public Query subtract(Query q) {
        _dev = mapperDeviceQuerySubtract(_dev, q._dev);
        return this;
    }

    protected void finalize() {
        if (_dev != 0)
            mapperDeviceQueryDone(_dev);
    }

    private native long mapperDeviceDeref(long ptr);
    private native long mapperDeviceQueryCopy(long ptr);
    private native long mapperDeviceQueryJoin(long lhs, long rhs);
    private native long mapperDeviceQueryIntersect(long lhs, long rhs);
    private native long mapperDeviceQuerySubtract(long lhs, long rhs);
    private native long mapperDeviceQueryNext(long ptr);
    private native void mapperDeviceQueryDone(long ptr);
    
    private long _dev;
}
