
package Mapper.Db;

import java.util.Iterator;

public class DeviceCollection implements Iterable<Mapper.Db.Device>
{
    public DeviceCollection(long devprops_p) {
        _devprops_p = devprops_p;
    }

    @Override
    public Iterator<Mapper.Db.Device> iterator() {
        return new DeviceIterator(_devprops_p);
    }
    private long _devprops_p;
}
