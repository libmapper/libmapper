
#include <cstring>
#include <iostream>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <array>

#include <mapper/mapper_cpp.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#else
#include <unistd.h>
#endif

int received = 0;

void insig_handler(mapper_signal sig, mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        printf("--> destination got %s", props->name);
        switch (props->type) {
            case 'i': {
                int *v = (int*)value;
                for (int i = 0; i < props->length; i++) {
                    printf(" %d", v[i]);
                }
                break;
            }
            case 'f': {
                float *v = (float*)value;
                for (int i = 0; i < props->length; i++) {
                    printf(" %f", v[i]);
                }
            }
            case 'd': {
                double *v = (double*)value;
                for (int i = 0; i < props->length; i++) {
                    printf(" %f", v[i]);
                }
            }
            default:
                break;
        }
        printf("\n");
    }
    received++;
}

int main(int argc, char ** argv)
{
    int i = 0, result = 0;

    mapper::Device dev("mydevice");

    mapper::Signal sig = dev.add_input("in1", 1, 'f', "meters", 0,
                                       0, insig_handler, 0);
    dev.remove_input(sig);
    dev.add_input("in2", 2, 'i', 0, 0, 0, insig_handler, 0);
    dev.add_input("in3", 2, 'i', 0, 0, 0, insig_handler, 0);
    dev.add_input("in4", 2, 'i', 0, 0, 0, insig_handler, 0);

    dev.add_output("out1", 1, 'f', "na", 0, 0);
    dev.remove_output("out1");
    sig = dev.add_output("out2", 3, 'd', "meters", 0, 0);

    while (!dev.ready()) {
        dev.poll(100);
    }

    std::cout << "device " << dev.name() << " ready..." << std::endl;
    std::cout << "  ordinal: " << dev.ordinal() << std::endl;
    std::cout << "  id: " << dev.id() << std::endl;
    std::cout << "  interface: " << dev.interface() << std::endl;
    const struct in_addr* a = dev.ip4();
    if (a)
        std::cout << "  host: " << inet_ntoa(*a) << std::endl;
    std::cout << "  port: " << dev.port() << std::endl;
    std::cout << "  num_fds: " << dev.num_fds() << std::endl;
    std::cout << "  num_inputs: " << dev.num_inputs() << std::endl;
    std::cout << "  num_outputs: " << dev.num_outputs() << std::endl;
    std::cout << "  num_incoming_maps: " << dev.num_incoming_maps() << std::endl;
    std::cout << "  num_outgoing_maps: " << dev.num_outgoing_maps() << std::endl;

    // access properties through the db_device
    mapper::Device::Properties props = dev.properties();
    std::cout << "name: " << (const char*)props.get("name") << std::endl;

    int value[] = {1,2,3,4,5,6};
    dev.properties().set("foo", value, 6);
    const int *tempi = dev.properties().get("foo");
    std::cout << "foo: ";
    for (i = 0; i < 6; i++)
        std::cout << tempi[i] << " ";
    std::cout << std::endl;

    // can also access properties like this
    std::cout << "name: " << (const char*)dev.property("name") << std::endl;

    // test std::array<std::string>
    std::cout << "set and get std::array<std::string>: ";
    std::array<std::string, 3> a1 = {{"one", "two", "three"}};
    dev.property("foo").set(a1);
    const std::array<std::string, 8> a2 = dev.property("foo");
    for (i = 0; i < 8; i++)
        std::cout << a2[i] << " ";
    std::cout << std::endl;

    // test std::array<const char*>
    std::cout << "set and get std::array<const char*>: ";
    std::array<const char*, 3> a3 = {{"four", "five", "six"}};
    dev.property("foo").set(a3);
    std::array<const char*, 3> a4 = dev.property("foo");
    for (i = 0; i < a4.size(); i++)
        std::cout << a4[i] << " ";
    std::cout << std::endl;

    // test plain array of const char*
    std::cout << "set and get const char*[]: ";
    const char* a5[3] = {"seven", "eight", "nine"};
    dev.property("foo").set(a5, 3);
    const char **a6 = dev.property("foo");
    std::cout << a6[0] << " " << a6[1] << " " << a6[2] << std::endl;

    // test plain array of float
    std::cout << "set and get float[]: ";
    float a7[3] = {7.7f, 8.8f, 9.9f};
    dev.property("foo").set(a7, 3);
    const float *a8 = dev.property("foo");
    std::cout << a8[0] << " " << a8[1] << " " << a8[2] << std::endl;

    // test std::vector<const char*>
    std::cout << "set and get std::vector<const char*>: ";
    const char *a9[3] = {"ten", "eleven", "twelve"};
    std::vector<const char*> v1(a9, std::end(a9));
    dev.property("foo").set(v1);
    std::vector<const char*> v2 = dev.property("foo");
    std::cout << "foo: ";
    for (std::vector<const char*>::iterator it = v2.begin(); it != v2.end(); ++it)
        std::cout << *it << " ";
    std::cout << std::endl;

    // test std::vector<std::string>
    std::cout << "set and get std::vector<std::string>: ";
    const char *a10[3] = {"thirteen", "14", "15"};
    std::vector<std::string> v3(a10, std::end(a10));
    dev.property("foo").set(v3);
    std::vector<std::string> v4 = dev.property("foo");
    std::cout << "foo: ";
    for (std::vector<std::string>::iterator it = v4.begin(); it != v4.end(); ++it)
        std::cout << *it << " ";
    std::cout << std::endl;

    mapper::Property p("temp", "tempstring");
    dev.properties().set(p);
    std::cout << p.name << ": " << (const char*)p << std::endl;

    // access property using overloaded index operator
    std::cout << "temp: " << (const char*)dev.properties()["temp"] << std::endl;

    dev.properties().remove("foo");
    std::cout << "foo: " << dev.properties().get("foo").value
        << " (should be 0x0)" << std::endl;

    std::cout << "signal: " << (const char*)sig << std::endl;

    for (int i = 0; i < dev.num_inputs(); i++) {
        std::cout << "input: " << (const char*)dev.inputs(i) << std::endl;
    }
    mapper::Signal::Iterator iter = dev.inputs().begin();
    for (; iter != dev.inputs().end(); iter++) {
        std::cout << "input: " << (const char*)(*iter) << std::endl;
    }

    mapper::Monitor mon(SUBSCRIBE_ALL);
    mapper::Db::Map map;
    map.set_mode(MO_EXPRESSION).set_expression("y=x[0:1]+123");
    double d[3] = {1., 2., 3.};
    map.source().set_minimum(mapper::Property(0, d, 3));
    mon.map(dev.outputs("out2"), dev.inputs("in2"), map);

    while (dev.num_outgoing_maps() <= 0) {
        dev.poll(100);
    }

    std::vector <double> v(3);
    while (i++ < 100) {
        dev.poll(10);
        mon.poll();
        v[i%3] = i;
        sig.update(v);
    }

    // try combining queries
    mapper::Db::Device::Iterator r = mon.db().devices_by_name_match("my");
    r += mon.db().devices_by_property(mapper::Property("num_inputs", 4),
                                      OP_GREATER_THAN_OR_EQUAL);
//    mapper::Db::Device::Iterator r = q1 + q2;
    for (; r != r.end(); r++) {
        std::cout << "  r device: " << (const char*)(*r) << std::endl;
    }

    // check db records
    std::cout << "db records:" << std::endl;
    for (auto const &device : mon.db().devices()) {
        std::cout << "  device: " << (const char*)device.get("name") << std::endl;
    }
    for (auto const &signal : mon.db().inputs()) {
        std::cout << "  input signal: " << signal.device().name()
            << "/" << signal.name() << std::endl;
    }
    for (auto const &signal : mon.db().outputs()) {
        std::cout << "  output signal: " << signal.device().name()
            << "/" << signal.name() << std::endl;
    }
    for (auto const &m : mon.db().maps()) {
        std::cout << "  map: ";
        if (m.num_sources() > 1)
            std::cout << "[";
        for (int i = 0; i < m.num_sources(); i++) {
            std::cout << m.source(i).signal().device().name()
                << "/" << m.source(i).signal().name() << ", ";
        }
        std::cout << "\b\b";
        if (m.num_sources() > 1)
            std::cout << "]";
        std::cout << " -> " << m.destination().signal().device().name()
            << "/" << m.destination().signal().name() << std::endl;
    }

    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
