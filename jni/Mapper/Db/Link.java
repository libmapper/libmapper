
package Mapper.Db;

import Mapper.PropertyValue;

public class Link
{
    public Link(long linkprops) {
        _linkprops = linkprops;

        _name1 = mdb_link_get_device_name(_linkprops, 1);
        _name2 = mdb_link_get_device_name(_linkprops, 0);

        _host1 = mdb_link_get_device_host(_linkprops, 1);
        _port1 = mdb_link_get_device_port(_linkprops, 1);
        _host2 = mdb_link_get_device_host(_linkprops, 0);
        _port2 = mdb_link_get_device_port(_linkprops, 0);
    }

    private String _name1;
    private String _name2;
    public String name1() { return _name1; }
    public String name2() { return _name2; }
    private native String mdb_link_get_device_name(long p, int first);

    private String _host1;
    private String _host2;
    public String host1() { return _host1; }
    public String host2() { return _host2; }
    private native String mdb_link_get_device_host(long p, int first);

    private int _port1;
    private int _port2;
    public int port1() { return _port1; }
    public int port2() { return _port2; }
    private native int mdb_link_get_device_port(long p, int first);

    public PropertyValue property(String property) {
        return mapper_db_link_property_lookup(_linkprops, property);
    }
    private native PropertyValue mapper_db_link_property_lookup(
        long p, String property);

    private long _linkprops;
}
