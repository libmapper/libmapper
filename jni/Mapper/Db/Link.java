
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

        _num_scopes1 = mdb_link_get_num_scopes(_linkprops, 1);
        _scope1_names = mdb_link_get_scope_names(_linkprops, 1);
        _num_scopes2 = mdb_link_get_num_scopes(_linkprops, 0);
        _scope2_names = mdb_link_get_scope_names(_linkprops, 0);
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

    int _port1;
    int _port2;
    public int port1() { return _port1; }
    public int port2() { return _port2; }
    private native int mdb_link_get_device_port(long p, int first);

    int _num_scopes1;
    public int numScopes1() { return _num_scopes1; }
    int _num_scopes2;
    public int numScopes2() { return _num_scopes2; }
    private native int mdb_link_get_num_scopes(long p, int direction);

    PropertyValue _scope1_names;
    public PropertyValue scope1Names() {
        _scope1_names = mdb_link_get_scope_names(_linkprops, 1);
        return _scope1_names;
    }
    PropertyValue _scope2_names;
    public PropertyValue scope2Names() {
        _scope2_names = mdb_link_get_scope_names(_linkprops, 0);
        return _scope2_names;
    }
    private native PropertyValue mdb_link_get_scope_names(long p, int direction);

    public PropertyValue property(String property) {
        return mapper_db_link_property_lookup(_linkprops, property);
    }
    private native PropertyValue mapper_db_link_property_lookup(
        long p, String property);

    private long _linkprops;
}
