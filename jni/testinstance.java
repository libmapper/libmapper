
import mapper.*;
import mapper.graph.*;
import mapper.signal.*;
import mapper.map.*;
import java.util.Arrays;
import java.util.Iterator;

class testinstance {
    public static void main(String [] args) {
        final Device dev1 = new Device("java_testinstance");
        final Device dev2 = new Device("java_testinstance");
        Time start = new Time();
        System.out.println("Current time: "+start.now());

        // This is how to ensure the device is freed when the program
        // exits, even on SIGINT.  The Device must be declared "final".
        Runtime.getRuntime().addShutdownHook(new Thread() {
                @Override
                public void run() {
                        dev1.free();
                        dev2.free();
                    }
            });

        Signal inp1 = dev1.addSignal(Direction.IN, "insig1", 1, Type.INT32, "Hz",
                                     2.0f, null, 10, new mapper.signal.Listener() {
            public void onEvent(Signal.Instance inst, mapper.signal.Event e, int v, Time time) {
                System.out.println("onEvent() for "
                                   +inst.properties().get("name")+": "
                                   +v+" at t="+time);
            }});

        System.out.println("Input signal name: "+inp1.properties().get("name"));

        Signal out1 = dev2.addSignal(Direction.OUT, "outsig1", 1, Type.INT32,
                                     "Hz", 0, 1, 10, null);
        Signal out2 = dev2.addSignal(Direction.OUT, "outsig2", 2, Type.FLOAT,
                                     "Hz", 0.0f, 1.0f, 10, null);

        System.out.print("Waiting for ready... ");
        while (!dev1.ready() || !dev2.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }
        System.out.println("OK");

        mapper.Map map = new mapper.Map(out1, inp1);
        map.properties().put(Property.EXPRESSION, "y=x*100");
        map.push();

        System.out.print("Establishing map... ");
        while (!map.ready()) {
            dev1.poll(50);
            dev2.poll(50);
        }
        System.out.println("OK");

        int i = 0;

        // Signal should report no value before the first update.
        if (out1.hasValue())
            System.out.println("Signal has value: " + out1.getValue());
        else
            System.out.println("Signal has no value.");

        // Test instances
        out1.setListener(new mapper.signal.Listener() {
            public void onEvent(Signal.Instance inst, mapper.signal.Event e, int v, Time time) {
                System.out.println("onEvent() for "
                                   + inst.properties().get("name") + " instance "
                                   + inst.id() + ": " + e.value());
//                java.lang.Object userObject = inst.userReference();
//                if (userObject != null) {
//                    System.out.println("userObject.class = "+userObject.getClass());
//                    if (userObject.getClass().equals(int[].class)) {
//                        System.out.println("  got int[] userObject "
//                                           +Arrays.toString((int[])userObject));
//                    }
//                }
            }}, mapper.signal.Event.ALL);

//        inp1.setListener(new mapper.signal.Listener() {
//            public void onEvent(Signal.Instance inst, mapper.signal.Event e, int v, Time time) {
//                System.out.println("in onEvent() for "
//                                   +inst.signal().properties().get("name")+" instance "
//                                   +inst.id()+": "+inst.userReference()+", val= "
//                                   +v);
//            }});

//        System.out.println(inp1.properties().get("name") + " allocation mode: "
//                           + inp1.getStealingMode());
//        inp1.setStealingType(StealingMode.NEWEST);
//        System.out.println(inp1.properties().get("name") + " allocation mode: "
//                           + inp1.getStealingMode());

        System.out.println("Reserving 4 instances for signal "
                           +out1.properties().get("name"));
        out1.reserveInstances(4);
//        int[] foo = new int[]{1,2,3,4};
//        Signal.Instance instance1 = out1.instance(foo);
        Signal.Instance instance1 = out1.instance();
        Signal.Instance instance2 = out1.instance();

        System.out.println("Reserving 4 instances for signal "
                           +inp1.properties().get("name"));
        inp1.reserveInstances(4);

        while (i++ <= 100) {
            if ((i % 3) > 0) {
                instance1.setValue(i);
                System.out.println("Updated instance1 value to: " + instance1.getValue());
            }
            else {
                instance2.setValue(i);
                System.out.println("Updated instance2 value to: " + instance2.getValue());
            }

            dev1.poll(50);
            dev2.poll(50);
        }

//        System.out.println(inp1.properties().get("name") + " oldest instance is "
//                           + inp1.oldestActiveInstance());
//        System.out.println(inp1.properties().get("name") + " newest instance is "
//                           + inp1.newestActiveInstance());
    }
}
