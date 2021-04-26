
#include <cstring>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <signal.h>

#include <mapper/mapper_cpp.h>

#ifdef HAVE_ARPA_INET_H
 #include <arpa/inet.h>
#endif

using namespace mapper;

int received = 0;
int done = 0;

int verbose = 1;
int terminate = 0;
int period = 100;

class out_stream : public std::ostream {
public:
    out_stream() : std::ostream (&buf) {}

private:
    class null_out_buf : public std::streambuf {
    public:
        virtual std::streamsize xsputn (const char * s, std::streamsize n) {
            return n;
        }
        virtual int overflow (int c) {
            return 1;
        }
    };
    null_out_buf buf;
};
out_stream null_out;

void simple_handler(Signal&& sig, int length, Type type, const void *value, Time&& t)
{
    ++received;
    if (verbose) {
        std::cout << "signal update:" << sig[Property::NAME];
    }

    if (!value) {
        if (verbose)
            std::cout << " ––––––––" << std::endl;
        return;
    }
    else if (!verbose)
        return;

    switch (type) {
        case Type::INT32: {
            int *v = (int*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::FLOAT: {
            float *v = (float*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::DOUBLE: {
            double *v = (double*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        default:
            break;
    }
    std::cout << std::endl;
}

void standard_handler(Signal&& sig, Signal::Event event, Id instance, int length,
                      Type type, const void *value, Time&& t)
{
    ++received;
    if (verbose) {
        std::cout << "\t\t\t\t\t   | --> signal update:"
                  << sig[Property::NAME] << "." << instance;
    }

    if (!value) {
        if (verbose)
            std::cout << " ––––––––" << std::endl;
        sig.instance(instance).release();
        return;
    }
    else if (!verbose)
        return;

    switch (type) {
        case Type::INT32: {
            int *v = (int*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::FLOAT: {
            float *v = (float*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::DOUBLE: {
            double *v = (double*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        default:
            break;
    }
    std::cout << std::endl;
}

void instance_handler(Signal::Instance&& si, Signal::Event event, int length,
                      Type type, const void *value, Time&& t)
{
    ++received;
    if (verbose) {
        std::cout << "\t\t\t\t\t   | --> signal update:" << si.signal()[Property::NAME] << "." << si.id();
    }

    if (!value) {
        if (verbose)
            std::cout << " ––––––––" << std::endl;
        si.release();
        return;
    }
    else if (!verbose)
        return;

    switch (type) {
        case Type::INT32: {
            int *v = (int*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::FLOAT: {
            float *v = (float*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        case Type::DOUBLE: {
            double *v = (double*)value;
            for (int i = 0; i < length; i++) {
                std::cout << " " << v[i];
            }
            break;
        }
        default:
            break;
    }
    std::cout << std::endl;
}

void ctrlc(int sig)
{
    done = 1;
}

int main(int argc, char ** argv)
{
    int i = 0, j, result = 0;

    // process flags for -v verbose, -t terminate, -h help
    for (i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] == '-') {
            int len = (int)strlen(argv[i]);
            for (j = 1; j < len; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        printf("testcpp.cpp: possible arguments "
                               "-q quiet (suppress output), "
                               "-t terminate automatically, "
                               "-f fast (execute quickly), "
                               "-h help\n");
                        return 1;
                        break;
                    case 'q':
                        verbose = 0;
                        break;
                    case 'f':
                        period = 1;
                        break;
                    case 't':
                        terminate = 1;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    signal(SIGINT, ctrlc);

    std::ostream& out = verbose ? std::cout : null_out;

    Device dev("mydevice");

    // make a copy of the device to check reference counting
    Device devcopy(dev);

    Signal sig = dev.add_signal(Direction::INCOMING, "in1", 1, Type::FLOAT, "meters")
                    .set_callback(standard_handler);
    dev.remove_signal(sig);
    dev.add_signal(Direction::INCOMING, "in2", 2, Type::INT32).set_callback(standard_handler);
    dev.add_signal(Direction::INCOMING, "in3", 2, Type::INT32).set_callback(standard_handler);
    dev.add_signal(Direction::INCOMING, "in4", 2, Type::INT32).set_callback(simple_handler);

    sig = dev.add_signal(Direction::OUTGOING, "out1", 1, Type::FLOAT, "na");
    dev.remove_signal(sig);
    sig = dev.add_signal(Direction::OUTGOING, "out2", 3, Type::DOUBLE, "meters");

    out << "waiting" << std::endl;
    while (!dev.ready() && !done) {
        dev.poll(10);
    }
    out << "ready" << std::endl;

    out << "device " << dev[Property::NAME] << " ready..." << std::endl;
    out << "  ordinal: " << dev["ordinal"] << std::endl;
    out << "  id: " << dev[Property::ID] << std::endl;
    out << "  interface: " << dev.graph().iface() << std::endl;
    out << "  bus url: " << dev.graph().address() << std::endl;
    out << "  port: " << dev["port"] << std::endl;
    out << "  num_inputs: " << dev.signals(Direction::INCOMING).size() << std::endl;
    out << "  num_outputs: " << dev.signals(Direction::OUTGOING).size() << std::endl;
    out << "  num_incoming_maps: " << dev.maps(Direction::INCOMING).size() << std::endl;
    out << "  num_outgoing_maps: " << dev.maps(Direction::OUTGOING).size() << std::endl;

    int value[] = {1,2,3,4,5,6};
    dev.set_property("foo", 6, value);
    out << "foo: " << dev["foo"] << std::endl;

    dev["foo"] = 100;
    out << "foo: " << dev["foo"] << std::endl;

    // test std::array<std::string>
    out << "set and get std::array<std::string>: ";
    std::array<std::string, 3> a1 = {{"one", "two", "three"}};
    dev["foo"] = a1;
    const std::array<std::string, 8> a2 = dev["foo"];
    for (i = 0; i < 8; i++)
        out << a2[i] << " ";
    out << std::endl;

    // test std::array<const char*>
    out << "set and get std::array<const char*>: ";
    std::array<const char*, 3> a3 = {{"four", "five", "six"}};
    dev["foo"] = a3;
    std::array<const char*, 3> a4 = dev["foo"];
    for (i = 0; i < (int)a4.size(); i++)
        out << a4[i] << " ";
    out << std::endl;

    // test plain array of const char*
    out << "set and get const char*[]: ";
    const char* a5[3] = {"seven", "eight", "nine"};
    dev.set_property("foo", 3, a5);
    const char **a6 = dev["foo"];
    out << a6[0] << " " << a6[1] << " " << a6[2] << std::endl;

    // test plain array of float
    out << "set and get float[]: ";
    float a7[3] = {7.7f, 8.8f, 9.9f};
    dev.set_property("foo", 3, a7);
    const float *a8 = dev["foo"];
    out << a8[0] << " " << a8[1] << " " << a8[2] << std::endl;

    // test std::vector<const char*>
    out << "set and get std::vector<const char*>: ";
    const char *a9[3] = {"ten", "eleven", "twelve"};
    std::vector<const char*> v1(a9, std::end(a9));
    dev["foo"] = v1;
    std::vector<const char*> v2 = dev["foo"];
    out << "foo: ";
    for (std::vector<const char*>::iterator it = v2.begin(); it != v2.end(); ++it)
        out << *it << " ";
    out << std::endl;

    // test std::vector<std::string>
    out << "set and get std::vector<std::string>: ";
    const char *a10[3] = {"thirteen", "14", "15"};
    std::vector<std::string> v3(a10, std::end(a10));
    dev["foo"] = v3;
    std::vector<std::string> v4 = dev["foo"];
    out << "foo: ";
    for (std::vector<std::string>::iterator it = v4.begin(); it != v4.end(); ++it)
        out << *it << " ";
    out << std::endl;

    dev.remove_property("foo");
    out << "foo: " << dev["foo"] << " (should be 0x0)" << std::endl;

    out << "signal: " << sig << std::endl;

    List<Signal> qsig = dev.signals(Direction::INCOMING);
    qsig.begin();
    for (; qsig != qsig.end(); ++qsig) {
        out << "  input: " << *qsig << std::endl;
    }

    Graph graph;
    Map map(dev.signals(Direction::OUTGOING)[0], dev.signals(Direction::INCOMING)[1]);
    map[Property::EXPRESSION] = "y=x[0:1]+123";

    map.push();

    while (!map.ready() && !done) {
        dev.poll(10);
    }

    std::vector <double> v(3);
    while (i++ < 100 && !done) {
        graph.poll();
        v[i%3] = i;
        if (i == 50) {
            Signal s = *dev.signals().filter(Property::NAME, "in4", Operator::EQUAL);
            s.set_callback(standard_handler);
        }
        sig.set_value(v);
        dev.poll(period);
    }

    // try retrieving linked devices
    out << "devices linked to " << dev << ":" << std::endl;
    List<Device> foo = dev[Property::LINKED];
    for (; foo != foo.end(); foo++) {
        out << "  " << *foo << std::endl;
    }

    // try combining queries
    out << "devices with name matching 'my*' AND >=0 inputs" << std::endl;
    List<Device> qdev = graph.devices();
    qdev.filter(Property::NAME, "my*", Operator::EQUAL);
    qdev.filter(Property::NUM_SIGNALS_IN, 0, Operator::GREATER_THAN_OR_EQUAL);
    for (; qdev != qdev.end(); qdev++) {
        out << "  " << *qdev << " (" << (*qdev)[Property::NUM_SIGNALS_IN] << " inputs)" << std::endl;
    }

    // check graph records
    out << "graph records:" << std::endl;
    for (const Device d : graph.devices()) {
        out << "  device: " << d << std::endl;
        for (Signal s : d.signals(Direction::INCOMING)) {
            out << "    input: " << s << std::endl;
        }
        for (Signal s : d.signals(Direction::OUTGOING)) {
            out << "    output: " << s << std::endl;
        }
    }
    for (Map m : graph.maps()) {
        out << "  map: " << m << std::endl;
    }

    // test API for signal instances
    out << "testing instances API" << std::endl;

    int num_inst = 10;
    mapper::Signal multisend = dev.add_signal(Direction::OUTGOING, "multisend", 1, Type::FLOAT,
                                              0, 0, 0, &num_inst);
    mapper::Signal multirecv = dev.add_signal(Direction::INCOMING, "multirecv", 1, Type::FLOAT,
                                              0, 0, 0, &num_inst)
                                  .set_callback(instance_handler, Signal::Event::UPDATE);
    multisend.set_property(Property::STEAL_MODE, (int)MPR_STEAL_OLDEST);
    multirecv.set_property(Property::STEAL_MODE, (int)MPR_STEAL_OLDEST);
    mapper::Map map2(multisend, multirecv);
    map2.push();
    while (!map2.ready() && !done) {
        dev.poll(10);
    }
    unsigned long id;
    for (int i = 0; i < 200 && !done; i++) {
        id = (rand() % 10) + 5;
        switch (rand() % 5) {
            case 0:
                // try to destroy an instance
                if (verbose)
                    printf("\t\t  Retiring instance %2lu --> |\n",
                           (unsigned long)id);
                multisend.instance(id).release();
                break;
            default:
                // try to update an instance
                float v = (rand() % 10) * 1.0f;
                multisend.instance(id).set_value(v);
                if (verbose)
                    printf("Sender instance %2lu updated to %2f --> |\n",
                           (unsigned long)id, v);
                break;
        }
        dev.poll(period);
    }

    // test some time manipulation
    Time t1(10, 200);
    Time t2(10, 300);
    if (t1 < t2)
        out << "t1 is less than t2" << std::endl;
    t1 += t2;
    if (t1 >= t2)
        out << "(t1 + t2) is greater then or equal to t2" << std::endl;

    printf("\r..................................................Test %s\x1B[0m.\n",
           result ? "\x1B[31mFAILED" : "\x1B[32mPASSED");
    return result;
}
