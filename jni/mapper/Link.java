
package mapper;

import mapper.link.*;
import mapper.Value;
import java.util.Iterator;

public class Link
{
    /* constructor */
    public Link(long link) {
        _link = link;
    }

    /* push */
    public native Link push();

    /* properties */
    public native int numProperties();
    public native Value property(String property);
    public native Property property(int index);
    public native Link setProperty(String property, Value p);
    public Link setProperty(Property prop) {
        return setProperty(prop.name, prop.value);
    }
    public native Link removeProperty(String property);
    public Link removeProperty(Property prop) {
        return removeProperty(prop.name);
    }

    /* property: id */
    public native long id();

    /* property: numMaps */
    public native int numMaps(int index, int dir);

    /* devices */
    public native mapper.Device device(int index);

    private long _link;
}
