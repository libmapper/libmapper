
package mapper.signal;

import java.util.Iterator;

public class Query implements Iterable<mapper.Signal> {
    public Query(long sig) {
        _sig = sig;
    }

    @Override
    public Iterator<mapper.Signal> iterator() {
        Iterator<mapper.Signal> it = new Iterator() {
            private long _sigcopy = mapperSignalQueryCopy(_sig);

            protected void finalize() {
                if (_sigcopy != 0)
                    mapperSignalQueryDone(_sigcopy);
            }

            @Override
            public boolean hasNext() { return _sigcopy != 0; }

            @Override
            public mapper.Signal next() {
                if (_sigcopy != 0) {
                    long temp = mapperSignalDeref(_sigcopy);
                    _sigcopy = mapperSignalQueryNext(_sigcopy);
                    return new mapper.Signal(temp);
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
        _sig = mapperSignalQueryJoin(_sig, q._sig);
        return this;
    }
    public Query intersect(Query q) {
        _sig = mapperSignalQueryIntersect(_sig, q._sig);
        return this;
    }
    public Query subtract(Query q) {
        _sig = mapperSignalQuerySubtract(_sig, q._sig);
        return this;
    }

    protected void finalize() {
        if (_sig != 0)
            mapperSignalQueryDone(_sig);
    }

    private native long mapperSignalDeref(long ptr);
    private native long mapperSignalQueryCopy(long ptr);
    private native long mapperSignalQueryJoin(long lhs, long rhs);
    private native long mapperSignalQueryIntersect(long lhs, long rhs);
    private native long mapperSignalQuerySubtract(long lhs, long rhs);
    private native long mapperSignalQueryNext(long ptr);
    private native void mapperSignalQueryDone(long ptr);

    private long _sig;
}
