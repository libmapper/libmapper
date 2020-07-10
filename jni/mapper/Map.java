
package mapper;

import mapper.map.Location;

public class Map extends mapper.AbstractObject
{
    /* constructor */
    public Map(long map) {
        super(map);
        if (_obj == 0)
            return;
    }

    /* self */
    @Override
    Map self() {
        return this;
    }

    private native long mapperMapNew(mapper.Signal[] _srcs, mapper.Signal _dst);
    public Map(mapper.Signal[] _srcs, mapper.Signal[] _dsts) {
        super(0);
        _obj = mapperMapNew(_srcs, _dsts[0]);
    }
    public Map(mapper.Signal _source, mapper.Signal _destination) {
        super(0);
        mapper.Signal[] temp = new Signal[1];
        temp[0] = _source;
        _obj = mapperMapNew(temp, _destination);
    }

    /* refresh */
    public native Map refresh();

    /* property: scopes */
    public native Map addScope(mapper.Device dev);
    public native Map removeScope(mapper.Device dev);

    /* property: ready */
    public native boolean ready();

    /* retrieve associated signals */
    private native long signals(long map, int loc);
    public mapper.List<mapper.Signal> signals(Location loc)
        { return new mapper.List<mapper.Signal>(signals(_obj, loc.value())); }
    public mapper.List<mapper.Signal> signals()
        { return new mapper.List<mapper.Signal>(signals(_obj, Location.ANY.value())); }

    /* helpers */
    private native long signal(long map, int loc, int idx);
    public mapper.Signal source(int index)
        { return new mapper.Signal(signal(_obj, Location.SOURCE.value(), index)); }
    public mapper.Signal source()
        { return new mapper.Signal(signal(_obj, Location.SOURCE.value(), 0)); }
    public mapper.Signal destination(int index)
        { return new mapper.Signal(signal(_obj, Location.DESTINATION.value(), index)); }
    public mapper.Signal destination()
        { return new mapper.Signal(signal(_obj, Location.DESTINATION.value(), 0)); }
}
