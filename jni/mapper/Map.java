
package mapper;

import mapper.map.Location;

public class Map extends mapper.AbstractObject
{
    private native long mapperMapSrcSignalPtr(long p, int i);
    private native long mapperMapDstSignalPtr(long p);

    /* constructor */
    public Map(long map) {
        super(map);
        if (_obj == 0)
            return;

        _num_src = this.numSignals(Location.SOURCE);
        sources = new Signal[_num_src];
        for (int i = 0; i < _num_src; i++)
            sources[i] = new Signal(mapperMapSrcSignalPtr(_obj, i));
        destination = new Signal(mapperMapDstSignalPtr(_obj));
    }

    /* self */
    @Override
    Map self() {
        return this;
    }

    private native long mapperMapNew(mapper.Signal[] _sources,
                                     mapper.Signal _destination);
    public Map(mapper.Signal[] _sources, mapper.Signal[] _destinations) {
        super(0);
        _obj = mapperMapNew(_sources, _destinations[0]);
        if (_obj == 0)
            return;

        _num_src = this.numSignals(Location.SOURCE);
        sources = new Signal[_num_src];
        for (int i = 0; i < _num_src; i++)
            sources[i] = new Signal(mapperMapSrcSignalPtr(_obj, i));
        destination = new Signal(mapperMapDstSignalPtr(_obj));
    }
    public Map(mapper.Signal _source, mapper.Signal _destination) {
        super(0);
        mapper.Signal[] temp = new Signal[1];
        temp[0] = _source;
        _obj = mapperMapNew(temp, _destination);
        if (_obj == 0)
            return;

        _num_src = this.numSignals(Location.SOURCE);
        sources = new Signal[_num_src];
        for (int i = 0; i < _num_src; i++)
            sources[i] = new Signal(mapperMapSrcSignalPtr(_obj, i));
        destination = new Signal(mapperMapDstSignalPtr(_obj));
    }

    /* refresh */
    public native Map refresh();

    /* property: scopes */
    public native mapper.List<mapper.Device> getScopes();
    public native Map addScope(mapper.Device dev);
    public native Map removeScope(mapper.Device dev);

    /* property: numSignals */
    private native int mapperMapNumSignals(long map, int loc);
    public int numSignals(Location loc) {
        return mapperMapNumSignals(_obj, loc.value());
    }
    public int numSignals() {
        return numSignals(Location.ANY);
    }

    /* property: ready */
    public native boolean ready();

    public Signal source(int index) {
        if (index > _num_src)
            return null;
        return sources[index];
    }
    public Signal source() {
        return source(0);
    }

    public Signal destination() {
        return destination;
    }

    public Signal[] sources;
    public Signal destination;
    private int _num_src;
}
