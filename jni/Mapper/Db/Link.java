
package Mapper.Db;

import Mapper.PropertyValue;

public class Link
{
    public Link(long linkprops) {
        _linkprops = linkprops;

        _src_name = mdb_link_get_src_name(_linkprops);
        _dest_name = mdb_link_get_dest_name(_linkprops);

        _src_host = mdb_link_get_src_host(_linkprops);
        _src_port = mdb_link_get_src_port(_linkprops);
        _dest_host = mdb_link_get_dest_host(_linkprops);
        _dest_port = mdb_link_get_dest_port(_linkprops);
    }

    private String _src_name;
    public String srcName() { return _src_name; }
    private native String mdb_link_get_src_name(long p);

    private String _dest_name;
    public String destName() { return _dest_name; }
    private native String mdb_link_get_dest_name(long p);

    private String _src_host;
    public String srcHost() { return _src_host; }
    private native String mdb_link_get_src_host(long p);

    int _src_port;
    public int srcPort() { return _src_port; }
    private native int mdb_link_get_src_port(long p);

    private String _dest_host;
    public String destHost() { return _dest_host; }
    private native String mdb_link_get_dest_host(long p);

    int _dest_port;
    public int destPort() { return _dest_port; }
    private native int mdb_link_get_dest_port(long p);

    public PropertyValue property(String property) {
        return mapper_db_link_property_lookup(_linkprops, property);
    }
    private native PropertyValue mapper_db_link_property_lookup(
        long p, String property);

    private long _linkprops;
}
