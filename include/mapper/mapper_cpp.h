
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

    class Property;
    class AbstractObjectProps;
    class Db;

    // Helper classes to allow polymorphism on "const char *",
    // "std::string", and "int".
    class string_type {
    public:
        string_type(const char *s=0) { _s = s; }
        string_type(const std::string &s) { _s = s.c_str(); }
        operator const char*() const { return _s; }
        const char *_s;
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
    private:
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
    private:
        mapper_timetag_t timetag;
    };

    class AbstractProps
    {
    protected:
        friend class Property;
        virtual void set(mapper::Property *p) = 0;
    public:
        virtual AbstractProps& set(mapper::Property p) = 0;
        virtual AbstractProps& remove(const string_type &name) = 0;
    };

    class Property
    {
    public:
        template <typename T>
        Property(const string_type &_name, T _value)
            { name = _name; _set(_value); parent = NULL; owned = 0; }
        template <typename T>
        Property(const string_type &_name, T& _value, int _length)
            { name = _name; _set(_value, _length); parent = NULL; owned = 0; }
        template <typename T>
        Property(const string_type &_name, std::vector<T> _value)
            { name = _name; _set(_value); parent = NULL; owned = 0; }
        template <typename T>
        Property(const string_type &_name, char _type, T& _value, int _length)
            { name = _name; _set(_type, _value, _length); parent = NULL; owned = 0; }

        ~Property()
            { maybe_free(); }

        template <typename T>
        void set(T _value)
            { maybe_free(); _set(_value); if (parent) parent->set(this); }
        template <typename T>
        void set(T& _value, int _length)
            { maybe_free(); _set(_value, _length); if (parent) parent->set(this); }
        template <typename T>
        void set(std::vector<T> _value)
            { maybe_free(); _set(_value); if (parent) parent->set(this); }

        operator const void*() const
            { return value; }
        const char *str()
            { return (const char*)value; }
        const int *i()
            { return (const int*)value; }
        const float *f()
            { return (const float*)value; }
        const double *d()
            { return (const double*)value; }
        void print()
        {
            printf("%s: ", name ?: "unknown");
            mapper_prop_pp(type, length, value);
        }
        const char *name;
        char type;
        int length;
        const void *value;
    protected:
        friend class AbstractDeviceProps;
        friend class AbstractSignalProps;
        friend class Db;
        Property(const string_type &_name, char _type, const void *_value,
                 int _length, const AbstractObjectProps *_parent)
        {
            name = _name;
            _set(_type, _value, _length);
            parent = (AbstractProps*)_parent;
            owned = 0;
        }
        Property()
            { name = 0; type = 0; length = 0; owned = 0; }
    private:
        union {
            double _d;
            float _f;
            int _i;
            char _c;
        };
        int owned;

        void maybe_free()
            { if (owned && value) free((void*)value); owned = 0; }
        void _set(int _value)
            { _i = _value; length = 1; type = 'i'; value = &_i; }
        void _set(float _value)
            { _f = _value; length = 1; type = 'f'; value = &_f; }
        void _set(double _value)
            { _d = _value; length = 1; type = 'd'; value = &_d; }
        void _set(char _value)
            { _c = _value; length = 1; type = 'c'; value = &_c; }
        void _set(const string_type &_value)
            { value = _value; length = 1; type = 's'; }
        void _set(int _value[], int _length)
            { value = _value; length = _length; type = 'i'; }
        void _set(float _value[], int _length)
            { value = _value; length = _length; type = 'f'; }
        void _set(double _value[], int _length)
            { value = _value; length = _length; type = 'd'; }
        void _set(char _value[], int _length)
            { value = _value; length = _length; type = 'c'; }
        void _set(const char *_value[], int _length)
            { value = _value; length = _length; type = 's'; }
        template <size_t N>
        void _set(std::array<int, N>& _value)
        {
            if (!_value.empty()) {
                value = _value.data();
                length = N;
                type = 'i';
            }
            else
                length = 0;
        }
        template <size_t N>
        void _set(std::array<float, N>& _value)
        {
            if (!_value.empty()) {
                value = _value.data();
                length = N;
                type = 'f';
            }
            else
                length = 0;
        }
        template <size_t N>
        void _set(std::array<double, N>& _value)
        {
            if (!_value.empty()) {
                value = _value.data();
                length = N;
                type = 'd';
            }
            else
                length = 0;
        }
        template <size_t N>
        void _set(std::array<char, N>& _value)
        {
            if (!_value.empty()) {
                value = _value.data();
                length = N;
                type = 'c';
            }
            else
                length = 0;
        }
        template <size_t N>
        void _set(std::array<const char*, N>& _value)
        {
            if (!_value.empty()) {
                value = _value.data();
                length = N;
                type = 's';
            }
            else
                length = 0;
        }
        template <size_t N>
        void _set(std::array<std::string, N>& _values)
        {
            length = N;
            type = 's';
            if (length == 1) {
                value = _values[0].c_str();
            }
            else if (length > 1) {
                // need to copy string array
                char **temp = (char**)malloc(sizeof(char*) * length);
                for (int i = 0; i < length; i++) {
                    temp[i] = (char*)_values[i].c_str();
                }
                value = temp;
                owned = 1;
            }
        }
        void _set(std::string _values[], int _length)
        {
            length = _length;
            type = 's';
            if (length == 1) {
                value = _values[0].c_str();
            }
            else if (length > 1) {
                // need to copy string array
                value = malloc(sizeof(char*) * length);
                for (int i = 0; i < length; i++) {
                    ((char**)value)[i] = (char*)_values[i].c_str();
                }
                owned = 1;
            }
        }
        void _set(std::vector<int> _value)
            { value = _value.data(); length = _value.size(); type = 'i'; }
        void _set(std::vector<float> _value)
            { value = _value.data(); length = _value.size(); type = 'f'; }
        void _set(std::vector<double> _value)
            { value = _value.data(); length = _value.size(); type = 'd'; }
        void _set(std::vector<char> _value)
            { value = _value.data(); length = _value.size(); type = 'c'; }
        void _set(std::vector<const char*>& _value)
            { value = _value.data(); length = _value.size(); type = 's'; }
        void _set(std::vector<std::string>& _value)
        {
            length = _value.size();
            type = 's';
            if (length == 1) {
                value = _value[0].c_str();
            }
            else if (length > 1) {
                // need to copy string array
                value = malloc(sizeof(char*) * length);
                for (int i = 0; i < length; i++) {
                    ((char**)value)[i] = (char*)_value[i].c_str();
                }
                owned = 1;
            }
        }
        void _set(char _type, const void *_value, int _length)
        {
            type = _type;
            value = _value;
            length = _length;
        }
        AbstractProps *parent;
    };

    class AbstractObjectProps : public AbstractProps
    {
    protected:
        virtual void set(mapper::Property *p) = 0;
    public:
        virtual AbstractObjectProps& set(mapper::Property p) = 0;
        virtual Property get(const string_type &name) const = 0;

        Property operator [] (const string_type key)
            { return get(key); }

        template <typename T>
        AbstractObjectProps& set(const string_type &_name, T _value)
            { set(Property(_name, _value)); return (*this); }
        template <typename T>
        AbstractObjectProps& set(const string_type &_name, T& _value, int _length)
            { set(Property(_name, _value, _length)); return (*this); }
        template <typename T>
        AbstractObjectProps& set(const string_type &_name, std::vector<T> _value)
            { set(Property(_name, _value)); return (*this); }
        template <typename T>
        AbstractObjectProps& set(const string_type &_name, char _type, T& _value, int _length)
            { set(Property(_name, _type, _value, _length)); return (*this); }
    };

    class AbstractSignalProps : public AbstractObjectProps
    {
    // Reuse class for signal and database
    protected:
        friend class Property;

        AbstractSignalProps(mapper_signal sig)
            { signal = sig; props = msig_properties(signal); }
        AbstractSignalProps(mapper_db_signal sig_db)
            { signal = 0; props = sig_db; }
        void set(mapper::Property *p)
        {
            if (signal)
                msig_set_property(signal, p->name, p->type,
                                  p->type == 's' && p->length == 1
                                  ? (void*)&p->value : (void*)p->value,
                                  p->length);
        }

    private:
        mapper_signal signal;
        mapper_db_signal props;

    public:
        operator mapper_db_signal() const
            { return props; }
        using AbstractObjectProps::set;
        AbstractSignalProps& set(mapper::Property p)
            { set(&p); return (*this); }
        AbstractSignalProps& remove(const string_type &name)
            { if (signal) msig_remove_property(signal, name); return (*this); }
        Property get(const string_type &name) const
        {
            char type;
            const void *value;
            int length;
            if (!mapper_db_signal_property_lookup(props, name, &type,
                                                  &value, &length))
                return Property(name, type, value, length, this);
            else
                return Property();
        }
        Property get(int index) const
        {
            const char *name;
            char type;
            const void *value;
            int length;
            if (!mapper_db_signal_property_index(props, index, &name, &type,
                                                 &value, &length))
                return Property(name, type, value, length, this);
            else
                return Property();
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
            AbstractSignalProps operator*()
                { return AbstractSignalProps(*sig); }
            Iterator begin()
                { return Iterator(sig); }
            Iterator end()
                { return Iterator(0); }
        private:
            mapper_db_signal *sig;
        };
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
        Signal update(void *value, int count)
            { msig_update(signal, value, count, MAPPER_NOW); return (*this); }
        Signal update(void *value, int count, Timetag tt)
            { msig_update(signal, value, count, *tt); return (*this); }
        Signal update(int value)
            { msig_update_int(signal, value); return (*this); }
        Signal update(float value)
            { msig_update_float(signal, value); return (*this); }
        Signal update(double value)
            { msig_update_double(signal, value); return (*this); }
        Signal update(int *value, int count=0)
        {
            if (props->type == 'i')
                msig_update(signal, value, count, MAPPER_NOW);
            return (*this);
        }
        Signal update(float *value, int count=0)
        {
            if (props->type == 'f')
                msig_update(signal, value, count, MAPPER_NOW);
            return (*this);
        }
        Signal update(double *value, int count=0)
        {
            if (props->type == 'd')
                msig_update(signal, value, count, MAPPER_NOW);
            return (*this);
        }
        Signal update(int *value, Timetag tt)
        {
            if (props->type == 'i')
                msig_update(signal, value, 1, *tt);
            return (*this);
        }
        Signal update(float *value, Timetag tt)
        {
            if (props->type == 'f')
                msig_update(signal, value, 1, *tt);
            return (*this);
        }
        Signal update(double *value, Timetag tt)
        {
            if (props->type == 'd')
                msig_update(signal, value, 1, *tt);
            return (*this);
        }
        Signal update(int *value, int count, Timetag tt)
        {
            if (props->type == 'i')
                msig_update(signal, value, count, *tt);
            return (*this);
        }
        Signal update(float *value, int count, Timetag tt)
        {
            if (props->type == 'f')
                msig_update(signal, value, count, *tt);
            return (*this);
        }
        Signal update(double *value, int count, Timetag tt)
        {
            if (props->type == 'd')
                msig_update(signal, value, count, *tt);
            return (*this);
        }
        Signal update(std::vector <int> value)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, MAPPER_NOW);
            return (*this);
        }
        Signal update(std::vector <float> value)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, MAPPER_NOW);
            return (*this);
        }
        Signal update(std::vector <double> value)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, MAPPER_NOW);
            return (*this);
        }
        Signal update(std::vector <int> value, Timetag tt)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, *tt);
            return (*this);
        }
        Signal update(std::vector <float> value, Timetag tt)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, *tt);
            return (*this);
        }
        Signal update(std::vector <double> value, Timetag tt)
        {
            msig_update(signal, &value[0],
                        value.size() / props->length, *tt);
            return (*this);
        }
        Signal update_instance(int instance_id, void *value, int count)
        {
            msig_update_instance(signal, instance_id, value, count, MAPPER_NOW);
            return (*this);
        }
        Signal update_instance(int instance_id, void *value, int count, Timetag tt)
        {
            msig_update_instance(signal, instance_id, value, count, *tt);
            return (*this);
        }
        Signal update_instance(int instance_id, int value)
        {
            msig_update_instance(signal, instance_id, &value, 1, MAPPER_NOW);
            return (*this);
        }
        Signal update_instance(int instance_id, float value)
        {
            msig_update_instance(signal, instance_id, &value, 1, MAPPER_NOW);
            return (*this);
        }
        Signal update_instance(int instance_id, double value)
        {
            msig_update_instance(signal, instance_id, &value, 1, MAPPER_NOW);
            return (*this);
        }
        Signal update_instance(int instance_id, int *value, int count=0)
        {
            if (props->type == 'i')
                msig_update_instance(signal, instance_id, value, count, MAPPER_NOW);
            return (*this);
        }
        Signal update_instance(int instance_id, float *value, int count=0)
        {
            if (props->type == 'f')
                msig_update_instance(signal, instance_id, value, count, MAPPER_NOW);
            return (*this);
        }
        Signal update_instance(int instance_id, double *value, int count=0)
        {
            if (props->type == 'd')
                msig_update_instance(signal, instance_id, value, count, MAPPER_NOW);
            return (*this);
        }
        Signal update_instance(int instance_id, int *value, Timetag tt)
        {
            if (props->type == 'i')
                msig_update_instance(signal, instance_id, value, 1, *tt);
            return (*this);
        }
        Signal update_instance(int instance_id, float *value, Timetag tt)
        {
            if (props->type == 'f')
                msig_update_instance(signal, instance_id, value, 1, *tt);
            return (*this);
        }
        Signal update_instance(int instance_id, double *value, Timetag tt)
        {
            if (props->type == 'd')
                msig_update_instance(signal, instance_id, value, 1, *tt);
            return (*this);
        }
        Signal update_instance(int instance_id, int *value, int count, Timetag tt)
        {
            if (props->type == 'i')
                msig_update_instance(signal, instance_id, value, count, *tt);
            return (*this);
        }
        Signal update_instance(int instance_id, float *value, int count, Timetag tt)
        {
            if (props->type == 'f')
                msig_update_instance(signal, instance_id, value, count, *tt);
            return (*this);
        }
        Signal update_instance(int instance_id, double *value, int count, Timetag tt)
        {
            if (props->type == 'd')
                msig_update_instance(signal, instance_id, value, count, *tt);
            return (*this);
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
        Signal reserve_instances(int num)
            { msig_reserve_instances(signal, num, 0, 0); return (*this); }
        Signal reserve_instances(int num, int *instance_ids, void **user_data)
        {
            msig_reserve_instances(signal, num, instance_ids, user_data);
            return (*this);
        }
        Signal release_instance(int instance_id)
        {
            msig_release_instance(signal, instance_id, MAPPER_NOW);
            return (*this);
        }
        Signal release_instance(int instance_id, Timetag tt)
            { msig_release_instance(signal, instance_id, *tt); return (*this); }
        Signal remove_instance(int instance_id)
            { msig_remove_instance(signal, instance_id); return (*this); }
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
        Signal set_instance_allocation_mode(mapper_instance_allocation_type mode)
            { msig_set_instance_allocation_mode(signal, mode); return (*this); }
        mapper_instance_allocation_type get_instance_allocation_mode() const
            { return msig_get_instance_allocation_mode(signal); }
        Signal set_instance_event_callback(mapper_signal_instance_event_handler h,
                                         int flags, void *user_data)
        {
            msig_set_instance_event_callback(signal, h, flags, user_data);
            return (*this);
        }
        Signal set_instance_data(int instance_id, void *user_data)
        {
            msig_set_instance_data(signal, instance_id, user_data);
            return (*this);
        }
        void *instance_data(int instance_id) const
            { return msig_get_instance_data(signal, instance_id); }
        Signal set_callback(mapper_signal_update_handler *handler, void *user_data)
        {
            msig_set_callback(signal, handler, user_data);
            return (*this);
        }
        int num_connections() const
            { return msig_num_connections(signal); }
        std::string full_name() const
        {
            char str[64];
            msig_full_name(signal, str, 64);
            return std::string(str);
        }
        Signal set_minimum(void *value)
            { msig_set_minimum(signal, value); return (*this); }
        Signal set_maximum(void *value)
            { msig_set_maximum(signal, value); return (*this); }
        Signal set_rate(int rate)
            { msig_set_rate(signal, rate); return (*this); }
        Signal set_direction(mapper_direction_t direction)
            { msig_set_direction(signal, direction); return (*this); }
        class Properties : public AbstractSignalProps
        {
        public:
            Properties(mapper_signal s) : AbstractSignalProps(s) {}
        };
        Properties properties() const
            { return Properties(signal); }
        Property property(const string_type name)
            { return Properties(signal).get(name); }
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
    private:
        mapper_signal signal;
        mapper_db_signal props;
    };

    class AbstractDeviceProps : public AbstractObjectProps
    {
    // Reuse same class for device and database
    protected:
        friend class Property;

        AbstractDeviceProps(mapper_device dev) : AbstractObjectProps()
            { device = dev; props = mdev_properties(device); }
        AbstractDeviceProps(mapper_db_device dev_db)
            { device = 0; props = dev_db; }
        void set(mapper::Property *p)
        {
            if (device)
                mdev_set_property(device, p->name, p->type,
                                  p->type == 's' && p->length == 1 ?
                                  (void*)&p->value : (void*)p->value,
                                  p->length);
        }

    private:
        mapper_device device;
        mapper_db_device props;

    public:
        operator mapper_db_device() const
            { return props; }
        using AbstractObjectProps::set;
        AbstractDeviceProps& set(mapper::Property p)
            { set(&p); return (*this); }
        AbstractDeviceProps& remove(const string_type &name)
            { if (device) mdev_remove_property(device, name); return (*this); }
        Property get(const string_type &name) const
        {
            char type;
            const void *value;
            int length;
            if (!mapper_db_device_property_lookup(props, name, &type,
                                                  &value, &length))
                return Property(name, type, value, length, this);
            else
                return Property();
        }
        Property get(int index) const
        {
            const char *name;
            char type;
            const void *value;
            int length;
            if (!mapper_db_device_property_index(props, index, &name, &type,
                                                 &value, &length))
                return Property(name, type, value, length, this);
            else
                return Property();
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
            AbstractDeviceProps operator*()
                { return AbstractDeviceProps(*dev); }
            Iterator begin()
                { return Iterator(dev); }
            Iterator end()
                { return Iterator(0); }
        private:
            mapper_db_device *dev;
        };
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
        operator mapper_device() const
            { return device; }
        Signal add_input(const string_type &name, int length, char type,
                         const string_type &unit, void *minimum,
                         void *maximum, mapper_signal_update_handler handler,
                         void *user_data)
        {
            return Signal(mdev_add_input(device, name, length, type, unit,
                                         minimum, maximum, handler, user_data));
        }
        Signal add_output(const string_type &name, int length, char type,
                          const string_type &unit, void *minimum=0, void *maximum=0)
        {
            return Signal(mdev_add_output(device, name, length, type, unit,
                                          minimum, maximum));
        }
        Device& remove_input(Signal input)
            { if (input) mdev_remove_input(device, input); return (*this); }
        Device& remove_input(const string_type &name)
        {
            if (name) {
                mapper_signal input = mdev_get_input_by_name(device, name, 0);
                mdev_remove_input(device, input);
            }
            return (*this);
        }
        Device& remove_output(Signal output)
            { if (output) mdev_remove_output(device, output); return (*this); }
        Device& remove_output(const string_type &name)
        {
            if (name) {
                mapper_signal output = mdev_get_output_by_name(device, name, 0);
                mdev_remove_output(device, output);
            }
            return (*this);
        }
        int num_inputs() const
            { return mdev_num_inputs(device); }
        int num_outputs() const
            { return mdev_num_outputs(device); }
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
        class Properties : public AbstractDeviceProps
        {
        public:
            Properties(mapper_device d) : AbstractDeviceProps(d) {}
        };
        Properties properties() const
            { return Properties(device); }
        Property property(const string_type name)
            { return Properties(device).get(name); }
        int poll(int block_ms=0) const
            { return mdev_poll(device, block_ms); }
        int num_fds() const
            { return mdev_num_fds(device); }
        int fds(int *fds, int num) const
            { return mdev_get_fds(device, fds, num); }
        Device& service_fd(int fd)
            { mdev_service_fd(device, fd); return (*this); }
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
        Device& set_connection_callback(mapper_device_connection_handler handler,
                                        void *user_data)
        {
            mdev_set_connection_callback(device, handler, user_data);
            return (*this);
        }
        Timetag now()
        {
            mapper_timetag_t tt;
            mdev_now(device, &tt);
            return Timetag(tt);
        }
    private:
        mapper_device device;
    };

    class Db
    {
    public:
        Db(mapper_monitor mon)
        {
            monitor = mon;
            db = mmon_get_db(mon);
        }
        ~Db()
        {}
        Db& flush()
        {
            mmon_flush_db(monitor, mmon_get_timeout(monitor), 0);
            return (*this);
        }
        Db& flush(int timeout_sec, int quiet=0)
        {
            mmon_flush_db(monitor, timeout_sec, quiet);
            return (*this);
        }
        // db_devices
        Db& add_device_callback(mapper_db_device_handler *handler, void *user_data)
        {
            mapper_db_add_device_callback(db, handler, user_data);
            return (*this);
        }
        Db& remove_device_callback(mapper_db_device_handler *handler,
                                    void *user_data)
        {
            mapper_db_remove_device_callback(db, handler, user_data);
            return (*this);
        }

        class Device : public AbstractDeviceProps
        {
        public:
            Device(mapper_db_device d) : AbstractDeviceProps(d) {}
        };

        Device device(const string_type &name) const
            { return Device(mapper_db_get_device_by_name(db, name)); }
        Device device(uint32_t hash) const
            { return Device(mapper_db_get_device_by_name_hash(db, hash)); }
        Device::Iterator devices() const
            { return Device::Iterator(mapper_db_get_all_devices(db)); }
        Device::Iterator devices(const string_type &pattern) const
        {
            return Device::Iterator(
                mapper_db_match_devices_by_name(db, pattern));
        }

        // db_signals
        Db& add_signal_callback(mapper_db_signal_handler *handler,
                                void *user_data)
        {
            mapper_db_add_signal_callback(db, handler, user_data);
            return (*this);
        }
        Db& remove_signal_callback(mapper_db_signal_handler *handler,
                                   void *user_data)
        {
            mapper_db_remove_signal_callback(db, handler, user_data);
            return (*this);
        }

        class Signal : public AbstractSignalProps
        {
        public:
            Signal(mapper_db_signal s) : AbstractSignalProps(s) {}
        };

        Signal input(const string_type &device_name,
                     const string_type &signal_name)
        {
            return Signal(
                mapper_db_get_input_by_device_and_signal_names(db, device_name,
                                                               signal_name));
        }
        Signal output(const string_type &device_name,
                      const string_type &signal_name)
        {
            return Signal(
                mapper_db_get_output_by_device_and_signal_names(db, device_name,
                                                                signal_name));
        }
        Signal::Iterator inputs() const
            { return Signal::Iterator(mapper_db_get_all_inputs(db)); }
        Signal::Iterator inputs(const string_type device_name) const
        {
            return Signal::Iterator(
                mapper_db_get_inputs_by_device_name(db, device_name));
        }
        Signal::Iterator match_inputs(const string_type device_name,
                                      const string_type pattern) const
        {
            return Signal::Iterator(
                 mapper_db_match_inputs_by_device_name(db, device_name, pattern));
        }
        Signal::Iterator outputs() const
            { return Signal::Iterator(mapper_db_get_all_outputs(db)); }
        Signal::Iterator outputs(const string_type device_name) const
        {
            return Signal::Iterator(
                 mapper_db_get_outputs_by_device_name(db, device_name));
        }
        Signal::Iterator match_outputs(const string_type device_name,
                                       const string_type pattern) const
        {
            return Signal::Iterator(
                 mapper_db_match_outputs_by_device_name(db, device_name, pattern));
        }

        // db connections
        Db& add_connection_callback(mapper_db_connection_handler *handler,
                                    void *user_data)
        {
            mapper_db_add_connection_callback(db, handler, user_data);
            return (*this);
        }
        Db& remove_connection_callback(mapper_db_connection_handler *handler,
                                       void *user_data)
        {
            mapper_db_remove_connection_callback(db, handler, user_data);
            return (*this);
        }
        class Connection : AbstractObjectProps
        {
        public:
            Connection(mapper_db_connection connection)
            {
                props = connection;
                if (props) {
                    _sources = new Slot[props->num_sources];
                    for (int i = 0; i < props->num_sources; i++) {
                        _sources[i] = Slot(this, &props->sources[i], 1);
                    }
                    _destination = Slot(this, &props->destination, 0);
                }
                else
                    _sources = 0;
                owned = 0;
            }
            Connection(int num_sources=1)
            {
                props = ((mapper_db_connection)
                         calloc(1, sizeof(mapper_db_connection_t)));
                props->num_sources = num_sources;
                props->sources = ((mapper_db_connection_slot)
                                  calloc(1,
                                         sizeof(struct _mapper_db_connection_slot)
                                         * num_sources));
                _sources = new Slot[num_sources];
                for (int i = 0; i < num_sources; i++) {
                    _sources[i] = Slot(this, &props->sources[i], 1);
                }
                _destination = Slot(this, &props->destination, 0);
                owned = 1;
            }
            ~Connection()
            {
//                delete _sources;
                if (owned && props) {
                    if (props->sources)
                        free(props->sources);
                    free(props);
                }
            }
            operator mapper_db_connection() const
                { return props; }
            int num_sources() const
                { return props->num_sources; }
            Connection& set_mode(mapper_mode_type mode)
            {
                props->mode = mode;
                props->flags |= CONNECTION_MODE;
                return (*this);
            }
            Connection& set_expression(const string_type &expression)
            {
                props->expression = (char*)(const char*)expression;
                props->flags |= CONNECTION_EXPRESSION;
                return (*this);
            }
            Property get(const string_type &name) const
            {
                char type;
                const void *value;
                int length;
                if (!mapper_db_connection_property_lookup(props, name, &type,
                                                          &value, &length))
                    return Property(name, type, value, length);
                else
                    return Property();
            }
            Property get(int index) const
            {
                const char *name;
                char type;
                const void *value;
                int length;
                if (!mapper_db_connection_property_index(props, index, &name,
                                                         &type, &value, &length))
                    return Property(name, type, value, length);
                else
                    return Property();
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
                Connection operator*()
                    { return Connection(*con); }
                Iterator begin()
                    { return Iterator(con); }
                Iterator end()
                    { return Iterator(0); }
            private:
                mapper_db_connection *con;
            };
            class Slot : AbstractObjectProps
            {
            public:
                ~Slot() {}
                Device device() const
                {
                    return Device(props->device);
                }
                Signal signal() const
                {
                    return Signal(props->signal);
                }
                Slot& set_bound_min(mapper_boundary_action bound_min)
                {
                    props->bound_min = bound_min;
                    props->flags |= CONNECTION_BOUND_MIN;
                    return (*this);
                }
                Slot& set_bound_max(mapper_boundary_action bound_max)
                {
                    props->bound_max = bound_max;
                    props->flags |= CONNECTION_BOUND_MAX;
                    return (*this);
                }
                Slot& set_minimum(const Property &value)
                {
                    if (props) {
                        props->minimum = (void*)(const void*)value;
                        props->type = value.type;
                        props->length = value.length;
                        props->flags |= CONNECTION_MIN_KNOWN;
                    }
                    return (*this);
                }
                Slot& set_maximum(const Property &value)
                {
                    if (props) {
                        props->maximum = (void*)(const void*)value;
                        props->type = value.type;
                        props->length = value.length;
                        props->flags |= CONNECTION_MAX_KNOWN;
                    }
                    return (*this);
                }
                Slot& set_cause_update(bool value)
                {
                    if (props)
                        props->cause_update = (int)value;
                    return (*this);
                }
                Slot& set_send_as_instance(bool value)
                {
                    if (props)
                        props->send_as_instance = (int)value;
                    return (*this);
                }
                Property get(const string_type &name) const
                {
                    if (!props)
                        return Property();
                    char type;
                    const void *value;
                    int length;
                    if (!mapper_db_connection_slot_property_lookup(props, name,
                                                                   &type, &value,
                                                                   &length))
                        return Property(name, type, value, length);
                    else
                        return Property();
                }
            protected:
                friend class Connection;
                Slot(const Connection *_parent, mapper_db_connection_slot slot,
                     int _is_src)
                {
                    props = slot;
                    parent = _parent;
                    is_src = _is_src;
                    flags = 0;
                }
                Slot()
                    { props = 0; parent = 0; flags = 0; }
                operator mapper_db_connection_slot() const
                    { return props; }
                void set(mapper::Property *p) {}
                Slot& set(mapper::Property p) { return (*this); }
                Slot& remove(const string_type &name) { return (*this); }
                int flags;
            private:
                mapper_db_connection_slot props;
                const Connection *parent;
                int is_src;
            };
            Slot destination() const
                { return _destination; }
            Slot source(int index=0) const
                { return _sources[index]; }
        protected:
            friend class Monitor;
            void set(mapper::Property *p) {}
            Connection& set(Property p) { set(&p); return (*this); }
            Connection& remove(const string_type &name) { return (*this); }
        private:
            mapper_db_connection props;
            Slot _destination;
            Slot *_sources;
            int owned;
        };
        Connection::Iterator connections() const
        {
            return Connection::Iterator(
                 mapper_db_get_all_connections(db));
        }
        Connection::Iterator connections(const string_type &src_device,
                                         const string_type &src_signal,
                                         const string_type &dest_device,
                                         const string_type &dest_signal) const
        {
            const char *_src_dev = src_device;
            const char *_src_sig = src_signal;
            return Connection::Iterator(
                mapper_db_get_connections_by_device_and_signal_names(db, 1,
                     &_src_dev, &_src_sig, dest_device, dest_signal));
        }
        Connection::Iterator connections(const string_type &device_name) const
        {
            return Connection::Iterator(
                 mapper_db_get_connections_by_device_name(db, device_name));
        }
        Connection::Iterator connections_by_src(const string_type &signal_name) const
        {
            return Connection::Iterator(
                 mapper_db_get_connections_by_src_signal_name(db, signal_name));
        }
        Connection::Iterator connections_by_src(const string_type &device_name,
                                                const string_type &signal_name) const
        {
            return Connection::Iterator(
                 mapper_db_get_connections_by_src_device_and_signal_names(db,
                      device_name, signal_name));
        }
        Connection::Iterator connections_by_dest(const string_type &signal_name) const
        {
            return Connection::Iterator(
                 mapper_db_get_connections_by_dest_signal_name(db, signal_name));
        }
        Connection::Iterator connections_by_dest(const string_type &device_name,
                                                 const string_type &signal_name) const
        {
            return Connection::Iterator(
                 mapper_db_get_connections_by_dest_device_and_signal_names(db,
                      device_name, signal_name));
        }
        Connection connection_by_signals(const string_type &source_name,
                                         const string_type &dest_name) const
        {
            const char *_src = source_name;
            return Connection(
                mapper_db_get_connection_by_signal_full_names(db, 1, &_src,
                                                              dest_name));
        }
        Connection::Iterator connections_by_devices(const string_type &source_name,
                                                    const string_type &dest_name) const
        {
            const char *_src = source_name;
            return Connection::Iterator(
                mapper_db_get_connections_by_src_dest_device_names(db, 1, &_src,
                                                                   dest_name));
        }
        Connection::Iterator connections(Signal::Iterator src_list,
                                         Signal::Iterator dest_list) const
        {
            return Connection::Iterator(
                 mapper_db_get_connections_by_signal_queries(db,
                     (mapper_db_signal*)(src_list),
                     (mapper_db_signal*)(dest_list)));
        }
    private:
        mapper_db db;
        mapper_monitor monitor;
    };

    class Monitor
    {
    public:
        Monitor()
            { monitor = mmon_new(0, 0); }
        Monitor(Admin admin, int autosubscribe_flags=0)
            { monitor = mmon_new(admin, autosubscribe_flags); }
        Monitor(int autosubscribe_flags)
            { monitor = mmon_new(0, autosubscribe_flags); }
        ~Monitor()
            { if (monitor) mmon_free(monitor); }
        int poll(int block_ms=0)
            { return mmon_poll(monitor, block_ms); }
        const Db db() const
            { return Db(monitor); }
        const Monitor& request_devices() const
        {
            mmon_request_devices(monitor);
            return (*this);
        }
        const Monitor& subscribe(const string_type &device_name, int flags, int timeout)
        {
            mmon_subscribe(monitor, device_name, flags, timeout);
            return (*this);
        }
        const Monitor& unsubscribe(const string_type &device_name)
        {
            mmon_unsubscribe(monitor, device_name);
            return (*this);
        }
        const Monitor& autosubscribe(int flags) const
        {
            mmon_autosubscribe(monitor, flags);
            return (*this);
        }
        // TODO: handle connections with multiple inputs
        // array/vector of const char
        // array/vector of std::string
        // array/vector of mapper::Signal
        // array/vector of mapper::Db::Signal
        const Monitor& connect(const mapper::Signal &source, const mapper::Signal &dest,
                               const mapper::Db::Connection &props=0) const
        {
            mapper_db_signal _src = (mapper_db_signal)source.properties();
            mmon_connect_signals_by_db_record(monitor, 1, &_src,
                                              (mapper_db_signal)dest.properties(),
                                              (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& connect(const mapper::Db::Signal &source,
                               const mapper::Db::Signal &dest,
                               const mapper::Db::Connection &props=0) const
        {
            mapper_db_signal _src = (mapper_db_signal)source;
            mmon_connect_signals_by_db_record(monitor, 1, &_src,
                                              (mapper_db_signal)dest,
                                              (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& connect(const string_type &source, const string_type &dest,
                               const mapper::Db::Connection &props=0) const
        {
            const char *_src = source;
            mmon_connect_signals_by_name(monitor, 1, &_src, dest,
                                         (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& connect(int num_sources,
                               const mapper::Signal sources[], const mapper::Signal &dest,
                               const mapper::Db::Connection &props=0) const
        {
            mapper_db_signal _srcs[num_sources];
            for (int i = 0; i < num_sources; i++)
                _srcs[i] = (mapper_db_signal)sources[i].properties();
            mmon_connect_signals_by_db_record(monitor, num_sources, _srcs,
                                              (mapper_db_signal)dest.properties(),
                                              (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& connect(int num_sources,
                               const mapper::Db::Signal sources[],
                               const mapper::Db::Signal &dest,
                               const mapper::Db::Connection &props=0) const
        {
            mapper_db_signal _srcs[num_sources];
            for (int i = 0; i < num_sources; i++)
                _srcs[i] = (mapper_db_signal)sources[i];
            mmon_connect_signals_by_db_record(monitor, num_sources, _srcs,
                                              (mapper_db_signal)dest,
                                              (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& connect(int num_sources,
                               const char *sources[], const string_type &dest,
                               const mapper::Db::Connection &props=0) const
        {
            const char *_srcs[num_sources];
            for (int i = 0; i < num_sources; i++)
                _srcs[i] = (const char *)sources[i];
            mmon_connect_signals_by_name(monitor, num_sources, _srcs, dest,
                                         (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& connect(int num_sources,
                               const std::string sources[], const string_type &dest,
                               const mapper::Db::Connection &props=0) const
        {
            const char *_srcs[num_sources];
            for (int i = 0; i < num_sources; i++)
                _srcs[i] = (const char *)sources[i].c_str();
            mmon_connect_signals_by_name(monitor, num_sources, _srcs, dest,
                                         (mapper_db_connection)props);
            return (*this);
        }
        template <size_t num_sources>
        const Monitor& connect(std::array<const mapper::Signal, num_sources>& sources,
                               const mapper::Signal &dest,
                               const mapper::Db::Connection &props=0) const
        {
            mapper_db_signal _srcs[num_sources];
            for (int i = 0; i < num_sources; i++)
                _srcs[i] = (mapper_db_signal)sources[i].properties();
            mmon_connect_signals_by_db_record(monitor, num_sources, _srcs,
                                              (mapper_db_signal)dest.properties(),
                                              (mapper_db_connection)props);
            return (*this);
        }
        template <size_t num_sources>
        const Monitor& connect(std::array<const mapper::Db::Signal, num_sources>& sources,
                               const mapper::Db::Signal &dest,
                               const mapper::Db::Connection &props=0) const
        {
            mapper_db_signal _srcs[num_sources];
            for (int i = 0; i < num_sources; i++)
                _srcs[i] = (mapper_db_signal)sources[i];
            mmon_connect_signals_by_db_record(monitor, num_sources, _srcs,
                                              (mapper_db_signal)dest,
                                              (mapper_db_connection)props);
            return (*this);
        }
        template <size_t num_sources>
        const Monitor& connect(std::array<const char*, num_sources>& sources,
                               const string_type &dest,
                               const mapper::Db::Connection &props=0) const
        {
            const char *_srcs[num_sources];
            for (int i = 0; i < num_sources; i++)
                _srcs[i] = (const char *)sources[i];
            mmon_connect_signals_by_name(monitor, num_sources, _srcs, dest,
                                         (mapper_db_connection)props);
            return (*this);
        }
        template <size_t num_sources>
        const Monitor& connect(std::array<std::string, num_sources>& sources,
                               const string_type &dest,
                               const mapper::Db::Connection &props=0) const
        {
            const char *_srcs[num_sources];
            for (int i = 0; i < num_sources; i++)
                _srcs[i] = (const char *)sources[i].c_str();
            mmon_connect_signals_by_name(monitor, num_sources, _srcs, dest,
                                         (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& connect(std::vector<const mapper::Signal> sources,
                               const mapper::Signal &dest,
                               const mapper::Db::Connection &props=0) const
        {
            connect(sources.size(), sources.data(), dest, props);
            return (*this);
        }
        const Monitor& connect(std::vector<const mapper::Db::Signal> sources,
                               const mapper::Db::Signal &dest,
                               const mapper::Db::Connection &props=0) const
        {
            connect(sources.size(), sources.data(), dest, props);
            return (*this);
        }
        const Monitor& connect(std::vector<const char*>& sources,
                               const string_type &dest,
                               const mapper::Db::Connection &props=0) const
        {
            connect(sources.size(), sources.data(), dest, props);
            return (*this);
        }
        const Monitor& connect(std::vector<std::string>& sources,
                               const string_type &dest,
                               const mapper::Db::Connection &props=0) const
        {
            connect(sources.size(), sources.data(), dest, props);
            return (*this);
        }
        const Monitor& modify_connection(const mapper::Db::Connection &connection) const
        {
            mmon_modify_connection(monitor, (mapper_db_connection)connection);
            return (*this);
        }
        const Monitor& modify_connection(const string_type &source,
                                         const string_type &dest,
                                         const mapper::Db::Connection &props) const
        {
            const char *_src = source;
            mmon_modify_connection_by_signal_names(monitor, 1, &_src, dest,
                                                   (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& modify_connection(const mapper::Signal &source,
                                         const mapper::Signal &dest,
                                         const mapper::Db::Connection &props) const
        {
            mapper_db_signal _src = (mapper_db_signal)source.properties();
            mmon_modify_connection_by_signal_db_records(monitor, 1, &_src,
                                                        (mapper_db_signal)dest.properties(),
                                                        (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& modify_connection(const mapper::AbstractSignalProps &source,
                                         const mapper::AbstractSignalProps &dest,
                                         const mapper::Db::Connection &props) const
        {
            mapper_db_signal _src = (mapper_db_signal)source;
            mmon_modify_connection_by_signal_db_records(monitor, 1, &_src,
                                                        (mapper_db_signal)dest,
                                                        (mapper_db_connection)props);
            return (*this);
        }
        const Monitor& disconnect(const mapper::Db::Connection &connection) const
        {
            mmon_remove_connection(monitor, (mapper_db_connection)connection);
            return (*this);
        }
        const Monitor& disconnect(const string_type &source,
                                  const string_type &dest) const
        {
            const char *_src = source;
            mmon_disconnect_signals_by_name(monitor, 1, &_src, dest);
            return (*this);
        }
        const Monitor& disconnect(const mapper::Signal &source,
                                  const mapper::Signal &dest) const
        {
            mapper_db_signal _src = (mapper_db_signal)source.properties();
            mmon_disconnect_signals_by_db_record(monitor, 1, &_src,
                                                 (mapper_db_signal)dest.properties());
            return (*this);
        }
        const Monitor& disconnect(const mapper::Db::Signal &source,
                                  const mapper::Db::Signal &dest) const
        {
            mapper_db_signal _src = (mapper_db_signal)source;
            mmon_disconnect_signals_by_db_record(monitor, 1, &_src,
                                                 (mapper_db_signal)dest);
            return (*this);
        }
    private:
        mapper_monitor monitor;
    };
};

#endif // _MAPPER_CPP_H_
