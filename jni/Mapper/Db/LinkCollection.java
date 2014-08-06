
package Mapper.Db;

import java.util.Iterator;

public class LinkCollection implements Iterable<Mapper.Db.Link>
{
    public LinkCollection(long linkprops_p) {
        _linkprops_p = linkprops_p;
    }

    @Override
    public Iterator<Mapper.Db.Link> iterator() {
        return new LinkIterator(_linkprops_p);
    }
    private long _linkprops_p;
}
