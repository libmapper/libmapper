
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
 *      device mapping handlers
 *      LinkProps: set scopes
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

    // Helper class to allow polymorphism on "const char *" and "std::string".
    class string_type {
    public:
        string_type(const char *s=0) { _s = s; }
        string_type(const std::string& s) { _s = s.c_str(); }
        operator const char*() const { return _s; }
        const char *_s;
    };

    class device_type {
    public:
        device_type(mapper_device d) { db_dev = mdev_properties(d); }
        device_type(mapper_db_device d) { db_dev = d; }
        operator mapper_db_device() const { return db_dev; }
        mapper_db_device db_dev;
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
        uint32_t sec()
            { return timetag.sec; }
        uint32_t frac()
            { return timetag.frac; }
        operator mapper_timetag_t*()
            { return &timetag; }
        operator double() const
            { return mapper_timetag_get_double(timetag); }
        Timetag& operator+(double addend)
            { mapper_timetag_add_double(&timetag, addend); return (*this); }
        Timetag& operator+(Timetag& addend)
        {
            mapper_timetag_add(&timetag, *(mapper_timetag_t*)addend);
            return (*this);
        }
        Timetag& operator-(Timetag& subtrahend)
        {
            mapper_timetag_subtract(&timetag, *(mapper_timetag_t*)subtrahend);
            return (*this);
        }
    private:
        mapper_timetag_t timetag;
    };

    class AbstractProps
    {
    protected:
        friend class Property;
        virtual AbstractProps& set(Property *p) = 0;
    public:
        virtual AbstractProps& set(Property& p) = 0;
        virtual AbstractProps& remove(const string_type &name) = 0;
    };

    class Property
    {
    public:
        template <typename T>
        Property(const string_type &_name, T _value)
            { name = _name; _set(_value); parent = NULL; owned = false; }
        template <typename T>
        Property(const string_type &_name, T& _value, int _length)
            { name = _name; _set(_value, _length); parent = NULL; owned = false; }
        template <typename T>
        Property(const string_type &_name, std::vector<T> _value)
            { name = _name; _set(_value); parent = NULL; owned = false; }
        template <typename T>
        Property(const string_type &_name, char _type, T& _value, int _length)
            { name = _name; _set(_type, _value, _length); parent = NULL; owned = false; }

        ~Property()
            { maybe_free(); }

        template <typename T>
        Property& set(T _value)
        {
            maybe_free();
            _set(_value);
            if (parent) parent->set(this);
            return (*this);
        }
        template <typename T>
        Property& set(T& _value, int _length)
        {
            maybe_free();
            _set(_value, _length);
            if (parent) parent->set(this);
            return (*this);
        }
        template <typename T>
        operator const T() const
            { return *(const T*)value; }
        operator const bool() const
        {
            if (!length || !type)
                return false;
            switch (type) {
                case 'i':
                    return *(int*)value != 0;
                    break;
                case 'f':
                    return *(float*)value != 0.f;
                    break;
                case 'd':
                    return *(double*)value != 0.;
                    break;
                default:
                    return value != 0;
            }
        }
        template <typename T>
        operator const T*() const
            { return (const T*)value; }
        operator const char**() const
            { return (const char**)value; }
        template <typename T, size_t N>
        operator const std::array<T, N>() const
        {
            std::array<T, N> temp_a;
            for (int i = 0; i < N && i < length; i++)
                temp_a[i] = ((T*)value)[i];
            return temp_a;
        }
        template <size_t N>
        operator const std::array<const char *, N>() const
        {
            std::array<const char*, N> temp_a;
            if (length == 1)
                temp_a[0] = (const char*)value;
            else {
                const char **tempp = (const char**)value;
                for (int i = 0; i < N && i < length; i++) {
                    temp_a[i] = tempp[i];
                }
            }
            return temp_a;
        }
        template <size_t N>
        operator const std::array<std::string, N>() const
        {
            std::array<std::string, N> temp_a;
            if (length == 1)
                temp_a[0] = std::string((const char*)value);
            else {
                const char **tempp = (const char**)value;
                for (int i = 0; i < N && i < length; i++) {
                    temp_a[i] = std::string(tempp[i]);
                }
            }
            return temp_a;
        }
        template <typename T>
        operator const std::vector<T>() const
        {
            std::vector<T> temp_v;
            for (int i = 0; i < length; i++)
                temp_v.push_back(((T*)value)[i]);
            return temp_v;
        }
        operator const std::vector<const char *>() const
        {
            std::vector<const char*> temp_v;
            if (length == 1)
                temp_v.push_back((const char*)value);
            else {
                const char **tempp = (const char**)value;
                for (int i = 0; i < length; i++)
                    temp_v.push_back(tempp[i]);
            }
            return temp_v;
        }
        operator const std::vector<std::string>() const
        {
            std::vector<std::string> temp_v;
            if (length == 1)
                temp_v.push_back(std::string((const char*)value));
            else {
                const char **tempp = (const char**)value;
                for (int i = 0; i < length; i++)
                    temp_v.push_back(std::string(tempp[i]));
            }
            return temp_v;
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
            owned = false;
        }
    private:
        union {
            double _d;
            float _f;
            int _i;
            char _c;
        } _scalar;
        bool owned;

        void maybe_free()
            { if (owned && value) free((void*)value); owned = false; }
        void _set(bool _value[], int _length)
        {
            int *ivalue = (int*)malloc(sizeof(int)*_length);
            if (!ivalue)
                return;
            for (int i = 0; i < _length; i++)
                ivalue[i] = (int)_value[i];
            value = ivalue;
            length = _length;
            type = 'i';
            owned = true;
        }
        void _set(int _value[], int _length)
            { value = _value; length = _length; type = 'i'; }
        void _set(float _value[], int _length)
            { value = _value; length = _length; type = 'f'; }
        void _set(double _value[], int _length)
            { value = _value; length = _length; type = 'd'; }
        void _set(char _value[], int _length)
            { value = _value; length = _length; type = 'c'; }
        void _set(const char *_value[], int _length)
        {
            length = _length;
            type = 's';
            if (_length == 1)
                value = _value[0];
            else
                value = _value;
        }
        template <typename T>
        void _set(T _value)
        {
            memcpy(&_scalar, &_value, sizeof(_scalar));
            _set((T*)&_scalar, 1);
        }
        template <typename T, size_t N>
        void _set(std::array<T, N>& _value)
        {
            if (!_value.empty())
                _set(_value.data(), N);
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
                owned = true;
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
                owned = true;
            }
        }
        template <typename T>
        void _set(std::vector<T> _value)
            { _set(_value.data(), (int)_value.size()); }
        void _set(std::vector<const char*>& _value)
        {
            length = (int)_value.size();
            type = 's';
            if (length == 1)
                value = _value[0];
            else
                value = _value.data();
        }
        void _set(std::vector<std::string>& _value)
        {
            length = (int)_value.size();
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
                owned = true;
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
        virtual AbstractObjectProps& set(Property *p) = 0;
    public:
        virtual AbstractObjectProps& set(Property& p) = 0;
        virtual Property get(const string_type &name) const = 0;

        Property operator [] (const char* key)
            { return get(key); }
        Property operator [] (const std::string& key)
        { return get(key.c_str()); }

        template <typename T>
        AbstractObjectProps& set(const string_type &_name, T _value)
            { Property temp(_name, _value); set(&temp); return (*this); }
        template <typename T>
        AbstractObjectProps& set(const string_type &_name, T& _value, int _length)
            { Property temp(_name, _value, _length); set(&temp); return (*this); }
        template <typename T>
        AbstractObjectProps& set(const string_type &_name, std::vector<T> _value)
            { Property temp(_name, _value); set(&temp); return (*this); }
        template <typename T>
        AbstractObjectProps& set(const string_type &_name, char _type,
                                 T& _value, int _length)
        {
            Property temp(_name, _type, _value, _length);
            set(&temp);
            return (*this);
        }
    };

    class AbstractSignalProps : public AbstractObjectProps
    {
    // Reuse class for signal and database
    protected:
        friend class Property;
        friend class Signal;

        AbstractSignalProps(mapper_signal sig)
            { signal = sig; props = msig_properties(signal); }
        AbstractSignalProps(mapper_db_signal sig_db)
            { signal = 0; props = sig_db; }
        AbstractSignalProps& set(Property *p)
        {
            if (signal)
                msig_set_property(signal, p->name, p->type,
                                  p->type == 's' && p->length == 1
                                  ? (void*)&p->value : (void*)p->value,
                                  p->length);
            return (*this);
        }
        const char *full_name() const
        {
            char *str = (char*)alloca(128);
            snprintf(str, 128, "%s/%s", props->device->name, props->name);
            return str;
        }

    private:
        mapper_signal signal;
        mapper_db_signal props;

    public:
        operator mapper_db_signal() const
            { return props; }
        operator bool() const
            { return signal || props; }
        operator const char*() const
            { return props->name; }
        operator uint64_t() const
            { return props->id; }
        using AbstractObjectProps::set;
        AbstractSignalProps& set(Property& p)
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
                return Property(name, 0, 0, 0, this);
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
                return Property(0, 0, 0, 0, this);
        }
        std::string name() const
            { return std::string(props->name); }
        char type() const
            { return props->type; }
        int length() const
            { return props->length; }
    };

    class Signal
    {
    public:
        Signal(mapper_signal sig)
        {
            signal = sig;
            props = msig_properties(signal);
            cpp_props = new Properties(signal);
        }
        ~Signal() { ; }

        operator mapper_signal() const
            { return signal; }
        operator mapper_db_signal() const
            { return props; }
        operator bool() const
            { return signal; }
        operator const char*() const
            { return props->name; }
        operator uint64_t() const
            { return props->id; }

        Signal& update(void *value, int count, Timetag tt)
            { msig_update(signal, value, count, *tt); return (*this); }
        Signal& update(int *value, int count, Timetag tt)
        {
            if (props->type == 'i') msig_update(signal, value, count, *tt);
            return (*this);
        }
        Signal& update(float *value, int count, Timetag tt)
        {
            if (props->type == 'f') msig_update(signal, value, count, *tt);
            return (*this);
        }
        Signal& update(double *value, int count, Timetag tt)
        {
            if (props->type == 'd') msig_update(signal, value, count, *tt);
            return (*this);
        }
        template <typename T>
        Signal& update(T value)
            { return update(&value, 1, 0); }
        template <typename T>
        Signal& update(T* value)
            { return update(value, 1, 0); }
        template <typename T, int count>
        Signal& update(T* value)
            { return update(value, count, 0); }
        template <typename T>
        Signal& update(T* value, Timetag tt)
            { return update(value, 1, tt); }
        template <typename T, size_t N>
        Signal& update(std::array<T,N> value)
            { return update(&value[0], N / props->length, 0); }
        template <typename T>
        Signal& update(std::vector<T> value, Timetag tt=0)
            { return update(&value[0], (int)value.size() / props->length, *tt); }
        Signal& update_instance(int instance_id, void *value, int count, Timetag tt)
        {
            msig_update_instance(signal, instance_id, value, count, *tt);
            return (*this);
        }
        Signal& update_instance(int instance_id, int *value, int count, Timetag tt)
        {
            if (props->type == 'i')
                msig_update_instance(signal, instance_id, value, count, *tt);
            return (*this);
        }
        Signal& update_instance(int instance_id, float *value, int count, Timetag tt)
        {
            if (props->type == 'f')
                msig_update_instance(signal, instance_id, value, count, *tt);
            return (*this);
        }
        Signal& update_instance(int instance_id, double *value, int count, Timetag tt)
        {
            if (props->type == 'd')
                msig_update_instance(signal, instance_id, value, count, *tt);
            return (*this);
        }
        template <typename T>
        Signal& update_instance(int instance_id, T value)
            { return update_instance(instance_id, &value, 1, 0); }
        template <typename T>
        Signal& update_instance(int instance_id, T* value, int count=0)
            { return update_instance(instance_id, value, count, 0); }
        template <typename T>
        Signal& update_instance(int instance_id, T* value, Timetag tt)
            { return update_instance(instance_id, value, 1, tt); }
        template <typename T, size_t N>
        Signal& update_instance(int instance_id, std::array<T,N> value, Timetag tt=0)
        {
            return update_instance(instance_id, &value[0], N / props->length, tt);
        }
        template <typename T>
        Signal& update_instance(int instance_id, std::vector<T> value, Timetag tt=0)
        {
            return update_instance(instance_id, &value[0],
                                   value.size()/props->length, tt);
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
        Signal& reserve_instances(int num)
            { msig_reserve_instances(signal, num, 0, 0); return (*this); }
        Signal& reserve_instances(int num, int *instance_ids, void **user_data)
        {
            msig_reserve_instances(signal, num, instance_ids, user_data);
            return (*this);
        }
        Signal& release_instance(int instance_id)
        {
            msig_release_instance(signal, instance_id, MAPPER_NOW);
            return (*this);
        }
        Signal& release_instance(int instance_id, Timetag tt)
            { msig_release_instance(signal, instance_id, *tt); return (*this); }
        Signal& remove_instance(int instance_id)
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
        Signal& set_instance_allocation_mode(mapper_instance_allocation_type mode)
            { msig_set_instance_allocation_mode(signal, mode); return (*this); }
        mapper_instance_allocation_type get_instance_allocation_mode() const
            { return msig_get_instance_allocation_mode(signal); }
        Signal& set_instance_event_callback(mapper_signal_instance_event_handler h,
                                            int flags, void *user_data)
        {
            msig_set_instance_event_callback(signal, h, flags, user_data);
            return (*this);
        }
        Signal& set_instance_data(int instance_id, void *user_data)
        {
            msig_set_instance_data(signal, instance_id, user_data);
            return (*this);
        }
        void *instance_data(int instance_id) const
            { return msig_get_instance_data(signal, instance_id); }
        Signal& set_callback(mapper_signal_update_handler *handler, void *user_data)
            { msig_set_callback(signal, handler, user_data); return (*this); }
        int num_maps() const
            { return msig_num_maps(signal); }
        Signal& set_minimum(void *value)
            { msig_set_minimum(signal, value); return (*this); }
        Signal& set_maximum(void *value)
            { msig_set_maximum(signal, value); return (*this); }
        Signal& set_rate(int rate)
            { msig_set_rate(signal, rate); return (*this); }
        Signal& set_direction(mapper_direction_t direction)
            { msig_set_direction(signal, direction); return (*this); }
        class Properties : public AbstractSignalProps
        {
        public:
            Properties(mapper_signal s) : AbstractSignalProps(s) {}
        };
        Properties properties() const
            { return *cpp_props; }
        Property property(const string_type name)
            { return cpp_props->get(name); }
        class Iterator : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Iterator(mapper_signal *s, int n)
                { sigs = s; size = n; }
            ~Iterator() {}
            bool operator==(const Iterator& rhs)
                { return (sigs == rhs.sigs && size == rhs.size); }
            bool operator!=(const Iterator& rhs)
                { return (sigs != rhs.sigs || size != rhs.size); }
            Iterator& operator++()
                { size++; return (*this); }
            Iterator operator++(int)
                { Iterator tmp(*this); size++; return tmp; }
            Signal operator*()
                { return Signal(sigs[size]); }
            Iterator begin()
                { return Iterator(sigs, 0); }
            Iterator end()
                { return Iterator(sigs, size); }
        private:
            mapper_signal *sigs;
            int size;
        };
    private:
        mapper_signal signal;
        mapper_db_signal props;
        Properties *cpp_props;
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
        AbstractDeviceProps& set(Property *p)
        {
            if (device) {
                mdev_set_property(device, p->name, p->type, (void*)p->value,
                                  p->length);
            }
            return (*this);
        }

    private:
        mapper_device device;
        mapper_db_device props;

    public:
        operator mapper_db_device() const
            { return props; }
        operator bool() const
            { return device || props; }
        operator const char*() const
            { return props->name; }
        operator uint64_t() const
            { return props->id; }

        using AbstractObjectProps::set;
        AbstractDeviceProps& set(Property& p)
            { set(&p); return (*this); }
        AbstractDeviceProps& remove(const string_type &name)
            { if (device) mdev_remove_property(device, name); return (*this); }
        Property get(const string_type &name) const
        {
            char type;
            const void *value;
            int length;
            if (!mapper_db_device_property_lookup(props, name, &type,
                                                  &value, &length)) {
                return Property(name, type, value, length, this);
            }
            else {
                return Property(name, 0, 0, 0, this);
            }
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
                return Property(0, 0, 0, 0, this);
        }
        std::string name() const
            { return std::string(props->name); }
        uint64_t id() const
            { return props->id; }
        int num_outputs() const
            { return props->num_outputs; }
        int num_inputs() const
            { return props->num_inputs; }
    };

    class Device
    {
    public:
        Device(const string_type &name_prefix, int port, Admin admin)
        {
            device = mdev_new(name_prefix, port, admin);
            props = mdev_properties(device);
            cpp_props = new Properties(device);
        }
        Device(const string_type &name_prefix)
        {
            device = mdev_new(name_prefix, 0, 0);
            props = mdev_properties(device);
            cpp_props = new Properties(device);
        }
        ~Device()
            { if (device) mdev_free(device); }
        operator mapper_device() const
            { return device; }
        operator const char*() const
            { return props->name; }
        operator uint64_t() const
            { return props->id; }

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
        Device& remove_signal(Signal sig)
            { mdev_remove_signal(device, sig); return (*this); }
        Device& remove_signal(const string_type &name)
        {
            if (name) {
                mapper_signal sig = mdev_get_signal_by_name(device, name, 0);
                mdev_remove_signal(device, sig);
            }
            return (*this);
        }
        Device& remove_input(Signal input)
            { mdev_remove_input(device, input); return (*this); }
        Device& remove_input(const string_type &name)
        {
            if (name) {
                mapper_signal input = mdev_get_input_by_name(device, name, 0);
                mdev_remove_input(device, input);
            }
            return (*this);
        }
        Device& remove_output(Signal output)
            { mdev_remove_output(device, output); return (*this); }
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
        int num_incoming_maps() const
            { return mdev_num_incoming_maps(device); }
        int num_outgoing_maps() const
            { return mdev_num_outgoing_maps(device); }
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
            { return *cpp_props; }
        Property property(const string_type name)
            { return cpp_props->get(name); }
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
        uint64_t id() const
            { return mdev_id(device); }
        int port() const
            { return mdev_port(device); }
        const struct in_addr *ip4() const
            { return mdev_ip4(device); }
        std::string interface() const
            { return mdev_interface(device); }
        int ordinal() const
            { return mdev_ordinal(device); }
        Device& start_queue(Timetag tt)
            { mdev_start_queue(device, *tt); return (*this); }
        Device& send_queue(Timetag tt)
            { mdev_send_queue(device, *tt); return (*this); }
//        lo::Server get_lo_server()
//            { return lo::Server(mdev_get_lo_server(device)); }
        Device& set_map_callback(mapper_device_map_handler handler,
                                 void *user_data)
        {
            mdev_set_map_callback(device, handler, user_data);
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
        mapper_db_device props;
        Properties *cpp_props;
    };

    class signal_type {
    public:
        signal_type(const mapper_signal s) { db_sig = msig_properties(s); }
        signal_type(const mapper_db_signal s) { db_sig = s; }
        signal_type(Signal s) { db_sig = s; }
        operator mapper_db_signal() const { return db_sig; }
        mapper_db_signal db_sig;
    };

    class Db
    {
    public:
        Db(mapper_monitor mon)
        {
            _mon = mon;
            _db = mmon_get_db(mon);
        }
        ~Db()
        {}
        const Db& flush() const
        {
            mmon_flush_db(_mon, mmon_get_timeout(_mon), 0);
            return (*this);
        }
        const Db& flush(int timeout_sec, int quiet=0) const
        {
            mmon_flush_db(_mon, timeout_sec, quiet);
            return (*this);
        }
        // db_devices
        const Db& add_device_callback(mapper_db_device_handler *handler,
                                      void *user_data) const
        {
            mapper_db_add_device_callback(_db, handler, user_data);
            return (*this);
        }
        const Db& remove_device_callback(mapper_db_device_handler *handler,
                                         void *user_data) const
        {
            mapper_db_remove_device_callback(_db, handler, user_data);
            return (*this);
        }

        class Device : public AbstractDeviceProps
        {
        public:
            Device(mapper_db_device d) : AbstractDeviceProps(d) {}
            class Iterator : public std::iterator<std::input_iterator_tag, int>
            {
            public:
                Iterator(mapper_db db, mapper_db_device *dev)
                    { _db = db; _dev = dev; }
                ~Iterator()
                    { mapper_db_device_done(_dev); }
                operator mapper_db_device*() const
                    { return _dev; }
                Iterator operator+(const Iterator& rhs) const
                {
                    return Iterator(_db,
                                    mapper_db_device_query_union(_db, _dev, rhs));
                }
                Iterator operator*(const Iterator& rhs) const
                {
                    return Iterator(_db,
                                    mapper_db_device_query_intersection(_db, _dev,
                                                                        rhs));
                }
                Iterator operator-(const Iterator& rhs) const
                {
                    return Iterator(_db,
                                    mapper_db_device_query_difference(_db, _dev,
                                                                      rhs));
                }
                Iterator& operator+=(const Iterator& rhs)
                {
                    _dev = mapper_db_device_query_union(_db, _dev, rhs);
                    return (*this);
                }
                Iterator& operator*=(const Iterator& rhs)
                {
                    _dev = mapper_db_device_query_intersection(_db, _dev, rhs);
                    return (*this);
                }
                Iterator& operator-=(const Iterator& rhs)
                {
                    _dev = mapper_db_device_query_difference(_db, _dev, rhs);
                    return (*this);
                }
                bool operator==(const Iterator& rhs)
                    { return (_dev == rhs._dev); }
                bool operator!=(const Iterator& rhs)
                    { return (_dev != rhs._dev); }
                Iterator& operator++()
                {
                    if (_dev != NULL)
                        _dev = mapper_db_device_next(_dev);
                    return (*this);
                }
                Iterator operator++(int)
                    { Iterator tmp(_db, *this); operator++(); return tmp; }
                Device operator*()
                    { return Device(*_dev); }
                Iterator begin()
                    { return Iterator(_db, _dev); }
                Iterator end()
                    { return Iterator(0, 0); }
            private:
                mapper_db _db;
                mapper_db_device *_dev;
            };
        };

        Device device_by_name(const string_type &name) const
            { return Device(mapper_db_get_device_by_name(_db, name)); }
        Device device_by_id(uint32_t id) const
            { return Device(mapper_db_get_device_by_id(_db, id)); }
        Device::Iterator devices() const
            { return Device::Iterator(_db, mapper_db_get_devices(_db)); }
        Device::Iterator devices_by_name_match(const string_type &pattern) const
        {
            return Device::Iterator(_db,
                mapper_db_get_devices_by_name_match(_db, pattern));
        }
        Device::Iterator devices_by_property(const Property& p,
                                             mapper_db_op op) const
        {
            return Device::Iterator(_db,
                mapper_db_get_devices_by_property(_db, p.name, p.type, p.length,
                                                  p.type == 's' && p.length == 1 ?
                                                  (void*)&p.value : (void*)p.value,
                                                  op));
        }
        Device::Iterator devices_by_property(const Property& p) const
        {
            return devices_by_property(p, OP_EXISTS);
        }

        // db_signals
        const Db& add_signal_callback(mapper_db_signal_handler *handler,
                                      void *user_data) const
        {
            mapper_db_add_signal_callback(_db, handler, user_data);
            return (*this);
        }
        const Db& remove_signal_callback(mapper_db_signal_handler *handler,
                                         void *user_data) const
        {
            mapper_db_remove_signal_callback(_db, handler, user_data);
            return (*this);
        }

        class Signal : public AbstractSignalProps
        {
        public:
            Signal(mapper_db_signal s) : AbstractSignalProps(s) {}
            Device device() const
            {
                mapper_db_signal s = (mapper_db_signal)(*this);
                return Db::Device(s->device);
            }
            class Iterator : public std::iterator<std::input_iterator_tag, int>
            {
            public:
                Iterator(mapper_db db, mapper_db_signal *sig)
                    { _db = db; _sig = sig; }
                ~Iterator()
                    { mapper_db_signal_done(_sig); }
                operator mapper_db_signal*() const
                    { return _sig; }
                Iterator operator+(const Iterator& rhs) const
                {
                    return Iterator(_db,
                                    mapper_db_signal_query_union(_db, _sig, rhs));
                }
                Iterator operator*(const Iterator& rhs) const
                {
                    return Iterator(_db,
                                    mapper_db_signal_query_intersection(_db, _sig,
                                                                        rhs));
                }
                Iterator operator-(const Iterator& rhs) const
                {
                    return Iterator(_db,
                                    mapper_db_signal_query_difference(_db, _sig,
                                                                      rhs));
                }
                Iterator& operator+=(const Iterator& rhs)
                {
                    _sig = mapper_db_signal_query_union(_db, _sig, rhs);
                    return (*this);
                }
                Iterator& operator*=(const Iterator& rhs)
                {
                    _sig = mapper_db_signal_query_intersection(_db, _sig, rhs);
                    return (*this);
                }
                Iterator& operator-=(const Iterator& rhs)
                {
                    _sig = mapper_db_signal_query_difference(_db, _sig, rhs);
                    return (*this);
                }
                bool operator==(const Iterator& rhs)
                    { return (_sig == rhs._sig); }
                bool operator!=(const Iterator& rhs)
                    { return (_sig != rhs._sig); }
                Iterator& operator++()
                {
                    if (_sig != NULL)
                        _sig = mapper_db_signal_next(_sig);
                    return (*this);
                }
                Iterator operator++(int)
                    { Iterator tmp(_db, *this); operator++(); return tmp; }
                Signal operator*()
                    { return Signal(*_sig); }
                Iterator begin()
                    { return Iterator(_db, _sig); }
                Iterator end()
                    { return Iterator(0, 0); }
            private:
                mapper_db _db;
                mapper_db_signal *_sig;
            };
        };

        Signal signal_by_id(uint64_t id) const
            { return Signal(mapper_db_get_signal_by_id(_db, id)); }
        Signal::Iterator signals() const
            { return Signal::Iterator(_db, mapper_db_get_signals(_db)); }
        Signal::Iterator inputs() const
            { return Signal::Iterator(_db, mapper_db_get_inputs(_db)); }
        Signal::Iterator outputs() const
            { return Signal::Iterator(_db, mapper_db_get_outputs(_db)); }
        Signal::Iterator signals_by_name(const string_type &name) const
        {
            return Signal::Iterator(_db, mapper_db_get_signals_by_name(_db, name));
        }
        Signal::Iterator inputs_by_name(const string_type &name) const
        {
            return Signal::Iterator(_db, mapper_db_get_inputs_by_name(_db, name));
        }
        Signal::Iterator outputs_by_name(const string_type &name) const
        {
            return Signal::Iterator(_db, mapper_db_get_outputs_by_name(_db, name));
        }
        Signal::Iterator signals_by_name_match(const string_type &pattern) const
        {
            return Signal::Iterator(_db, mapper_db_get_signals_by_name_match(_db, pattern));
        }
        Signal::Iterator inputs_by_name_match(const string_type &pattern) const
        {
            return Signal::Iterator(_db, mapper_db_get_inputs_by_name_match(_db, pattern));
        }
        Signal::Iterator outputs_by_name_match(const string_type &pattern) const
        {
            return Signal::Iterator(_db, mapper_db_get_outputs_by_name_match(_db, pattern));
        }
        Signal::Iterator signals_by_property(const Property& p,
                                             mapper_db_op op) const
        {
            return Signal::Iterator(_db,
                mapper_db_get_signals_by_property(_db, p.name, p.type, p.length,
                                                  p.type == 's' && p.length == 1 ?
                                                  (void*)&p.value : (void*)p.value,
                                                  op));
        }
        Signal::Iterator signals_by_property(const Property& p) const
        {
            return signals_by_property(p, OP_EXISTS);
        }
        Signal::Iterator device_signals(const device_type& device) const
        {
            return Signal::Iterator(_db, mapper_db_get_device_signals(_db, device));
        }
        Signal::Iterator device_inputs(const device_type& device) const
        {
            return Signal::Iterator(_db, mapper_db_get_device_inputs(_db, device));
        }
        Signal::Iterator device_outputs(const device_type& device) const
        {
            return Signal::Iterator(_db, mapper_db_get_device_outputs(_db, device));
        }
        Signal device_signal_by_name(const device_type &device,
                                     const string_type &name) const
        {
            return Signal(mapper_db_get_device_signal_by_name(_db, device, name));
        }
        Signal device_input_by_name(const device_type &device,
                                    const string_type &name) const
        {
            return Signal(mapper_db_get_device_input_by_name(_db, device, name));
        }
        Signal device_output_by_name(const device_type &device,
                                     const string_type &name) const
        {
            return Signal(mapper_db_get_device_output_by_name(_db, device, name));
        }
        Signal::Iterator device_signals_by_name_match(const string_type device_name,
                                                      const string_type pattern) const
        {
            mapper_db_device dev = mapper_db_get_device_by_name(_db, device_name);
            return Signal::Iterator(_db,
                mapper_db_get_device_signals_by_name_match(_db, dev, pattern));
        }
        Signal::Iterator device_inputs_by_name_match(const string_type device_name,
                                                     const string_type pattern) const
        {
            mapper_db_device dev = mapper_db_get_device_by_name(_db, device_name);
            return Signal::Iterator(_db,
                 mapper_db_get_device_inputs_by_name_match(_db, dev, pattern));
        }
        Signal::Iterator device_outputs_by_name_match(const string_type device_name,
                                                      const string_type pattern) const
        {
            mapper_db_device dev = mapper_db_get_device_by_name(_db, device_name);
            return Signal::Iterator(_db,
                mapper_db_get_device_outputs_by_name_match(_db, dev, pattern));
        }

        // db maps
        const Db& add_map_callback(mapper_db_map_handler *handler,
                                   void *user_data) const
        {
            mapper_db_add_map_callback(_db, handler, user_data);
            return (*this);
        }
        const Db& remove_map_callback(mapper_db_map_handler *handler,
                                      void *user_data) const
        {
            mapper_db_remove_map_callback(_db, handler, user_data);
            return (*this);
        }
        class Map : AbstractObjectProps
        {
        public:
            Map(mapper_monitor mon, mapper_db_map map) :
                _destination(Slot(this, &map->destination, 0)),
                _sources((Slot*)calloc(1, map->num_sources * sizeof(Slot)))
            {
                _mon = mon;
                _map = map;
                for (int i = 0; i < _map->num_sources; i++)
                    _sources[i] = Slot(this, &_map->sources[i], 1);
                _owned = false;
            }
            Map(int num_sources, mapper_db_signal sources[],
                mapper_db_signal destination) :
                _destination(Slot(this, 0, 0)),
                _sources((Slot*)calloc(1, num_sources * sizeof(Slot)))
            {
                _mon = 0;
                _map = mapper_db_map_new(num_sources, sources, destination);
                _destination._slot = &_map->destination;
                for (int i = 0; i < num_sources; i++)
                    _sources[i] = Slot(this, &_map->sources[i], 1);
                _owned = true;
            }
            Map(const signal_type source, const signal_type destination) :
                _destination(Slot(this, 0, 0)),
                _sources((Slot*)calloc(1, sizeof(Slot)))
            {
                _mon = 0;
                mapper_db_signal src = source;
                _map = mapper_db_map_new(1, &src, destination);
                _destination._slot = &_map->destination;
                _sources[0] = Slot(this, &_map->sources[0], 1);
                _owned = true;
            }
            ~Map()
            {
                printf("Map destructor\n");
                if (_owned && _map) {
                    if (_sources)
                        free(_sources);
                    mapper_db_map_free(_map);
                    _owned = false;
                    _map = 0;
                }
            }
            operator mapper_db_map() const
                { return _map; }
            operator bool() const
                { return _map; }
            Map& update()
                { if (_mon) mmon_update_map(_mon, _map); return (*this); }
            int num_sources() const
                { return _map->num_sources; }
            mapper_mode_type mode() const
                { return _map->mode; }
            Map& set_mode(mapper_mode_type mode)
            {
                _map->mode = mode;
                _map->flags |= MAP_MODE;
                return (*this);
            }
            std::string expression() const
                { return std::string(_map->expression); }
            Map& set_expression(const string_type &expression)
            {
                _map->expression = (char*)(const char*)expression;
                _map->flags |= MAP_EXPRESSION;
                return (*this);
            }
            Property get(const string_type& name) const
            {
                char type;
                const void *value;
                int length;
                if (!mapper_db_map_property_lookup(_map, name, &type, &value,
                                                   &length))
                    return Property(name, type, value, length);
                else
                    return Property(name, 0, 0, 0, 0);
            }
            Property get(int index) const
            {
                const char *name;
                char type;
                const void *value;
                int length;
                if (!mapper_db_map_property_index(_map, index, &name, &type,
                                                  &value, &length))
                    return Property(name, type, value, length);
                else
                    return Property(0, 0, 0, 0, 0);
            }
            uint64_t id() const
                { return _map->id; }
            class Iterator : public std::iterator<std::input_iterator_tag, int>
            {
            public:
                Iterator(mapper_monitor mon, mapper_db_map *map)
                    { _mon = mon; _map = map; }
                ~Iterator()
                    { mapper_db_map_done(_map); }
                operator mapper_db_map*() const
                    { return _map; }
                Iterator operator+(const Iterator& rhs) const
                {
                    return Iterator(mmon_get_db(_mon),
                                    mapper_db_map_query_union(mmon_get_db(_mon),
                                                              _map, rhs));
                }
                Iterator operator*(const Iterator& rhs) const
                {
                    return Iterator(mmon_get_db(_mon),
                                    mapper_db_map_query_intersection(mmon_get_db(_mon),
                                                                     _map, rhs));
                }
                Iterator operator-(const Iterator& rhs) const
                {
                    return Iterator(mmon_get_db(_mon),
                                    mapper_db_map_query_difference(mmon_get_db(_mon),
                                                                   _map, rhs));
                }
                Iterator& operator+=(const Iterator& rhs)
                {
                    _map = mapper_db_map_query_union(mmon_get_db(_mon), _map, rhs);
                    return (*this);
                }
                Iterator& operator*=(const Iterator& rhs)
                {
                    _map = mapper_db_map_query_intersection(mmon_get_db(_mon), _map, rhs);
                    return (*this);
                }
                Iterator& operator-=(const Iterator& rhs)
                {
                    _map = mapper_db_map_query_difference(mmon_get_db(_mon), _map, rhs);
                    return (*this);
                }
                bool operator==(const Iterator& rhs)
                    { return (_map == rhs._map); }
                bool operator!=(const Iterator& rhs)
                    { return (_map != rhs._map); }
                Iterator& operator++()
                {
                    if (_map != NULL)
                        _map = mapper_db_map_next(_map);
                    return (*this);
                }
                Iterator operator++(int)
                    { Iterator tmp(_mon, *this); operator++(); return tmp; }
                Map operator*()
                    { return Map(_mon, *_map); }
                Iterator begin()
                    { return Iterator(_mon, _map); }
                Iterator end()
                    { return Iterator(0, 0); }
            private:
                mapper_monitor _mon;
                mapper_db_map *_map;
            };
            class Slot : AbstractObjectProps
            {
            public:
                ~Slot() {}
                Signal signal() const
                    { return Signal(_slot->signal); }
                mapper_boundary_action bound_min() const
                    { return _slot->bound_min; }
                Slot& set_bound_min(mapper_boundary_action bound_min)
                {
                    _slot->bound_min = bound_min;
                    _slot->flags |= MAP_SLOT_BOUND_MIN;
                    return (*this);
                }
                mapper_boundary_action bound_max() const
                    { return _slot->bound_max; }
                Slot& set_bound_max(mapper_boundary_action bound_max)
                {
                    _slot->bound_max = bound_max;
                    _slot->flags |= MAP_SLOT_BOUND_MAX;
                    return (*this);
                }
                Property minimum() const
                {
                    if (_slot->minimum)
                        return Property("minimum", _slot->type, _slot->minimum,
                                        _slot->length);
                    else
                        return Property("minimum", 0, 0, 0, 0);
                }
                Slot& set_minimum(const Property &value)
                {
                    _slot->minimum = (void*)(const void*)value;
                    _slot->type = value.type;
                    _slot->length = value.length;
                    _slot->flags |= MAP_SLOT_MIN_KNOWN;
                    return (*this);
                }
                Property maximum() const
                {
                    if (_slot->maximum)
                        return Property("maximum", _slot->type, _slot->maximum,
                                        _slot->length);
                    else
                        return Property("maximum", 0, 0, 0, 0);
                }
                Slot& set_maximum(const Property &value)
                {
                    _slot->maximum = (void*)(const void*)value;
                    _slot->type = value.type;
                    _slot->length = value.length;
                    _slot->flags |= MAP_SLOT_MAX_KNOWN;
                    return (*this);
                }
                bool cause_update() const
                    { return _slot->cause_update; }
                Slot& set_cause_update(bool value)
                    { _slot->cause_update = (int)value; return (*this); }
                bool send_as_instance() const
                    { return _slot->send_as_instance; }
                Slot& set_send_as_instance(bool value)
                    { _slot->send_as_instance = (int)value; return (*this); }
                Property get(const string_type &name) const
                {
                    char type;
                    const void *value;
                    int length;
                    if (!mapper_db_map_slot_property_lookup(_slot, name, &type,
                                                            &value, &length))
                        return Property(name, type, value, length);
                    else
                        return Property(name, 0, 0, 0, 0);
                }
            protected:
                friend class Map;
                Slot(const Map *map, mapper_db_map_slot slot, int is_src)
                {
                    _slot = slot;
                    _map = map;
                    _is_src = is_src;
                    _flags = 0;
                }
                operator mapper_db_map_slot() const
                    { return _slot; }
                Slot& set(Property *p) { return (*this); }
                Slot& set(Property& p) { return (*this); }
                Slot& remove(const string_type &name) { return (*this); }
                int _flags;
            private:
                mapper_db_map_slot _slot;
                const Map *_map;
                int _is_src;
            };
            Slot destination() const
                { return _destination; }
            Slot source(int index=0) const
                { return _sources[index]; }
        protected:
            friend class Monitor;
            Map& set(Property *p) { return (*this); }
            Map& set(Property& p) { set(&p); return (*this); }
            Map& remove(const string_type &name) { return (*this); }
        private:
            mapper_monitor _mon;
            mapper_db_map _map;
            Slot _destination;
            Slot *_sources;
            bool _owned;
        };
        Map map_by_id(uint64_t id) const
            { return Map(_mon, mapper_db_get_map_by_id(_db, id)); }
        Map::Iterator maps() const
            { return Map::Iterator(_mon, mapper_db_get_maps(_db)); }
        Map::Iterator maps_by_property(const Property& p, mapper_db_op op) const
        {
            return Map::Iterator(_db,
                mapper_db_get_maps_by_property(_db, p.name, p.type, p.length,
                                               p.type == 's' && p.length == 1 ?
                                               (void*)&p.value : (void*)p.value,
                                               op));
        }
        Map::Iterator maps_by_property(const Property& p) const
        {
            return maps_by_property(p, OP_EXISTS);
        }
        Map::Iterator device_maps(const device_type& device) const
        {
            return Map::Iterator(_db,
                mapper_db_get_device_maps(_db, (mapper_db_device)device));
        }
        Map::Iterator device_outgoing_maps(const device_type& device) const
        {
            return Map::Iterator(_db,
                mapper_db_get_device_outgoing_maps(_db, (mapper_db_device)device));
        }
        Map::Iterator device_incoming_maps(const device_type& device) const
        {
            return Map::Iterator(_db,
                mapper_db_get_device_incoming_maps(_db, (mapper_db_device)device));
        }
        Map::Iterator signal_maps(const signal_type& signal) const
        {
            return Map::Iterator(_db,
                mapper_db_get_signal_maps(_db, (mapper_db_signal)signal));
        }
        Map::Iterator signal_outgoing_maps(const signal_type& signal) const
        {
            return Map::Iterator(_db,
                mapper_db_get_signal_outgoing_maps(_db, (mapper_db_signal)signal));
        }
        Map::Iterator signal_incoming_maps(const signal_type& signal) const
        {
            return Map::Iterator(_db,
                mapper_db_get_signal_incoming_maps(_db, (mapper_db_signal)signal));
        }
    private:
        mapper_db _db;
        mapper_monitor _mon;
    };

    class Monitor
    {
    public:
        Monitor()
            { _mon = mmon_new(0, 0); }
        Monitor(Admin admin, int subscribe_flags=0)
            { _mon = mmon_new(admin, subscribe_flags); }
        Monitor(int subscribe_flags)
            { _mon = mmon_new(0, subscribe_flags); }
        ~Monitor()
            { if (_mon) mmon_free(_mon); }
        int poll(int block_ms=0)
            { return mmon_poll(_mon, block_ms); }
        const Db db() const
            { return Db(_mon); }
        const Monitor& request_devices() const
        {
            mmon_request_devices(_mon);
            return (*this);
        }
        const Monitor& subscribe(const device_type& device,
                                 int flags, int timeout)
        {
            mmon_subscribe(_mon, device, flags, timeout);
            return (*this);
        }
        const Monitor& subscribe(int flags)
            { mmon_subscribe(_mon, 0, flags, -1); return (*this); }
        const Monitor& unsubscribe(const device_type& device)
        {
            mmon_unsubscribe(_mon, device);
            return (*this);
        }
        const Monitor& unsubscribe()
            { mmon_unsubscribe(_mon, 0); return (*this); }

        const Monitor& update(Db::Map& map)
            { mmon_update_map(_mon, map); return (*this); }

        const Monitor& remove(const Db::Map &map) const
            { mmon_remove_map(_mon, (mapper_db_map)map); return (*this); }
        const Monitor& remove(Db::Map::Iterator maps)
        {
            for (auto const &m : maps)
                remove(m);
            return (*this);
        }
    private:
        mapper_monitor _mon;
    };
};

#endif // _MAPPER_CPP_H_
