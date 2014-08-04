
#ifndef _MAPPER_CPP_H_
#define _MAPPER_CPP_H_

#include <mapper/mapper.h>
#include <mapper/mapper_types.h>
#include <mapper/mapper_db.h>

#include <functional>
#include <memory>
#include <list>
#include <unordered_map>
#include <string>
#include <sstream>
#include <initializer_list>
#include <vector>
#include <iterator>

//#include <lo/lo.h>
//#include <lo/lo_cpp.h>

/* TODO:
 *      signal update handlers
 *      instance event handlers
 *      monitor db handlers
 *      device link & connection handlers
 *      LinkProps: set scopes
 *      Link and COnnection props: set arbitrary properties
 */

//signal_update_handler(Signal sig, instance_id, value, count, TimeTag)
//- optional: instance_id, timetag, count
//
//possible forms:
//(Signal sig, void *value)
//(Signal sig, void *value, TimeTag tt)
//(Signal sig, int instance_id, void *value)
//(Signal sig, int instance_id, void *value, TimeTag tt)
//(Signal sig, void *value, int count)
//(Signal sig, void *value, int count, TimeTag tt)
//(Signal sig, int instance_id, void *value, int count)
//(Signal sig, int instance_id, void *value, int count, TimeTag tt)


namespace mapper {

    // Helper classes to allow polymorphism on "const char *",
    // "std::string", and "int".
    class string_type {
    public:
        string_type(const char *s=0) { _s = s; }
        string_type(const std::string &s) { _s = s.c_str(); }
        operator const char*() const { return _s; }
        const char *_s;
    };

    class num_string_type : public string_type {
    public:
        num_string_type(const char *s) : string_type(s) {}
        num_string_type(const std::string &s) : string_type(s) {}
        num_string_type(int n) { std::ostringstream ss; ss << n;
            _p.reset(new std::string(ss.str())); _s = _p->c_str(); }
        std::unique_ptr<std::string> _p;
    };

    class Admin
    {
    public:
        Admin(const string_type &iface=0, const string_type &group=0, int port=0)
            { admin = mapper_admin_new(iface, group, port); }
        ~Admin()
            { if (admin) mapper_admin_free(admin); }
        operator mapper_admin() const
            { return admin; }
        std::string libversion()
            { return std::string(mapper_admin_libversion(admin)); }
    protected:
        mapper_admin admin;
    };

    class Timetag
    {
    public:
        Timetag(mapper_timetag_t tt)
            { timetag.sec = tt.sec; timetag.frac = tt.frac; }
        Timetag(int seconds)
            { mapper_timetag_set_int(&timetag, seconds); }
        Timetag(float seconds)
            { mapper_timetag_set_float(&timetag, seconds); }
        Timetag(double seconds)
            { mapper_timetag_set_double(&timetag, seconds); }
        operator mapper_timetag_t*()
            { return &timetag; }
    protected:
        mapper_timetag_t timetag;
    };

    class Property
    {
    public:
        Property(const string_type &_name, int _value)
            { name = _name; i = _value; length = 1; type = 'i'; }
        Property(const string_type &_name, float _value)
            { name = _name; f = _value; length = 1; type = 'f'; }
        Property(const string_type &_name, double _value)
            { name = _name; d = _value; length = 1; type = 'd'; }
        Property(const string_type &_name, int *_value, int _length)
            { name = _name; pi = _value; length = _length; type = 'i'; }
        Property(const string_type &_name, float *_value, int _length)
            { name = _name; pf = _value; length = _length; type = 'f'; }
        Property(const string_type &_name, double *_value, int _length)
            { name = _name; pd = _value; length = _length; type = 'd'; }
        Property(const string_type &_name, std::vector<int> _value)
            { name = _name; pi = &_value[0]; length = _value.size(); type = 'i'; }
        Property(const string_type &_name, std::vector<float> _value)
            { name = _name; pf = &_value[0]; length = _value.size(); type = 'f'; }
        Property(const string_type &_name, std::vector<double> _value)
            { name = _name; pd = &_value[0]; length = _value.size(); type = 'd'; }
        Property(const string_type &_name, char _type,
                 const void *_value, int _length)
        {
            name = _name;
            type = _type;
            value = _value;
            length = _length;
        }
        operator const void*() const
            { return value; }
        void print()
        {
            printf("%s: ", name);
            mapper_prop_pp(type, length, value);
        }
        const char *name;
        char type;
        int length;
    protected:
        union {
            const void *value;
            int *pi;
            float *pf;
            double *pd;
            int i;
            float f;
            double d;
        };
    };

