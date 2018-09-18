
package mpr;

import mpr.map.Location;

public class Map extends mpr.AbstractObject
{
    private native long mprMapSrcSignalPtr(long p, int i);
    private native long mprMapDstSignalPtr(long p);

    /* constructor */
    public Map(long map) {
        super(map);
        if (_obj == 0)
            return;

        _num_src = this.numSignals(Location.SOURCE);
        sources = new Signal[_num_src];
        for (int i = 0; i < _num_src; i++)
            sources[i] = new Signal(mprMapSrcSignalPtr(_obj, i));
        destination = new Signal(mprMapDstSignalPtr(_obj));
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
        if (_obj == 0)
            return;

        _num_src = this.numSignals(Location.SOURCE);
        sources = new Signal[_num_src];
        for (int i = 0; i < _num_src; i++)
            sources[i] = new Signal(mprMapSrcSignalPtr(_obj, i));
        destination = new Signal(mprMapDstSignalPtr(_obj));
    }
    public Map(mpr.Signal _source, mpr.Signal _destination) {
        super(0);
        mpr.Signal[] temp = new Signal[1];
        temp[0] = _source;
        _obj = mprMapNew(temp, _destination);
        if (_obj == 0)
            return;

        _num_src = this.numSignals(Location.SOURCE);
        sources = new Signal[_num_src];
        for (int i = 0; i < _num_src; i++)
            sources[i] = new Signal(mprMapSrcSignalPtr(_obj, i));
        destination = new Signal(mprMapDstSignalPtr(_obj));
    }

    /* refresh */
    public native Map refresh();

    /* property: scopes */
    public native Map addScope(mpr.Device dev);
    public native Map removeScope(mpr.Device dev);

    /* property: numSignals */
    private native int mprMapNumSignals(long map, int loc);
    public int numSignals(Location loc) {
        return mprMapNumSignals(_obj, loc.value());
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
