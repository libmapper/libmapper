
#ifndef _MAPPER_CPP_H_
#define _MAPPER_CPP_H_

#include <mapper/mapper.h>

#include <functional>
#include <memory>
#include <list>
#include <unordered_map>
#include <string>
#include <sstream>
#include <initializer_list>
#include <array>
#include <vector>
#include <iterator>

/* TODO:
 *      signal update handlers
 *      instance event handlers
 *      database handlers
 *      device mapping handlers
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

    class Device;
    class Signal;
    class GenericObject;
    class Property;
    class Database;

    // Helper class to allow polymorphism on "const char *" and "std::string".
    class string_type {
    public:
        string_type(const char *s=0) { _s = s; }
        string_type(const std::string& s) { _s = s.c_str(); }
        operator const char*() const { return _s; }
        const char *_s;
    };

    class Network
    {
    public:
        Network(const string_type &iface=0, const string_type &group=0, int port=0)
            { _net = mapper_network_new(iface, group, port); _owned = 1; }
        ~Network()
            { if (_owned && _net) mapper_network_free(_net); }
        operator mapper_network() const
            { return _net; }
        std::string libversion()
            { return std::string(mapper_libversion()); }
        std::string interface() const
        {
            const char *iface = mapper_network_interface(_net);
            return iface ? std::string(iface) : 0;
        }
        const struct in_addr *ip4() const
            { return mapper_network_ip4(_net); }
        std::string group() const
            { return std::string(mapper_network_group(_net)); }
        int port() const
            { return mapper_network_port(_net); }
    protected:
        friend class Device;
        friend class Database;
        Network(mapper_network net)
            { _net = net; _owned = 0; }
    private:
        mapper_network _net;
        int _owned;
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
            { return mapper_timetag_double(timetag); }
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

    class AbstractObjectWithSetter
    {
    protected:
        friend class Property;
        virtual AbstractObjectWithSetter& set_property(Property *p) = 0;
    public:
        virtual AbstractObjectWithSetter& remove_property(const string_type &key) = 0;
    };

    class Property
    {
    public:
        template <typename T>
        Property(const string_type &_name, T _value)
            { name = _name; owned = false; _set(_value); parent = NULL; }
        template <typename T>
        Property(const string_type &_name, int _length, T& _value)
            { name = _name; owned = false; _set(_length, _value); parent = NULL; }
        template <typename T, size_t N>
        Property(const string_type &_name, std::array<T, N> _value)
            { name = _name; owned = false; _set(_value); parent = NULL; }
        template <typename T>
        Property(const string_type &_name, std::vector<T> _value)
            { name = _name; owned = false; _set(_value); parent = NULL; }
        template <typename T>
        Property(const string_type &_name, int _length, char _type, T& _value)
            { name = _name; owned = false; _set(_length, _type, _value); parent = NULL; }

        ~Property()
            { maybe_free(); }

        template <typename T>
        Property& set(T _value)
        {
            maybe_free();
            _set(_value);
            if (parent) parent->set_property(this);
            return (*this);
        }
        template <typename T>
        Property& set(int _length, T& _value)
        {
            maybe_free();
            _set(_value, _length);
            if (parent) parent->set_property(this);
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
        friend class Database;
        friend class GenericObject;
        friend class Device;
        friend class Signal;
        friend class Map;
        Property(const string_type &_name, int _length, char _type,
                 const void *_value, const AbstractObjectWithSetter *_parent)
        {
            name = _name;
            _set(_length, _type, _value);
            parent = (AbstractObjectWithSetter *)_parent;
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
        {
            if (owned && value) {
                if (type == 's' && length > 1) {
                    for (int i = 0; i < length; i++) {
                        free(((char**)value)[i]);
                    }
                }
                free((void*)value);
                owned = false;
            }
        }
        void _set(int _length, bool _value[])
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
        void _set(int _length, int _value[])
            { value = _value; length = _length; type = 'i'; }
        void _set(int _length, float _value[])
            { value = _value; length = _length; type = 'f'; }
        void _set(int _length, double _value[])
            { value = _value; length = _length; type = 'd'; }
        void _set(int _length, char _value[])
            { value = _value; length = _length; type = 'c'; }
        void _set(int _length, const char *_value[])
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
            _set(1, (T*)&_scalar);
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
        void _set(std::array<const char*, N>& _values)
        {
            length = N;
            type = 's';
            if (length == 1) {
                value = strdup(_values[0]);
            }
            else if (length > 1) {
                // need to copy string array
                value = (char**)malloc(sizeof(char*) * length);
                for (int i = 0; i < length; i++) {
                    ((char**)value)[i] = strdup((char*)_values[i]);
                }
                owned = true;
            }
        }
        template <size_t N>
        void _set(std::array<std::string, N>& _values)
        {
            length = N;
            type = 's';
            if (length == 1) {
                value = strdup(_values[0].c_str());
            }
            else if (length > 1) {
                // need to copy string array
                value = (char**)malloc(sizeof(char*) * length);
                for (int i = 0; i < length; i++) {
                    ((char**)value)[i] = strdup((char*)_values[i].c_str());
                }
                owned = true;
            }
        }
        void _set(std::string _values[], int _length)
        {
            length = _length;
            type = 's';
            if (length == 1) {
                value = strdup(_values[0].c_str());
            }
            else if (length > 1) {
                // need to copy string array
                value = malloc(sizeof(char*) * length);
                for (int i = 0; i < length; i++) {
                    ((char**)value)[i] = strdup((char*)_values[i].c_str());
                }
                owned = true;
            }
        }
        template <typename T>
        void _set(std::vector<T> _value)
            { _set((int)_value.size(), _value.data()); }
        void _set(std::vector<const char*>& _value)
        {
            length = (int)_value.size();
            type = 's';
            if (length == 1)
                value = strdup(_value[0]);
            else {
                // need to copy string array since std::vector may free it
                value = malloc(sizeof(char*) * length);
                for (int i = 0; i < length; i++) {
                    ((char**)value)[i] = strdup((char*)_value[i]);
                }
                owned = true;
            }
        }
        void _set(std::vector<std::string>& _value)
        {
            length = (int)_value.size();
            type = 's';
            if (length == 1) {
                value = strdup(_value[0].c_str());
            }
            else if (length > 1) {
                // need to copy string array
                value = malloc(sizeof(char*) * length);
                for (int i = 0; i < length; i++) {
                    ((char**)value)[i] = strdup((char*)_value[i].c_str());
                }
                owned = true;
            }
        }
        void _set(int _length, char _type, const void *_value)
        {
            type = _type;
            value = _value;
            length = _length;
        }
        AbstractObjectWithSetter *parent;
    };

    class GenericObject : public AbstractObjectWithSetter
    {
    protected:
        virtual GenericObject& set_property(Property *p) = 0;
    public:
        virtual Property property(const string_type &name) const = 0;
        virtual Property property(int index) const = 0;
    };

    class Signal : public GenericObject
    {
    protected:
        friend class Property;

        Signal& set_property(Property *p)
        {
            if (_sig)
                mapper_signal_set_property(_sig, p->name, p->length, p->type,
                                           p->value);
            return (*this);
        }

    private:
        mapper_signal _sig;

    public:
        Signal(mapper_signal sig)
            { _sig = sig; }
        operator mapper_signal() const
            { return _sig; }
        operator bool() const
            { return _sig ? true : false; }
        operator const char*() const
            { return mapper_signal_name(_sig); }
        operator mapper_id() const
            { return mapper_signal_id(_sig); }
        template <typename... Values>
        Signal& set_property(Values... values)
        {
            Property p(values...);
            if (p)
                set_property(&p);
            return (*this);
        }
        Signal& remove_property(const string_type &key)
        {
            if (_sig && key)
                mapper_signal_remove_property(_sig, key);
            return (*this);
        }
        Property property(const string_type &name) const
        {
            char type;
            const void *value;
            int length;
            if (!mapper_signal_property(_sig, name, &length, &type, &value))
                return Property(name, length, type, value, this);
            else
                return Property(name, 0, 0, 0, this);
        }
        Property property(int index) const
        {
            const char *name;
            char type;
            const void *value;
            int length;
            if (!mapper_signal_property_index(_sig, index, &name, &length,
                                              &type, &value))
                return Property(name, length, type, value, this);
            else
                return Property(0, 0, 0, 0, this);
        }
        mapper_id id() const
            { return mapper_signal_id(_sig); }
        std::string name() const
            { return std::string(mapper_signal_name(_sig)); }
        char type() const
            { return mapper_signal_type(_sig); }
        int length() const
            { return mapper_signal_length(_sig); }

        /* Value update functions*/
        Signal& update(void *value, int count, Timetag tt)
        {
            mapper_signal_update(_sig, value, count, *tt);
            return (*this);
        }
        Signal& update(int *value, int count, Timetag tt)
        {
            if (mapper_signal_type(_sig) == 'i')
                mapper_signal_update(_sig, value, count, *tt);
            return (*this);
        }
        Signal& update(float *value, int count, Timetag tt)
        {
            if (mapper_signal_type(_sig) == 'f')
                mapper_signal_update(_sig, value, count, *tt);
            return (*this);
        }
        Signal& update(double *value, int count, Timetag tt)
        {
            if (mapper_signal_type(_sig) == 'd')
                mapper_signal_update(_sig, value, count, *tt);
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
            { return update(&value[0], N / mapper_signal_length(_sig), 0); }
        template <typename T>
        Signal& update(std::vector<T> value, Timetag tt=0)
        {
            return update(&value[0],
                          (int)value.size() / mapper_signal_length(_sig), *tt);
        }
        const void *value() const
            { return mapper_signal_value(_sig, 0); }
        const void *value(Timetag tt) const
            { return mapper_signal_value(_sig, tt); }
        int query_remotes() const
            { return mapper_signal_query_remotes(_sig, MAPPER_NOW); }
        int query_remotes(Timetag tt) const
            { return mapper_signal_query_remotes(_sig, *tt); }
        Signal& set_user_data(void *user_data)
        {
            mapper_signal_set_user_data(_sig, user_data);
            return (*this);
        }
        void *user_data() const
            { return mapper_signal_user_data(_sig); }
        Signal& set_callback(mapper_signal_update_handler *handler,
                             void *user_data)
        {
            mapper_signal_set_callback(_sig, handler, user_data);
            return (*this);
        }
        int num_maps(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_signal_num_maps(_sig, dir); }
        Signal& set_minimum(void *value)
            { mapper_signal_set_minimum(_sig, value); return (*this); }
        Signal& set_maximum(void *value)
            { mapper_signal_set_maximum(_sig, value); return (*this); }
        Signal& set_rate(int rate)
            { mapper_signal_set_rate(_sig, rate); return (*this); }

        class Instance {
        public:
            Instance(mapper_signal sig, mapper_id id)
                { _sig = sig; _id = id; }
            bool operator == (Instance i)
                { return (_id == i._id); }
            operator mapper_id() const
                { return _id; }
            Instance& update(void *value, int count, Timetag tt)
            {
                mapper_signal_instance_update(_sig, _id, value, count, *tt);
                return (*this);
            }
            Instance& instance(int *value, int count, Timetag tt)
            {
                if (mapper_signal_type(_sig) == 'i')
                    mapper_signal_instance_update(_sig, _id, value, count, *tt);
                return (*this);
            }
            Instance& instance(float *value, int count, Timetag tt)
            {
                if (mapper_signal_type(_sig) == 'f')
                    mapper_signal_instance_update(_sig, _id, value, count, *tt);
                return (*this);
            }
            Instance& instance(double *value, int count, Timetag tt)
            {
                if (mapper_signal_type(_sig) == 'd')
                    mapper_signal_instance_update(_sig, _id, value, count, *tt);
                return (*this);
            }

            void release()
                { mapper_signal_instance_release(_sig, _id, MAPPER_NOW); }
            void release(Timetag tt)
                { mapper_signal_instance_release(_sig, _id, *tt); }

            template <typename T>
            Instance& update(T value)
                { return update(&value, 1, 0); }
            template <typename T>
            Instance& update(T* value, int count=0)
                { return update(value, count, 0); }
            template <typename T>
            Instance& update(T* value, Timetag tt)
                { return update(value, 1, tt); }
            template <typename T, size_t N>
            Instance& update(std::array<T,N> value, Timetag tt=0)
            {
                return update(&value[0], N / mapper_signal_length(_sig), tt);
            }
            template <typename T>
            Instance& update(std::vector<T> value, Timetag tt=0)
            {
                return update(&value[0],
                              value.size() / mapper_signal_length(_sig), tt);
            }

            mapper_id id() const
                { return _id; }

            Instance& set_user_data(void *user_data)
            {
                mapper_signal_instance_set_user_data(_sig, _id, user_data);
                return (*this);
            }
            void *user_data() const
                { return mapper_signal_instance_user_data(_sig, _id); }

            const void *value() const
                { return mapper_signal_instance_value(_sig, _id, 0); }
            const void *value(Timetag tt) const
                { return mapper_signal_instance_value(_sig, _id, tt); }
        protected:
            friend class Signal;
        private:
            mapper_id _id;
            mapper_signal _sig;
        };
        Instance instance(mapper_id id = 0)
        {
            if (!id)
                id = mapper_device_unique_id(mapper_signal_device(_sig));
            // TODO: wait before activating instance?
            mapper_signal_instance_set_user_data(_sig, id, 0);
            return Instance(_sig, id);
        }
        Signal& reserve_instances(int num, mapper_id *ids = 0)
        {
            mapper_signal_reserve_instances(_sig, num, ids, 0);
            return (*this);
        }
        Signal& reserve_instances(int num, mapper_id *ids, void **user_data)
        {
            mapper_signal_reserve_instances(_sig, num, ids, user_data);
            return (*this);
        }
        Instance active_instance_at_index(int index) const
        {
            return Instance(_sig, mapper_signal_active_instance_id(_sig, index));
        }
        Signal& remove_instance(Instance instance)
            { mapper_signal_remove_instance(_sig, instance._id); return (*this); }
        Instance oldest_active_instance(mapper_id instance_id)
        {
            return Instance(_sig,
                            mapper_signal_oldest_active_instance(_sig));
        }
        Instance newest_active_instance(mapper_id instance_id)
        {
            return Instance(_sig,
                            mapper_signal_newest_active_instance(_sig));
        }
        int num_active_instances() const
            { return mapper_signal_num_active_instances(_sig); }
        int num_reserved_instances() const
            { return mapper_signal_num_reserved_instances(_sig); }
        Signal& set_instance_stealing_mode(mapper_instance_stealing_type mode)
        {
            mapper_signal_set_instance_stealing_mode(_sig, mode);
            return (*this);
        }
        mapper_instance_stealing_type instance_stealing_mode() const
            { return mapper_signal_instance_stealing_mode(_sig); }
        Signal& set_instance_event_callback(mapper_instance_event_handler h,
                                            int flags, void *user_data)
        {
            mapper_signal_set_instance_event_callback(_sig, h, flags, user_data);
            return (*this);
        }

        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Query(mapper_signal *sigs)
                { _sigs = sigs; }
            // override copy constructor
            Query(const Query& orig)
                { _sigs = mapper_signal_query_copy(orig._sigs); }
            ~Query()
                { mapper_signal_query_done(_sigs); }
            operator mapper_signal*() const
                { return _sigs; }
            bool operator==(const Query& rhs)
                { return (_sigs == rhs._sigs); }
            bool operator!=(const Query& rhs)
                { return (_sigs != rhs._sigs); }
            Query& operator++()
            {
                if (_sigs)
                    _sigs = mapper_signal_query_next(_sigs);
                return (*this);
            }
            Query operator++(int)
                { Query tmp(*this); operator++(); return tmp; }
            Signal operator*()
                { return Signal(*_sigs); }
            Query& begin()
                { return (*this); }
            Query end()
                { return Query(0); }

            // Combining functions
            Query& join(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                _sigs = mapper_signal_query_union(_sigs, rhs_cpy);
                return (*this);
            }
            Query& intersect(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                _sigs = mapper_signal_query_intersection(_sigs, rhs_cpy);
                return (*this);
            }
            Query& subtract(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                _sigs = mapper_signal_query_difference(_sigs, rhs_cpy);
                return (*this);
            }
            Query operator+(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_signal *lhs_cpy = mapper_signal_query_copy(_sigs);
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                return Query(mapper_signal_query_union(lhs_cpy, rhs_cpy));
            }
            Query operator*(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_signal *lhs_cpy = mapper_signal_query_copy(_sigs);
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                return Query(mapper_signal_query_intersection(lhs_cpy, rhs_cpy));
            }
            Query operator-(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_signal *lhs_cpy = mapper_signal_query_copy(_sigs);
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                return Query(mapper_signal_query_difference(lhs_cpy, rhs_cpy));
            }
            Query& operator+=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                _sigs = mapper_signal_query_union(_sigs, rhs_cpy);
                return (*this);
            }
            Query& operator*=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                _sigs = mapper_signal_query_intersection(_sigs, rhs_cpy);
                return (*this);
            }
            Query& operator-=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_signal *rhs_cpy = mapper_signal_query_copy(rhs._sigs);
                _sigs = mapper_signal_query_difference(_sigs, rhs_cpy);
                return (*this);
            }

            Signal operator [] (int index)
            {
                return Signal(mapper_signal_query_index(_sigs, index));
            }

            operator std::vector<Signal>() const
            {
                std::vector<Signal> vec;
                // use a copy
                mapper_signal *cpy = mapper_signal_query_copy(_sigs);
                while (cpy) {
                    vec.push_back(Signal(*cpy));
                    cpy = mapper_signal_query_next(cpy);
                }
                return vec;
            }

            // also enable some Signal methods
            Query& remove_property(const string_type &key)
            {
                if (!key)
                    return (*this);
                // use a copy
                mapper_signal *cpy = mapper_signal_query_copy(_sigs);
                while (cpy) {
                    mapper_signal_remove_property(*cpy, key);
                    cpy = mapper_signal_query_next(cpy);
                }
                return (*this);
            }
            template <typename... Values>
            Query& set_property(Values... values)
            {
                Property p(values...);
                if (!p)
                    return (*this);
                // use a copy
                mapper_signal *cpy = mapper_signal_query_copy(_sigs);
                while (cpy) {
                    mapper_signal_set_property(*cpy, p.name, p.length, p.type,
                                               p.value);
                    cpy = mapper_signal_query_next(cpy);
                }
                return (*this);
            }
            Query& set_user_data(void *user_data)
            {
                // use a copy
                mapper_signal *cpy = mapper_signal_query_copy(_sigs);
                while (cpy) {
                    mapper_signal_set_user_data(*cpy, user_data);
                    cpy = mapper_signal_query_next(cpy);
                }
                return (*this);
            }
        private:
            mapper_signal *_sigs;
        };
    };

    class Device : public GenericObject
    {
    protected:
        Device& set_property(Property *p)
        {
            if (_dev)
                mapper_device_set_property(_dev, p->name, p->length, p->type,
                                           p->value);
            return (*this);
        }
    public:
        Device(const string_type &name_prefix, int port, Network net)
        {
            _dev = mapper_device_new(name_prefix, port, net);
            _db = mapper_device_database(_dev);
            _owned = 1;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        Device(const string_type &name_prefix)
        {
            _dev = mapper_device_new(name_prefix, 0, 0);
            _db = mapper_device_database(_dev);
            _owned = 1;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        Device(Device& orig) {
            if (orig) {
                _dev = orig._dev;
                _db = orig._db;
                _owned = orig._owned;
                _refcount_ptr = orig._refcount_ptr;
                if (_owned)
                    incr_refcount();
            }
        }
        Device(mapper_device dev)
        {
            _dev = dev;
            _db = mapper_device_database(_dev);
            _owned = 0;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        ~Device()
        {
            if (_owned && _dev && decr_refcount() <= 0)
                mapper_device_free(_dev);
        }
        operator mapper_device() const
            { return _dev; }
        operator const char*() const
            { return mapper_device_name(_dev); }
        operator mapper_id() const
            { return mapper_device_id(_dev); }

        Device& set_user_data(void *user_data)
        {
            mapper_device_set_user_data(_dev, user_data);
            return (*this);
        }
        void *user_data() const
            { return mapper_device_user_data(_dev); }

        Signal add_signal(mapper_direction dir, const string_type &name,
                          int length, char type, const string_type &unit=0,
                          void *minimum=0, void *maximum=0,
                          mapper_signal_update_handler handler=0,
                          void *user_data=0)
        {
            return Signal(mapper_device_add_signal(_dev, dir, name, length, type,
                                                   unit, minimum, maximum,
                                                   handler, user_data));
        }
        Signal add_input_signal(const string_type &name, int length, char type,
                                const string_type &unit=0,
                                void *minimum=0, void *maximum=0,
                                mapper_signal_update_handler handler=0,
                                void *user_data=0)
        {
            return Signal(mapper_device_add_input_signal(_dev, name, length, type,
                                                         unit, minimum, maximum,
                                                         handler, user_data));
        }
        Signal add_output_signal(const string_type &name, int length, char type,
                                 const string_type &unit=0, void *minimum=0,
                                 void *maximum=0)
        {
            return Signal(mapper_device_add_output_signal(_dev, name, length,
                                                          type, unit,
                                                          minimum, maximum));
        }
        Device& remove_signal(Signal sig)
            { mapper_device_remove_signal(_dev, sig); return (*this); }

        template <typename... Values>
        Device& set_property(Values... values)
        {
            Property p(values...);
            if (p)
                set_property(&p);
            return (*this);
        }

        Network network() const
            { return Network(mapper_device_network(_dev)); }
        int num_signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_device_num_signals(_dev, dir); }

        int num_maps(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_device_num_maps(_dev, dir); }

        Signal signal(const string_type& name)
            { return Signal(mapper_device_signal_by_name(_dev, name)); }
        Signal signal(mapper_id id)
            { return Signal(mapper_device_signal_by_id(_dev, id)); }
        Signal::Query signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Signal::Query(mapper_device_signals(_dev, dir)); }

        int poll(int block_ms=0) const
            { return mapper_device_poll(_dev, block_ms); }
        int num_fds() const
            { return mapper_device_num_fds(_dev); }
        int fds(int *fds, int num) const
            { return mapper_device_fds(_dev, fds, num); }
        Device& service_fd(int fd)
            { mapper_device_service_fd(_dev, fd); return (*this); }
        bool ready() const
            { return mapper_device_ready(_dev); }
        std::string name() const
            { return std::string(mapper_device_name(_dev)); }
        mapper_id id() const
            { return mapper_device_id(_dev); }
        int port() const
            { return mapper_device_port(_dev); }
        int ordinal() const
            { return mapper_device_ordinal(_dev); }
        Device& remove_property(const string_type &key)
        {
            if (_dev && key)
                mapper_device_remove_property(_dev, key);
            return (*this);
        }
        Property property(const string_type &name) const
        {
            char type;
            const void *value;
            int length;
            if (!mapper_device_property(_dev, name, &length, &type, &value))
                return Property(name, length, type, value, this);
            else
                return Property(name, 0, 0, 0, this);
        }
        Property property(int index) const
        {
            const char *name;
            char type;
            const void *value;
            int length;
            if (!mapper_device_property_index(_dev, index, &name, &length,
                                              &type, &value))
                return Property(name, length, type, value, this);
            else
                return Property(0, 0, 0, 0, this);
        }
        Device& start_queue(Timetag tt)
            { mapper_device_start_queue(_dev, *tt); return (*this); }
        Device& send_queue(Timetag tt)
            { mapper_device_send_queue(_dev, *tt); return (*this); }
//        lo::Server lo_server()
//            { return lo::Server(mapper_device_lo_server(_dev)); }
        Timetag now()
        {
            mapper_timetag_t tt;
            mapper_device_now(_dev, &tt);
            return Timetag(tt);
        }

        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Query(mapper_device *devs)
                { _devs = devs; }
            // override copy constructor
            Query(const Query& orig)
                { _devs = mapper_device_query_copy(orig._devs); }
            ~Query()
                { mapper_device_query_done(_devs); }
            operator mapper_device*() const
                { return _devs; }
            bool operator==(const Query& rhs)
                { return (_devs == rhs._devs); }
            bool operator!=(const Query& rhs)
                { return (_devs != rhs._devs); }
            Query& operator++()
            {
                if (_devs)
                    _devs = mapper_device_query_next(_devs);
                return (*this);
            }
            Query operator++(int)
                { Query tmp(*this); operator++(); return tmp; }
            Device operator*()
                { return Device(*_devs); }
            Query begin()
                { return Query(_devs); }
            Query end()
                { return Query(0); }

            // Combination functions
            Query& join(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                _devs = mapper_device_query_union(_devs, rhs_cpy);
                return (*this);
            }
            Query& intersect(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                _devs = mapper_device_query_intersection(_devs, rhs_cpy);
                return (*this);
            }
            Query& subtract(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                _devs = mapper_device_query_difference(_devs, rhs_cpy);
                return (*this);
            }
            Query operator+(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_device *lhs_cpy = mapper_device_query_copy(_devs);
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                return Query(mapper_device_query_union(lhs_cpy, rhs_cpy));
            }
            Query operator*(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_device *lhs_cpy = mapper_device_query_copy(_devs);
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                return Query(mapper_device_query_intersection(lhs_cpy, rhs_cpy));
            }
            Query operator-(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_device *lhs_cpy = mapper_device_query_copy(_devs);
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                return Query(mapper_device_query_difference(lhs_cpy, rhs_cpy));
            }
            Query& operator+=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                _devs = mapper_device_query_union(_devs, rhs_cpy);
                return (*this);
            }
            Query& operator*=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                _devs = mapper_device_query_intersection(_devs, rhs_cpy);
                return (*this);
            }
            Query& operator-=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_device *rhs_cpy = mapper_device_query_copy(rhs._devs);
                _devs = mapper_device_query_difference(_devs, rhs_cpy);
                return (*this);
            }

            Device operator [] (int index)
            {
                return Device(mapper_device_query_index(_devs, index));
            }

            operator std::vector<Device>() const
            {
                std::vector<Device> vec;
                // use a copy
                mapper_device *cpy = mapper_device_query_copy(_devs);
                while (cpy) {
                    vec.push_back(Device(*cpy));
                    cpy = mapper_device_query_next(cpy);
                }
                return vec;
            }

            // also enable some Device methods
            Query& remove_property(const string_type &key)
            {
                if (!key)
                    return (*this);
                // use a copy
                mapper_device *cpy = mapper_device_query_copy(_devs);
                while (cpy) {
                    mapper_device_remove_property(*cpy, key);
                    cpy = mapper_device_query_next(cpy);
                }
                return (*this);
            }
            template <typename... Values>
            Query& set_property(Values... values)
            {
                Property p(values...);
                if (!p)
                    return (*this);
                // use a copy
                mapper_device *cpy = mapper_device_query_copy(_devs);
                while (cpy) {
                    mapper_device_set_property(*cpy, p.name, p.length, p.type,
                                               p.value);
                    cpy = mapper_device_query_next(cpy);
                }
                return (*this);
            }
            Query& set_user_data(void *user_data)
            {
                // use a copy
                mapper_device *cpy = mapper_device_query_copy(_devs);
                while (cpy) {
                    mapper_device_set_user_data(*cpy, user_data);
                    cpy = mapper_device_query_next(cpy);
                }
                return (*this);
            }
        private:
            mapper_device *_devs;
        };
    private:
        mapper_device _dev;
        mapper_database _db;
        int _owned;
        int* _refcount_ptr;
        int incr_refcount()
            { return _refcount_ptr ? ++(*_refcount_ptr) : 0; }
        int decr_refcount()
            { return _refcount_ptr ? --(*_refcount_ptr) : 0; }
    };

    class signal_type {
    public:
        signal_type(mapper_signal sig) { _sig = sig; }
        signal_type(const Signal& sig) { _sig = (mapper_signal)sig; }
        operator mapper_signal() const { return _sig; }
        mapper_signal _sig;
    };

    class Map : public GenericObject
    {
    public:
        Map(const Map& orig)
            { _map = orig._map; }
        Map(mapper_map map)
            { _map = map; }
        Map(signal_type source, signal_type destination)
        {
            mapper_signal cast = source;
            _map = mapper_map_new(1, &cast, destination);
        }
        Map(int num_sources, signal_type sources[], signal_type destination)
        {
            mapper_signal cast[num_sources];
            for (int i = 0; i < num_sources; i++) {
                cast[i] = sources[i];
            }
            _map = mapper_map_new(num_sources, cast, destination);
        }
        template <size_t N>
        Map(std::array<signal_type, N>& sources, signal_type destination)
        {
            if (sources.empty()) {
                _map = 0;
                return;
            }
            mapper_signal cast[N];
            for (int i = 0; i < N; i++) {
                cast[i] = sources.data()[i];
            }
            _map = mapper_map_new(N, cast, destination);
        }
        Map(std::vector<Signal>& sources, signal_type destination)
        {
            if (sources.size()) {
                _map = 0;
            }
            int num_sources = sources.size();
            mapper_signal cast[num_sources];
            for (int i = 0; i < num_sources; i++) {
                cast[i] = sources.data()[i];
            }
                _map = mapper_map_new(num_sources, cast, destination);
        }
        operator mapper_map() const
            { return _map; }
        operator bool() const
            { return _map; }
        operator mapper_id() const
            { return mapper_map_id(_map); }
        const Map& push() const
            { mapper_map_push(_map); return (*this); }
        // this function can be const since it only sends the unmap msg
        void release() const
            { mapper_map_release(_map); }
        int num_sources() const
            { return mapper_map_num_sources(_map); }
        bool ready() const
            { return mapper_map_ready(_map); }
        mapper_mode mode() const
            { return mapper_map_mode(_map); }
        Map& set_mode(mapper_mode mode)
        {
            mapper_map_set_mode(_map, mode);
            return (*this);
        }
        const char* expression() const
            { return mapper_map_expression(_map); }
        Map& set_expression(const string_type &expression)
        {
            mapper_map_set_expression(_map, expression);
            return (*this);
        }
        Property property(const string_type& name) const
        {
            char type;
            const void *value;
            int length;
            if (!mapper_map_property(_map, name, &length, &type, &value))
                return Property(name, length, type, value);
            else
                return Property(name, 0, 0, 0, 0);
        }
        Property property(int index) const
        {
            const char *name;
            char type;
            const void *value;
            int length;
            if (!mapper_map_property_index(_map, index, &name, &length, &type,
                                           &value))
                return Property(name, length, type, value);
            else
                return Property(0, 0, 0, 0, 0);
        }
        mapper_id id() const
            { return mapper_map_id(_map); }
        Map& set_user_data(void *user_data)
        {
            mapper_map_set_user_data(_map, user_data);
            return (*this);
        }
        void *user_data() const
            { return mapper_map_user_data(_map); }
        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            Query(mapper_map *maps)
                { _maps = maps; }
            // override copy constructor
            Query(const Query& orig)
                { _maps = mapper_map_query_copy(orig._maps); }
            ~Query()
                { mapper_map_query_done(_maps); }
            operator mapper_map*() const
                { return _maps; }
            bool operator==(const Query& rhs)
                { return (_maps == rhs._maps); }
            bool operator!=(const Query& rhs)
                { return (_maps != rhs._maps); }
            Query& operator++()
            {
                if (_maps != NULL)
                    _maps = mapper_map_query_next(_maps);
                return (*this);
            }
            Query operator++(int)
                { Query tmp(*this); operator++(); return tmp; }
            Map operator*()
                { return Map(*_maps); }
            Query begin()
                { return Query(_maps); }
            Query end()
                { return Query(0); }

            // Combination functions
            Query& join(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                _maps = mapper_map_query_union(_maps, rhs_cpy);
                return (*this);
            }
            Query& intersect(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                _maps = mapper_map_query_intersection(_maps, rhs_cpy);
                return (*this);
            }
            Query& subtract(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                _maps = mapper_map_query_difference(_maps, rhs_cpy);
                return (*this);
            }
            Query operator+(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_map *lhs_cpy = mapper_map_query_copy(_maps);
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                return Query(mapper_map_query_union(lhs_cpy, rhs_cpy));
            }
            Query operator*(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_map *lhs_cpy = mapper_map_query_copy(_maps);
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                return Query(mapper_map_query_intersection(lhs_cpy, rhs_cpy));
            }
            Query operator-(const Query& rhs) const
            {
                // need to use copies of both queries
                mapper_map *lhs_cpy = mapper_map_query_copy(_maps);
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                return Query(mapper_map_query_difference(lhs_cpy, rhs_cpy));
            }
            Query& operator+=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                _maps = mapper_map_query_union(_maps, rhs_cpy);
                return (*this);
            }
            Query& operator*=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                _maps = mapper_map_query_intersection(_maps, rhs_cpy);
                return (*this);
            }
            Query& operator-=(const Query& rhs)
            {
                // need to use copy of rhs query
                mapper_map *rhs_cpy = mapper_map_query_copy(rhs._maps);
                _maps = mapper_map_query_difference(_maps, rhs_cpy);
                return (*this);
            }

            Map operator [] (int index)
            {
                return Map(mapper_map_query_index(_maps, index));
            }

            operator std::vector<Map>() const
            {
                std::vector<Map> vec;
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_maps);
                while (cpy) {
                    vec.push_back(Map(*cpy));
                    cpy = mapper_map_query_next(cpy);
                }
                return vec;
            }

            // also enable some Map methods
            Query& push()
            {
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_maps);
                while (cpy) {
                    mapper_map_push(*cpy);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
            Query& release()
            {
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_maps);
                while (cpy) {
                    mapper_map_release(*cpy);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
            Query& remove_property(const string_type &key)
            {
                if (!key)
                    return (*this);
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_maps);
                while (cpy) {
                    mapper_map_remove_property(*cpy, key);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
            Query& set_expression(const string_type &expression)
            {
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_maps);
                while (cpy) {
                    mapper_map_set_expression(*cpy, expression);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
            Query& set_mode(mapper_mode mode)
            {
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_maps);
                while (cpy) {
                    mapper_map_set_mode(*cpy, mode);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
            template <typename... Values>
            Query& set_property(Values... values)
            {
                Property p(values...);
                if (!p)
                    return (*this);
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_maps);
                while (cpy) {
                    mapper_map_set_property(*cpy, p.name, p.length, p.type,
                                            p.value);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
            Query& set_user_data(void *user_data)
            {
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_maps);
                while (cpy) {
                    mapper_map_set_user_data(*cpy, user_data);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
        private:
            mapper_map *_maps;
        };
        class Slot : public GenericObject
        {
        public:
            ~Slot() {}
            Signal signal() const
                { return Signal(mapper_slot_signal(_slot)); }
            Device device() const
                { return Device(mapper_signal_device(mapper_slot_signal(_slot))); }
            mapper_boundary_action bound_min() const
                { return mapper_slot_bound_min(_slot); }
            Slot& set_bound_min(mapper_boundary_action bound_min)
            {
                mapper_slot_set_bound_min(_slot, bound_min);
                return (*this);
            }
            mapper_boundary_action bound_max() const
                { return mapper_slot_bound_max(_slot); }
            Slot& set_bound_max(mapper_boundary_action bound_max)
            {
                mapper_slot_set_bound_max(_slot, bound_max);
                return (*this);
            }
            Property minimum() const
            {
                char type;
                int length;
                void *value;
                mapper_slot_minimum(_slot, &length, &type, &value);
                if (value)
                    return Property("minimum", length, type, value);
                else
                    return Property("minimum", 0, 0, 0, 0);
            }
            Slot& set_minimum(const Property &value)
            {
                mapper_slot_set_minimum(_slot, value.length, value.type,
                                        (void*)(const void*)value);
                return (*this);
            }
            Property maximum() const
            {
                char type;
                int length;
                void *value;
                mapper_slot_maximum(_slot, &length, &type, &value);
                if (value)
                    return Property("maximum", length, type, value);
                else
                    return Property("maximum", 0, 0, 0, 0);
            }
            Slot& set_maximum(const Property &value)
            {
                mapper_slot_set_maximum(_slot, value.length, value.type,
                                        (void*)(const void*)value);
                return (*this);
            }
            bool calibrating() const
                { return mapper_slot_calibrating(_slot); }
            Slot& set_calibrating(bool value)
            {
                mapper_slot_set_calibrating(_slot, (int)value);
                return (*this);
            }
            bool causes_update() const
                { return mapper_slot_causes_update(_slot); }
            Slot& set_causes_update(bool value)
            {
                mapper_slot_set_causes_update(_slot, (int)value);
                return (*this);
            }
            bool use_as_instance() const
                { return mapper_slot_use_as_instance(_slot); }
            Slot& set_use_as_instance(bool value)
            {
                mapper_slot_set_use_as_instance(_slot, (int)value);
                return (*this);
            }
            Property property(const string_type &name) const
            {
                char type;
                const void *value;
                int length;
                if (!mapper_slot_property(_slot, name, &length, &type, &value))
                    return Property(name, length, type, value);
                else
                    return Property(name, 0, 0, 0, 0);
            }
            Property property(int index) const
            {
                const char *name;
                char type;
                const void *value;
                int length;
                if (!mapper_slot_property_index(_slot, index, &name, &length,
                                                &type, &value))
                    return Property(name, length, type, value);
                else
                    return Property(name, 0, 0, 0, 0);
            }
            template <typename... Values>
            Slot& set_property(Values... values)
            {
                Property p(values...);
                if (p)
                    set_property(&p);
                return (*this);
            }
        protected:
            friend class Map;
            Slot(mapper_slot slot)
                { _slot = slot; }
            operator mapper_slot() const
                { return _slot; }
            Slot& set_property(Property *p)
            {
                if (_slot)
                    mapper_slot_set_property(_slot, p->name, p->length, p->type,
                                             p->value);
                return (*this);
            }
            Slot& remove_property(const string_type &key)
            {
                if (_slot && key)
                    mapper_slot_remove_property(_slot, key);
                return (*this);
            }
        private:
            mapper_slot _slot;
        };
        Slot destination() const
            { return Slot(mapper_map_destination_slot(_map)); }
        Slot source(int index=0) const
            { return Slot(mapper_map_source_slot(_map, index)); }
        template <typename... Values>
        Map& set_property(Values... values)
        {
            Property p(values...);
            if (p)
                set_property(&p);
            return (*this);
        }
    protected:
        friend class Database;
        Map& set_property(Property *p)
        {
            if (_map)
                mapper_map_set_property(_map, p->name, p->length, p->type,
                                        p->value);
            return (*this);
        }
        Map& remove_property(const string_type &key)
        {
            if (_map && key)
                mapper_map_remove_property(_map, key);
            return (*this);
        }
    private:
        mapper_map _map;
    };

    class device_type {
    public:
        device_type(mapper_device dev) { _dev = dev; }
        device_type(const Device& dev) { _dev = (mapper_device)dev; }
        operator mapper_device() const { return _dev; }
        mapper_device _dev;
    };

    class Database
    {
    protected:
        Database(mapper_database db)
            { _db = db; }
    public:
        Database(int flags = MAPPER_OBJ_ALL)
            { _db = mapper_database_new(0, flags); }
        ~Database()
        {
            if (_db)
                mapper_database_free(_db);
        }
        const Database& poll(int block_ms=0) const
        {
            mapper_database_poll(_db, block_ms);
            return (*this);
        }
        const Database& flush() const
        {
            mapper_database_flush(_db, mapper_database_timeout(_db), 0);
            return (*this);
        }
        const Database& flush(int timeout_sec, int quiet=0) const
        {
            mapper_database_flush(_db, timeout_sec, quiet);
            return (*this);
        }

        // subscriptions
        const Database& request_devices() const
        {
            mapper_database_request_devices(_db);
            return (*this);
        }
        const Database& subscribe(const device_type& dev, int flags, int timeout)
        {
            mapper_database_subscribe(_db, dev, flags, timeout);
            return (*this);
        }
        const Database& subscribe(int flags)
            { mapper_database_subscribe(_db, 0, flags, -1); return (*this); }
        const Database& unsubscribe(const device_type& dev)
        {
            mapper_database_unsubscribe(_db, dev);
            return (*this);
        }
        const Database& unsubscribe()
            { mapper_database_unsubscribe(_db, 0); return (*this); }

        // database_devices
        const Database& add_device_callback(mapper_database_device_handler *h,
                                            void *user_data) const
        {
            mapper_database_add_device_callback(_db, h, user_data);
            return (*this);
        }
        const Database& remove_device_callback(mapper_database_device_handler *h,
                                         void *user_data) const
        {
            mapper_database_remove_device_callback(_db, h, user_data);
            return (*this);
        }

        Device device_by_id(mapper_id id) const
            { return Device(mapper_database_device_by_id(_db, id)); }
        Device device_by_name(const string_type &name) const
            { return Device(mapper_database_device_by_name(_db, name)); }
        Device::Query devices() const
            { return Device::Query(mapper_database_devices(_db)); }
        Device::Query local_devices() const
            { return Device::Query(mapper_database_local_devices(_db)); }
        Device::Query devices_by_name_match(const string_type &pattern) const
        {
            return Device::Query(mapper_database_devices_by_name_match(_db,
                                                                       pattern));
        }
        Device::Query devices_by_property(const Property& p, mapper_op op) const
        {
            return Device::Query(
                mapper_database_devices_by_property(_db, p.name, p.length,
                                                    p.type, p.value, op));
        }
        Device::Query devices_by_property(const Property& p) const
        {
            return devices_by_property(p, MAPPER_OP_EXISTS);
        }

        // database_signals
        const Database& add_signal_callback(mapper_database_signal_handler *h,
                                            void *user_data) const
        {
            mapper_database_add_signal_callback(_db, h, user_data);
            return (*this);
        }
        const Database& remove_signal_callback(mapper_database_signal_handler *h,
                                               void *user_data) const
        {
            mapper_database_remove_signal_callback(_db, h, user_data);
            return (*this);
        }

        Signal signal(mapper_id id) const
            { return Signal(mapper_database_signal_by_id(_db, id)); }
        Signal::Query signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Signal::Query(mapper_database_signals(_db, dir)); }
        Signal::Query signals_by_name(const string_type &name) const
        {
            return Signal::Query(mapper_database_signals_by_name(_db, name));
        }
        Signal::Query signals_by_name_match(const string_type &pattern) const
        {
            return Signal::Query(mapper_database_signals_by_name_match(_db,
                                                                       pattern));
        }
        Signal::Query signals_by_property(const Property& p, mapper_op op) const
        {
            return Signal::Query(
                mapper_database_signals_by_property(_db, p.name, p.length,
                                                    p.type, p.value, op));
        }
        Signal::Query signals_by_property(const Property& p) const
        {
            return signals_by_property(p, MAPPER_OP_EXISTS);
        }
        Signal::Query device_signals(const device_type& dev,
                                     mapper_direction dir=MAPPER_DIR_ANY) const
        {
            return Signal::Query(mapper_device_signals(dev, dir));
        }
        Signal device_signal_by_name(const device_type& dev,
                                     const string_type& name) const
        {
            return Signal(mapper_device_signal_by_name(dev, name));
        }

        // database maps
        const Database& add_map_callback(mapper_map_handler *h,
                                         void *user_data) const
        {
            mapper_database_add_map_callback(_db, h, user_data);
            return (*this);
        }
        const Database& remove_map_callback(mapper_map_handler *handler,
                                      void *user_data) const
        {
            mapper_database_remove_map_callback(_db, handler, user_data);
            return (*this);
        }

        Map map_by_id(mapper_id id) const
            { return Map(mapper_database_map_by_id(_db, id)); }
        Map::Query maps() const
            { return Map::Query(mapper_database_maps(_db)); }
        Map::Query maps_by_property(const Property& p, mapper_op op) const
        {
            return Map::Query(
                mapper_database_maps_by_property(_db, p.name, p.length, p.type,
                                                 p.value, op));
        }
        Map::Query maps_by_property(const Property& p) const
        {
            return maps_by_property(p, MAPPER_OP_EXISTS);
        }
        Map::Query device_maps(const device_type& dev,
                               mapper_direction dir=MAPPER_DIR_ANY) const
        {
            return Map::Query(
                mapper_device_maps((mapper_device)dev, dir));
        }
        Map::Query signal_maps(const signal_type& signal,
                               mapper_direction dir=MAPPER_DIR_ANY) const
        {
            return Map::Query(
                mapper_signal_maps((mapper_signal)signal, dir));
        }
    private:
        mapper_database _db;
    };
};

#endif // _MAPPER_CPP_H_
