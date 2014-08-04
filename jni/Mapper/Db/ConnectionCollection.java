
package Mapper.Db;

import java.util.Iterator;

public class ConnectionCollection implements Iterable<Mapper.Db.Connection>
{
    public ConnectionCollection(long conprops_p) {
        _conprops_p = conprops_p;
    }

    @Override
    public Iterator<Mapper.Db.Connection> iterator() {
        return new ConnectionIterator(_conprops_p);
    }
    private long _conprops_p;
}
