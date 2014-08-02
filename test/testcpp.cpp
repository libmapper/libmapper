
//#include <cstdio.h>
#include <string.h>
#include <iostream>

#include <mapper/mapper.h>
#include <mapper/mapper_cpp.h>
//#include <lo/lo.h>
//#include <lo/lo_cpp.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#else
#include <unistd.h>
#endif

int main(int argc, char ** argv)
{
    int i = 0, result = 0;

    mapper::Device dev("mydevice");

    mapper::Signal sig = dev.add_input("in1", 1, 'f', "meters", 0, 0, 0, 0);
    dev.remove_input(sig);
    dev.add_input("in2", 2, 'i', 0, 0, 0, 0, 0);
    dev.add_input("in3", 2, 'i', 0, 0, 0, 0, 0);
    dev.add_input("in4", 2, 'i', 0, 0, 0, 0, 0);

    sig = dev.add_output("out1", 1, 'f', "na", 0, 0);
    dev.remove_output(sig);
    dev.add_output("out2", 3, 'd', "meters", 0, 0);

    while (!dev.ready()) {
        dev.poll(100);
    }

    std::cout << "device " << dev.name() << "ready..." << std::endl;
    std::cout << "  ordinal: " << dev.ordinal() << std::endl;
    std::cout << "  id: " << dev.id() << std::endl;
    std::cout << "  interface: " << dev.interface() << std::endl;
//    std::cout << "  host: " << dev.host() << std::endl;
    std::cout << "  port: " << dev.port() << std::endl;
    std::cout << "  num_fds: " << dev.num_fds() << std::endl;
//    std::cout << "  clock_offset: " << dev.clock_offset() << std::endl;
    std::cout << "  num_inputs: " << dev.num_inputs() << std::endl;
    std::cout << "  num_outputs: " << dev.num_outputs() << std::endl;
    std::cout << "  num_links_in: " << dev.num_links_in() << std::endl;
    std::cout << "  num_links_out: " << dev.num_links_out() << std::endl;
    std::cout << "  num_connections_in: " << dev.num_connections_in() << std::endl;
    std::cout << "  num_connections_out: " << dev.num_connections_out() << std::endl;

    // access properties through the db_device
    dev.properties().get("name").print();
    std::cout << std::endl;

    int value[] = {1,2,3,4,5,6};
    dev.properties().set("foo", value, 6);
    dev.properties().get("foo").print();
    std::cout << std::endl;

    dev.properties().remove("foo");
    dev.properties().get("foo").print();
    std::cout << std::endl;

    std::cout << "signal " << sig.full_name() << std::endl;

    for (int i = 0; i < dev.num_inputs(); i++) {
        std::cout << "input: " << dev.inputs(i).full_name() << std::endl;
    }
    mapper::Signal::Iterator iter = dev.inputs().begin();
    for (; iter != dev.inputs().end(); iter++) {
        std::cout << "input: " << (*iter).full_name() << std::endl;
    }

    mapper::Monitor mon(SUB_DEVICE_ALL);

    std::vector <double> v(3);
    while (i++ < 100) {
        dev.poll(100);
        mon.poll();
        v[0] = i;
        sig.update(v);
    }

    // check db records
    std::cout << "db records:" << std::endl;
    mapper::DeviceProps::Iterator devices = mon.db().devices().begin();
    for (; devices != devices.end(); devices++) {
        std::cout << "  device: ";
        (*devices).get("name").print();
        std::cout << std::endl;
    }

//  done:
    printf("Test %s.\n", result ? "FAILED" : "PASSED");
    return result;
}
