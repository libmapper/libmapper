
package mpr;

import mpr.map.Location;

public class Map extends mpr.AbstractObject
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

    private native long mprMapNew(mpr.Signal[] _srcs, mpr.Signal _dst);
    public Map(mpr.Signal[] _srcs, mpr.Signal[] _dsts) {
        super(0);
        _obj = mprMapNew(_srcs, _dsts[0]);
    }
    public Map(mpr.Signal _source, mpr.Signal _destination) {
        super(0);
        mpr.Signal[] temp = new Signal[1];
        temp[0] = _source;
        _obj = mprMapNew(temp, _destination);
    }

    /* refresh */
    public native Map refresh();

    /* property: scopes */
    public native Map addScope(mpr.Device dev);
    public native Map removeScope(mpr.Device dev);

    /* property: ready */
    public native boolean ready();

    /* retrieve associated signals */
    private native long signals(long map, int loc);
    public mpr.List<mpr.Signal> signals(Location loc)
        { return new mpr.List<mpr.Signal>(signals(_obj, loc.value())); }
    public mpr.List<mpr.Signal> signals()
        { return new mpr.List<mpr.Signal>(signals(_obj, Location.ANY.value())); }

    /* helpers */
    private native long signal(long map, int loc, int idx);
    public mpr.Signal source(int index)
        { return new mpr.Signal(signal(_obj, Location.SOURCE.value(), index)); }
    public mpr.Signal source()
        { return new mpr.Signal(signal(_obj, Location.SOURCE.value(), 0)); }
    public mpr.Signal destination(int index)
        { return new mpr.Signal(signal(_obj, Location.DESTINATION.value(), index)); }
    public mpr.Signal destination()
        { return new mpr.Signal(signal(_obj, Location.DESTINATION.value(), 0)); }
}
