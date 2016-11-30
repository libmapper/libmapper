
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

    /* properties */
    public native int numProperties();
    public native Value property(String property);
    public native Property property(int index);
    public native Link setProperty(String name, Value value, boolean publish);
    public Link setProperty(String name, Value value) {
        return setProperty(name, value, true);
    }
    public Link setProperty(Property prop) {
        return setProperty(prop.name, prop.value, prop.publish);
    }
    public native Link removeProperty(String name);
    public Link removeProperty(Property prop) {
        return removeProperty(prop.name);
    }

    /* clear staged properties */
    public native Link clearStagedProperties();

    /* push */
    public native Link push();

    /* property: id */
    public native long id();

    /* property: numMaps */
    public native int numMaps();

    /* devices */
    public native mapper.Device device(int index);

    private long _link;
}
