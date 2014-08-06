
package Mapper.Db;

import java.util.Iterator;

public class SignalCollection implements Iterable<Mapper.Db.Signal>
{
    public SignalCollection(long sigprops_p) {
        _sigprops_p = sigprops_p;
    }

    @Override
    public Iterator<Mapper.Db.Signal> iterator() {
        return new SignalIterator(_sigprops_p);
    }
    private long _sigprops_p;
}