    class SignalProps
    {
        // Reuse class for signal and database
    public:
        int found;
        SignalProps(mapper_signal sig)
            { signal = sig; props = msig_properties(signal); found = 1; }
        SignalProps(mapper_db_signal sig_db)
            { signal = 0; props = sig_db; found = sig_db ? 1 : 0; }
        operator mapper_db_signal() const
            { return props; }
        void set(const string_type &name, int value)
            { if (signal) msig_set_property(signal, name, 'i', &value, 1); }
        void set(const string_type &name, float value)
            { if (signal) msig_set_property(signal, name, 'f', &value, 1); }
        void set(const string_type &name, double value)
            { if (signal) msig_set_property(signal, name, 'd', &value, 1); }
        void set(const string_type &name, string_type &value)
            { if (signal) msig_set_property(signal, name, 's',
                                        (void*)(const char *)value, 1); }
        void set(const string_type &name, int *value, int length)
            { if (signal) msig_set_property(signal, name, 'i', value, length); }
        void set(const string_type &name, float *value, int length)
            { if (signal) msig_set_property(signal, name, 'f', value, length); }
        void set(const string_type &name, double *value, int length)
            { if (signal) msig_set_property(signal, name, 'd', value, length); }
        // TODO: string array
        void set(const string_type &name, std::vector<int> value)
            { if (signal) msig_set_property(signal, name, 'i', &value[0], value.size()); }
        void set(const string_type &name, std::vector<float> value)
            { if (signal) msig_set_property(signal, name, 'f', &value[0], value.size()); }
        void set(const string_type &name, std::vector<double> value)
            { if (signal) msig_set_property(signal, name, 'd', &value[0], value.size()); }
//        void set(const string_type &name, std::vector<std::string> value)
//            { if (signal) msig_set_property(signal, name, 's', &value[0], value.size()); }
        void remove(const string_type &name)
            { if (signal) msig_remove_property(signal, name); }
        Property get(const string_type &name)
        {
            char type;
            const void *value;
            int length;
            mapper_db_signal_property_lookup(props, name, &type, &value, &length);
            return Property(name, type, value, length);
        }
        Property get(int index)
        {
            const char *name;
            char type;
            const void *value;
            int length;
            mapper_db_signal_property_index(props, index, &name, &type,
                                            &value, &length);
            return Property(name, type, value, length);
        }
        class Iterator : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Iterator(mapper_db_signal *s)
                { sig = s; }
            ~Iterator()
                { mapper_db_signal_done(sig); }
            operator mapper_db_signal*() const
                { return sig; }
            bool operator==(const Iterator& rhs)
                { return (sig == rhs.sig); }
            bool operator!=(const Iterator& rhs)
                { return (sig != rhs.sig); }
            Iterator& operator++()
            {
                if (sig != NULL)
                    sig = mapper_db_signal_next(sig);
                return (*this);
            }
            Iterator operator++(int)
                { Iterator tmp(*this); operator++(); return tmp; }
            SignalProps operator*()
                { return SignalProps(*sig); }
            Iterator begin()
                { return Iterator(sig); }
            Iterator end()
                { return Iterator(0); }
        private:
            mapper_db_signal *sig;
        };
    protected:
        mapper_signal signal;
        mapper_db_signal props;
    };

    class Signal
    {
    public:
        Signal(mapper_signal sig)
            { signal = sig; props = msig_properties(signal); }
        ~Signal()
            { ; }
        operator mapper_signal() const
            { return signal; }

        // TODO: check if data type is correct in update!
        void update(void *value, int count)
            { msig_update(signal, value, count, MAPPER_NOW); }
        void update(void *value, int count, Timetag tt)
            { msig_update(signal, value, count, *tt); }
        void update(int value)
            { msig_update_int(signal, value); }
        void update(float value)
            { msig_update_float(signal, value); }
        void update(double value)
            { msig_update_double(signal, value); }
        void update(int *value, int count=0)
        {
            if (props->type == 'i')
                msig_update(signal, value, count, MAPPER_NOW);
        }
        void update(float *value, int count=0)
        {
            if (props->type == 'f')
                msig_update(signal, value, count, MAPPER_NOW);
        }
        void update(double *value, int count=0)
        {
            if (props->type == 'd')
                msig_update(signal, value, count, MAPPER_NOW);
        }
        void update(int *value, Timetag tt)
        {
            if (props->type == 'i')
                msig_update(signal, value, 1, *tt);
        }
        void update(float *value, Timetag tt)
        {
            if (props->type == 'f')
                msig_update(signal, value, 1, *tt);
        }
        void update(double *value, Timetag tt)
        {
            if (props->type == 'd')
                msig_update(signal, value, 1, *tt);
        }
        void update(int *value, int count, Timetag tt)
        {
            if (props->type == 'i')
                msig_update(signal, value, count, *tt);
        }
        void update(float *value, int count, Timetag tt)
        {
            if (props->type == 'f')
                msig_update(signal, value, count, *tt);
        }
        void update(double *value, int count, Timetag tt)
        {
            if (props->type == 'd')
                msig_update(signal, value, count, *tt);
        }
        void update(std::vector <int> value)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, MAPPER_NOW);
        }
        void update(std::vector <float> value)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, MAPPER_NOW);
        }
        void update(std::vector <double> value)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, MAPPER_NOW);
        }
        void update(std::vector <int> value, Timetag tt)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, *tt);
        }
        void update(std::vector <float> value, Timetag tt)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, *tt);
        }
        void update(std::vector <double> value, Timetag tt)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, *tt);
        }
        void update_instance(int instance_id, void *value, int count)
            { msig_update_instance(signal, instance_id, value, count, MAPPER_NOW); }
        void update_instance(int instance_id, void *value, int count, Timetag tt)
            { msig_update_instance(signal, instance_id, value, count, *tt); }
        void update_instance(int instance_id, int value)
            { msig_update_instance(signal, instance_id, &value, 1, MAPPER_NOW); }
        void update_instance(int instance_id, float value)
            { msig_update_instance(signal, instance_id, &value, 1, MAPPER_NOW); }
        void update_instance(int instance_id, double value)
            { msig_update_instance(signal, instance_id, &value, 1, MAPPER_NOW); }
        void update_instance(int instance_id, int *value, int count=0)
        {
            if (props->type == 'i')
                msig_update_instance(signal, instance_id, value, count, MAPPER_NOW);
        }
        void update_instance(int instance_id, float *value, int count=0)
        {
            if (props->type == 'f')
                msig_update_instance(signal, instance_id, value, count, MAPPER_NOW);
        }
        void update_instance(int instance_id, double *value, int count=0)
        {
            if (props->type == 'd')
                msig_update_instance(signal, instance_id, value, count, MAPPER_NOW);
        }
        void update_instance(int instance_id, int *value, Timetag tt)
        {
            if (props->type == 'i')
                msig_update_instance(signal, instance_id, value, 1, *tt);
        }
        void update_instance(int instance_id, float *value, Timetag tt)
        {
            if (props->type == 'f')
                msig_update_instance(signal, instance_id, value, 1, *tt);
        }
        void update_instance(int instance_id, double *value, Timetag tt)
        {
            if (props->type == 'd')
                msig_update_instance(signal, instance_id, value, 1, *tt);
        }
        void update_instance(int instance_id, int *value, int count, Timetag tt)
        {
            if (props->type == 'i')
                msig_update_instance(signal, instance_id, value, count, *tt);
        }
        void update_instance(int instance_id, float *value, int count, Timetag tt)
        {
            if (props->type == 'f')
                msig_update_instance(signal, instance_id, value, count, *tt);
        }
        void update_instance(int instance_id, double *value, int count, Timetag tt)
        {
            if (props->type == 'd')
                msig_update_instance(signal, instance_id, value, count, *tt);
        }
        void *value() const
            { return msig_value(signal, 0); }
        void *value(Timetag tt) const
            { return msig_value(signal, tt); }
        void *instance_value(int instance_id) const
            { return msig_instance_value(signal, instance_id, 0); }
        void *instance_value(int instance_id, Timetag tt) const
            { return msig_instance_value(signal, instance_id, tt); }
        int query_remotes() const
            { return msig_query_remotes(signal, MAPPER_NOW); }
        int query_remotes(Timetag tt) const
            { return msig_query_remotes(signal, *tt); }
        void reserve_instances(int num)
            { msig_reserve_instances(signal, num, 0, 0); }
        void reserve_instances(int num, int *instance_ids, void **user_data)
            { msig_reserve_instances(signal, num, instance_ids, user_data); }
        void release_instance(int instance_id)
            { msig_release_instance(signal, instance_id, MAPPER_NOW); }
        void release_instance(int instance_id, Timetag tt)
            { msig_release_instance(signal, instance_id, *tt); }
        void remove_instance(int instance_id)
            { msig_remove_instance(signal, instance_id); }
        int oldest_active_instance(int *instance_id)
            { return msig_get_oldest_active_instance(signal, instance_id); }
        int newest_active_instance(int *instance_id)
            { return msig_get_newest_active_instance(signal, instance_id); }
        int num_active_instances() const
            { return msig_num_active_instances(signal); }
        int num_reserved_instances() const
            { return msig_num_reserved_instances(signal); }
        int active_instance_id(int index) const
            { return msig_active_instance_id(signal, index); }
        void set_instance_allocation_mode(mapper_instance_allocation_type mode)
            { msig_set_instance_allocation_mode(signal, mode); }
        mapper_instance_allocation_type get_instance_allocation_mode() const
            { return msig_get_instance_allocation_mode(signal); }
        void set_instance_event_callback(mapper_signal_instance_event_handler h,
                                         int flags, void *user_data)
            { msig_set_instance_event_callback(signal, h, flags, user_data); }
        void set_instance_data(int instance_id, void *user_data)
            { msig_set_instance_data(signal, instance_id, user_data); }
        void *get_instance_data(int instance_id) const
            { return msig_get_instance_data(signal, instance_id); }
        void set_callback(mapper_signal_update_handler *handler, void *user_data)
            { msig_set_callback(signal, handler, user_data); }
        int num_connections() const
            { return msig_num_connections(signal); }
        std::string full_name() const
        {
            char str[64];
            msig_full_name(signal, str, 64);
            return std::string(str);
        }
        void set_minimum(void *value)
            { msig_set_minimum(signal, value); }
        void set_maximum(void *value)
            { msig_set_maximum(signal, value); }
        void set_rate(int rate)
            { msig_set_rate(signal, rate); }
        SignalProps properties() const
        {
            return SignalProps(msig_properties(signal));
        }
        class Iterator : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Iterator(mapper_signal *s, int n)
                { signals = s; size = n; }
            ~Iterator() {}
            bool operator==(const Iterator& rhs)
                { return (signals == rhs.signals && size == rhs.size); }
            bool operator!=(const Iterator& rhs)
                { return (signals != rhs.signals || size != rhs.size); }
            Iterator& operator++()
                { size++; return (*this); }
            Iterator operator++(int)
                { Iterator tmp(*this); size++; return tmp; }
            Signal operator*()
                { return Signal(signals[size]); }
            Iterator begin()
                { return Iterator(signals, 0); }
            Iterator end()
                { return Iterator(signals, size); }
        private:
            mapper_signal *signals;
            int size;
        };
    protected:
        mapper_signal signal;
        mapper_db_signal props;
    };

    class DeviceProps
    {
    // Reuse same class for device and database
    public:
        int found;
        DeviceProps(mapper_device dev)
            { device = dev; props = mdev_properties(device); found = 1; }
        DeviceProps(mapper_db_device dev_db)
            { device = 0; props = dev_db; found = dev_db ? 1 : 0; }
        operator mapper_db_device() const
            { return props; }
        void set(const string_type &name, int value)
            { if (device) mdev_set_property(device, name, 'i', &value, 1); }
        void set(const string_type &name, float value)
            { if (device) mdev_set_property(device, name, 'f', &value, 1); }
        void set(const string_type &name, double value)
            { if (device) mdev_set_property(device, name, 'd', &value, 1); }
        void set(const string_type &name, string_type &value)
            { if (device) mdev_set_property(device, name, 's',
                                            (void*)(const char *)value, 1); }
        void set(const string_type &name, int *value, int length)
            { if (device) mdev_set_property(device, name, 'i', value, length); }
        void set(const string_type &name, float *value, int length)
            { if (device) mdev_set_property(device, name, 'f', value, length); }
        void set(const string_type &name, double *value, int length)
            { if (device) mdev_set_property(device, name, 'd', value, length); }
        void set(const string_type &name, std::vector<int> value)
            { if (device) mdev_set_property(device, name, 'i', &value[0], value.size()); }
        void set(const string_type &name, std::vector<float> value)
            { if (device) mdev_set_property(device, name, 'f', &value[0], value.size()); }
        void set(const string_type &name, std::vector<double> value)
            { if (device) mdev_set_property(device, name, 'd', &value[0], value.size()); }
        // TODO: string array
        // TODO: string vector
        void remove(const string_type &name)
            { if (device) mdev_remove_property(device, name); }
        Property get(const string_type &name)
        {
            char type;
            const void *value;
            int length;
            mapper_db_device_property_lookup(props, name, &type, &value, &length);
            return Property(name, type, value, length);
        }
        Property get(int index)
        {
            const char *name;
            char type;
            const void *value;
            int length;
            mapper_db_device_property_index(props, index, &name, &type,
                                            &value, &length);
            return Property(name, type, value, length);
        }
        class Iterator : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Iterator(mapper_db_device *d)
                { dev = d; }
            ~Iterator()
                { mapper_db_device_done(dev); }
            operator mapper_db_device*() const
                { return dev; }
            bool operator==(const Iterator& rhs)
                { return (dev == rhs.dev); }
            bool operator!=(const Iterator& rhs)
                { return (dev != rhs.dev); }
            Iterator& operator++()
                {
                    if (dev != NULL)
                        dev = mapper_db_device_next(dev);
                    return (*this);
                }
            Iterator operator++(int)
                { Iterator tmp(*this); operator++(); return tmp; }
            DeviceProps operator*()
                { return DeviceProps(*dev); }
            Iterator begin()
                { return Iterator(dev); }
            Iterator end()
                { return Iterator(0); }
        private:
            mapper_db_device *dev;
        };
    protected:
        mapper_device device;
        mapper_db_device props;
    };

    class Device
    {
    public:
        Device(const string_type &name_prefix, int port, Admin admin)
            { device = mdev_new(name_prefix, port, admin); }
        Device(const string_type &name_prefix)
            { device = mdev_new(name_prefix, 0, 0); }
        ~Device()
            { if (device) mdev_free(device); }
        Signal add_input(const string_type &name, int length, char type,
                         const string_type &unit, void *minimum,
                         void *maximum, mapper_signal_update_handler handler,
                         void *user_data)
        {
            return Signal(mdev_add_input(device, name, length, type, unit,
                                         minimum, maximum, handler, user_data));
        }
        Signal add_output(const string_type &name, int length, char type,
                          const string_type &unit, void *minimum, void *maximum)
        {
            return Signal(mdev_add_output(device, name, length, type, unit,
                                          minimum, maximum));
        }
        void remove_input(Signal input)
            { if (input) mdev_remove_input(device, input); }
        void remove_output(Signal output)
            { if (output) mdev_remove_output(device, output); }
        int num_inputs() const
            { return mdev_num_inputs(device); }
        int num_outputs() const
            { return mdev_num_outputs(device); }
        int num_links_in() const
            { return mdev_num_links_in(device); }
        int num_links_out() const
            { return mdev_num_links_out(device); }
        int num_connections_in() const
            { return mdev_num_connections_in(device); }
        int num_connections_out() const
            { return mdev_num_connections_out(device); }
        Signal::Iterator inputs() const
        {
            return Signal::Iterator(mdev_get_inputs(device),
                                    mdev_num_inputs(device));
        }
        Signal inputs(const string_type &name, int* index=0) const
            { return Signal(mdev_get_input_by_name(device, name, index)); }
        Signal inputs(int index) const
            { return Signal(mdev_get_input_by_index(device, index)); }
        Signal::Iterator outputs() const
        {
            return Signal::Iterator(mdev_get_outputs(device),
                                    mdev_num_outputs(device));
        }
        Signal outputs(const string_type &name, int *index=0) const
            { return Signal(mdev_get_output_by_name(device, name, index)); }
        Signal outputs(int index) const
            { return Signal(mdev_get_output_by_index(device, index)); }
        DeviceProps properties() const
            { return DeviceProps(device); }
        int poll(int block_ms=0) const
            { return mdev_poll(device, block_ms); }
        int num_fds() const
            { return mdev_num_fds(device); }
        int get_fds(int *fds, int num) const
            { return mdev_get_fds(device, fds, num); }
        void service_fd(int fd)
            { mdev_service_fd(device, fd); }
        bool ready() const
            { return mdev_ready(device); }
        std::string name() const
            { return std::string(mdev_name(device)); }
        int id() const
            { return mdev_id(device); }
        int port() const
            { return mdev_port(device); }
        const struct in_addr *ip4() const
            { return mdev_ip4(device); }
        std::string interface() const
            { return mdev_interface(device); }
        int ordinal() const
            { return mdev_ordinal(device); }
        void start_queue(Timetag tt) const
            { mdev_start_queue(device, *tt); }
        void send_queue(Timetag tt) const
            { mdev_send_queue(device, *tt); }
//        lo::Server get_lo_server()
//            { return lo::Server(mdev_get_lo_server(device)); }
        void set_link_callback(mapper_device_link_handler handler, void *user_data)
            { mdev_set_link_callback(device, handler, user_data); }
        void set_connection_callback(mapper_device_connection_handler handler,
                                     void *user_data)
            { mdev_set_connection_callback(device, handler, user_data); }
        Timetag now()
        {
            mapper_timetag_t tt;
            mdev_now(device, &tt);
            return Timetag(tt);
        }
    protected:
        mapper_device device;
    };

    class LinkProps
    {
    public:
        int found;
        LinkProps(mapper_db_link link)
            { props = link; found = link ? 1 : 0; owned = 0; }
        LinkProps()
            {
                props = (mapper_db_link)calloc(1, sizeof(mapper_db_link_t));
                owned = 1;
            }
        ~LinkProps()
            { if (owned && props) free(props); }
        operator mapper_db_link() const
            { return props; }
        Property get(const string_type &name)
        {
            char type;
            const void *value;
            int length;
            mapper_db_link_property_lookup(props, name, &type, &value, &length);
            return Property(name, type, value, length);
        }
        Property get(int index)
        {
            const char *name;
            char type;
            const void *value;
            int length;
            mapper_db_link_property_index(props, index, &name, &type,
                                          &value, &length);
            return Property(name, type, value, length);
        }
        class Iterator : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Iterator(mapper_db_link *l)
                { link = l; }
            ~Iterator()
                { mapper_db_link_done(link); }
            bool operator==(const Iterator& rhs)
                { return (link == rhs.link); }
            bool operator!=(const Iterator& rhs)
                { return (link != rhs.link); }
            Iterator& operator++()
            {
                if (link != NULL)
                    link = mapper_db_link_next(link);
                return (*this);
            }
            Iterator operator++(int)
                { Iterator tmp(*this); operator++(); return tmp; }
            LinkProps operator*()
                { return LinkProps(*link); }
            Iterator begin()
                { return Iterator(link); }
            Iterator end()
                { return Iterator(0); }
        private:
            mapper_db_link *link;
        };
    protected:
        mapper_db_link props;
        int owned;
    };

    class ConnectionProps
    {
    public:
        int found;
        int flags;
        char src_type;
        char dest_type;
        int src_length;
        int dest_length;
        void *src_min;
        void *src_max;
        void *dest_min;
        void *dest_max;

        ConnectionProps(mapper_db_connection connection)
            { props = connection; found = connection ? 1 : 0; owned = 0; }
        ConnectionProps()
        {
            props = (mapper_db_connection)calloc(1, sizeof(mapper_db_connection_t));
            flags = 0;
            owned = 1;
        }
        ~ConnectionProps()
            { if (owned && props) free(props); }
        operator mapper_db_connection() const
        {
            return props;
        }
        void set_mode(mapper_mode_type mode)
            { props->mode = mode; flags |= CONNECTION_MODE; }
        void set_bound_min(mapper_boundary_action bound_min)
            { props->bound_min = bound_min; flags |= CONNECTION_BOUND_MIN; }
        void set_bound_max(mapper_boundary_action bound_max)
            { props->bound_max = bound_max; flags |= CONNECTION_BOUND_MAX; }
        void set_expression(const string_type &expression)
        {
            props->expression = (char*)(const char*)expression;
            flags |= CONNECTION_EXPRESSION;
        }
        void set_src_min(const Property &value)
        {
            props->src_min = (void*)(const void*)value;
            props->src_type = value.type;
            props->src_length = value.length;
            flags |= (CONNECTION_RANGE_SRC_MIN | CONNECTION_SRC_TYPE | CONNECTION_SRC_LENGTH);
        }
        void set_src_max(Property &value)
        {
            props->src_max = (void*)(const void*)value;
            props->src_type = value.type;
            props->src_length = value.length;
            flags |= (CONNECTION_RANGE_SRC_MAX | CONNECTION_SRC_TYPE | CONNECTION_SRC_LENGTH);
        }
        void set_dest_min(Property &value)
        {
            props->dest_min = (void*)(const void*)value;
            props->dest_type = value.type;
            props->dest_length = value.length;
            flags |= (CONNECTION_RANGE_DEST_MIN | CONNECTION_DEST_TYPE | CONNECTION_DEST_LENGTH);
        }
        void set_dest_max(Property &value)
        {
            props->dest_min = (void*)(const void*)value;
            props->dest_type = value.type;
            props->dest_length = value.length;
            flags |= (CONNECTION_RANGE_DEST_MAX | CONNECTION_DEST_TYPE | CONNECTION_DEST_LENGTH);
        }
        Property get(const string_type &name)
        {
            char type;
            const void *value;
            int length;
            mapper_db_connection_property_lookup(props, name, &type,
                                                 &value, &length);
            return Property(name, type, value, length);
        }
        Property get(int index)
        {
            const char *name;
            char type;
            const void *value;
            int length;
            mapper_db_connection_property_index(props, index, &name, &type,
                                                &value, &length);
            return Property(name, type, value, length);
        }
        class Iterator : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Iterator(mapper_db_connection *c)
                { con = c; }
            ~Iterator()
                { mapper_db_connection_done(con); }
            bool operator==(const Iterator& rhs)
                { return (con == rhs.con); }
            bool operator!=(const Iterator& rhs)
                { return (con != rhs.con); }
            Iterator& operator++()
            {
                if (con != NULL)
                    con = mapper_db_connection_next(con);
                return (*this);
            }
            Iterator operator++(int)
                { Iterator tmp(*this); operator++(); return tmp; }
            ConnectionProps operator*()
                { return ConnectionProps(*con); }
            Iterator begin()
                { return Iterator(con); }
            Iterator end()
                { return Iterator(0); }
        private:
            mapper_db_connection *con;
        };
    protected:
        mapper_db_connection props;
        int owned;
    };

    class Db
    {
    public:
        Db(mapper_monitor mon)
        {
            monitor = mon;
            db = mapper_monitor_get_db(mon);
        }
        ~Db()
        {}
        void flush()
        {
            mapper_monitor_flush_db(monitor,
                                    mapper_monitor_get_timeout(monitor), 0);
        }
        void flush(int timeout_sec, int quiet=0)
            { mapper_monitor_flush_db(monitor, timeout_sec, quiet); }

        // db_devices
        void add_device_callback(mapper_db_device_handler *handler,
                                 void *user_data)
            { mapper_db_add_device_callback(db, handler, user_data); }
        void remove_device_callback(mapper_db_device_handler *handler,
                                    void *user_data)
            { mapper_db_remove_device_callback(db, handler, user_data); }
        DeviceProps get_device(const string_type &name) const
            { return DeviceProps(mapper_db_get_device_by_name(db, name)); }
        DeviceProps get_device(uint32_t hash) const
            { return DeviceProps(mapper_db_get_device_by_name_hash(db, hash)); }
        DeviceProps::Iterator devices() const
            { return DeviceProps::Iterator(mapper_db_get_all_devices(db)); }
        DeviceProps::Iterator match_devices(const string_type &pattern) const
        {
            return DeviceProps::Iterator(
                mapper_db_match_devices_by_name(db, pattern));
        }

        // db_signals
        void add_signal_callback(mapper_db_signal_handler *handler,
                                 void *user_data)
            { mapper_db_add_signal_callback(db, handler, user_data); }
        void remove_signal_callback(mapper_db_signal_handler *handler,
                                    void *user_data)
            { mapper_db_remove_signal_callback(db, handler, user_data); }
        SignalProps get_input(const string_type &device_name,
                              const string_type &signal_name)
        {
            return SignalProps(
                mapper_db_get_input_by_device_and_signal_names(db, device_name,
                                                               signal_name));
        }
        SignalProps get_output(const string_type &device_name,
                               const string_type &signal_name)
        {
            return SignalProps(
                mapper_db_get_output_by_device_and_signal_names(db, device_name,
                                                                signal_name));
        }
        SignalProps::Iterator inputs() const
            { return SignalProps::Iterator(mapper_db_get_all_inputs(db)); }
        SignalProps::Iterator inputs(const string_type device_name) const
        {
            return SignalProps::Iterator(
                mapper_db_get_inputs_by_device_name(db, device_name));
        }
        SignalProps::Iterator match_inputs(const string_type device_name,
                                           const string_type pattern) const
        {
            return SignalProps::Iterator(
                 mapper_db_match_inputs_by_device_name(db, device_name, pattern));
        }
        SignalProps::Iterator outputs() const
            { return SignalProps::Iterator(mapper_db_get_all_outputs(db)); }
        SignalProps::Iterator outputs(const string_type device_name) const
        {
            return SignalProps::Iterator(
                 mapper_db_get_outputs_by_device_name(db, device_name));
        }
        SignalProps::Iterator match_outputs(const string_type device_name,
                                            const string_type pattern) const
        {
            return SignalProps::Iterator(
                 mapper_db_match_outputs_by_device_name(db, device_name, pattern));
        }

        // db_links
        void add_link_callback(mapper_db_link_handler *handler,
                               void *user_data)
            { mapper_db_add_link_callback(db, handler, user_data); }
        void remove_link_callback(mapper_db_link_handler *handler,
                                  void *user_data)
            { mapper_db_remove_link_callback(db, handler, user_data); }
        LinkProps::Iterator links() const
            { return LinkProps::Iterator(mapper_db_get_all_links(db)); }
        LinkProps::Iterator links(const string_type &device_name) const
        {
            return LinkProps::Iterator(
                mapper_db_get_links_by_device_name(db, device_name));
        }
        LinkProps::Iterator links_by_src(const string_type &device_name) const
        {
            return LinkProps::Iterator(
                mapper_db_get_links_by_src_device_name(db, device_name));
        }
        LinkProps::Iterator links_by_dest(const string_type &device_name) const
        {
            return LinkProps::Iterator(
                mapper_db_get_links_by_dest_device_name(db, device_name));
        }
        LinkProps get_link(const string_type &source_device,
                           const string_type &dest_device)
        {
            return LinkProps(
                mapper_db_get_link_by_src_dest_names(db, source_device,
                                                     dest_device));
        }
        LinkProps::Iterator links(DeviceProps::Iterator src_list,
                                  DeviceProps::Iterator dest_list) const
        {
            // TODO: check that this works!
            return LinkProps::Iterator(
                mapper_db_get_links_by_src_dest_devices(db,
                    (mapper_db_device*)(src_list),
                    (mapper_db_device*)(dest_list)));
        }

        // db connections
        void add_connection_callback(mapper_db_connection_handler *handler,
                                     void *user_data)
            { mapper_db_add_connection_callback(db, handler, user_data); }
        void remove_connection_callback(mapper_db_connection_handler *handler,
                                        void *user_data)
            { mapper_db_remove_connection_callback(db, handler, user_data); }
        ConnectionProps::Iterator connections() const
        {
            return ConnectionProps::Iterator(
                 mapper_db_get_all_connections(db));
        }
        ConnectionProps::Iterator connections(const string_type &src_device,
                                              const string_type &src_signal,
                                              const string_type &dest_device,
                                              const string_type &dest_signal) const
        {
            return ConnectionProps::Iterator(
                mapper_db_get_connections_by_device_and_signal_names(db,
                     src_device, src_signal, dest_device, dest_signal));
        }
        ConnectionProps::Iterator connections(const string_type &device_name) const
        {
            return ConnectionProps::Iterator(
                 mapper_db_get_connections_by_device_name(db, device_name));
        }
        ConnectionProps::Iterator connections_by_src(const string_type &signal_name) const
        {
            return ConnectionProps::Iterator(
                 mapper_db_get_connections_by_src_signal_name(db, signal_name));
        }
        ConnectionProps::Iterator connections_by_src(const string_type &device_name,
                                                     const string_type &signal_name) const
        {
            return ConnectionProps::Iterator(
                 mapper_db_get_connections_by_src_device_and_signal_names(db,
                      device_name, signal_name));
        }
        ConnectionProps::Iterator connections_by_dest(const string_type &signal_name) const
        {
            return ConnectionProps::Iterator(
                 mapper_db_get_connections_by_dest_signal_name(db, signal_name));
        }
        ConnectionProps::Iterator connections_by_dest(const string_type &device_name,
                                                      const string_type &signal_name) const
        {
            return ConnectionProps::Iterator(
                 mapper_db_get_connections_by_dest_device_and_signal_names(db,
                      device_name, signal_name));
        }
        ConnectionProps connection_by_signals(const string_type &source_name,
                                              const string_type &dest_name) const
        {
            return ConnectionProps(
                mapper_db_get_connection_by_signal_full_names(db, source_name,
                                                              dest_name));
        }
        ConnectionProps::Iterator connection_by_devices(const string_type &source_name,
                                                        const string_type &dest_name) const
        {
            return ConnectionProps::Iterator(
                mapper_db_get_connections_by_src_dest_device_names(db,
                                                                   source_name,
                                                                   dest_name));
        }
        ConnectionProps::Iterator connections(SignalProps::Iterator src_list,
                                              SignalProps::Iterator dest_list) const
        {
            return ConnectionProps::Iterator(
                 mapper_db_get_connections_by_signal_queries(db,
                     (mapper_db_signal*)(src_list),
                     (mapper_db_signal*)(dest_list)));
        }
    protected:
        mapper_db db;
        mapper_monitor monitor;
    };

    class Monitor
    {
    public:
        Monitor()
            { monitor = mapper_monitor_new(0, 0); }
        Monitor(Admin admin, int autosubscribe_flags=0)
            { monitor = mapper_monitor_new(admin, autosubscribe_flags); }
        Monitor(int autosubscribe_flags)
            { monitor = mapper_monitor_new(0, autosubscribe_flags); }
        ~Monitor()
            { if (monitor) mapper_monitor_free(monitor); }
        int poll(int block_ms=0)
            { return mapper_monitor_poll(monitor, block_ms); }
        Db db() const
            { return Db(monitor); }
        void request_devices() const
            { mapper_monitor_request_devices(monitor); }
        void subscribe(const string_type &device_name, int flags, int timeout)
            { mapper_monitor_subscribe(monitor, device_name, flags, timeout); }
        void unsubscribe(const string_type &device_name)
            { mapper_monitor_unsubscribe(monitor, device_name); }
        void autosubscribe(int flags) const
            { mapper_monitor_autosubscribe(monitor, flags); }
        void link(const string_type &source_device, const string_type &dest_device)
            { mapper_monitor_link(monitor, source_device, dest_device, 0, 0); }
//        void link(const string_type &source_device, const string_type &dest_device,
//             const LinkProps &properties)
//        {
//            mapper_monitor_link(monitor, source_device, dest_device,
//                                mapper_db_link(properties), properties.flags);
//        }
        void unlink(const string_type &source_device, const string_type &dest_device)
            { mapper_monitor_unlink(monitor, source_device, dest_device); }
        void connect(const string_type &source_signal, const string_type &dest_signal)
            { mapper_monitor_connect(monitor, source_signal, dest_signal, 0, 0); }
        void connect(const string_type &source_signal,
                     const string_type &dest_signal,
                     const ConnectionProps &properties) const
        {
            mapper_monitor_connect(monitor, source_signal, dest_signal,
                                   mapper_db_connection(properties),
                                   properties.flags);
        }
        void connection_modify(const string_type &source_signal,
                               const string_type &dest_signal,
                               ConnectionProps &properties) const
        {
            mapper_monitor_connection_modify(monitor, source_signal, dest_signal,
                                             mapper_db_connection(properties),
                                             properties.flags);
        }
        void disconnect(const string_type &source_signal, const string_type &dest_signal)
            { mapper_monitor_disconnect(monitor, source_signal, dest_signal); }
    protected:
        mapper_monitor monitor;
    };
};

#endif // _MAPPER_CPP_H_