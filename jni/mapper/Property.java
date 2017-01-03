
package mapper;
import mapper.Value;

/* Contains a named Value. */
public class Property
{
    public Property(String _name, Value _value, boolean _publish) {
        name = _name;
        value = _value;
        publish = _publish;
    }
    public Property(String _name, Value _value) {
        name = _name;
        value = _value;
        publish = true;
    }

    public String name;
    public Value value;
    public boolean publish;
}
