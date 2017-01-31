
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
#include <cstring>

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

#define MAPPER_TYPE(NAME) mapper_ ## NAME
#define MAPPER_FUNC(OBJ, FUNC) mapper_ ## OBJ ## _ ## FUNC

#define PROPERTY_METHODS(CLASS_NAME, NAME, PTR)                             \
protected:                                                                  \
    CLASS_NAME& set_property(Property *p)                                   \
    {                                                                       \
        if (PTR)                                                            \
            MAPPER_FUNC(NAME, set_property)(PTR, p->name, p->length,        \
                                            p->type, p->value, p->publish); \
        return (*this);                                                     \
    }                                                                       \
public:                                                                     \
    template <typename... Values>                                           \
    CLASS_NAME& set_property(Values... values)                              \
    {                                                                       \
        Property p(values...);                                              \
        if (p)                                                              \
            set_property(&p);                                               \
        return (*this);                                                     \
    }                                                                       \
    CLASS_NAME& remove_property(const string_type &key)                     \
    {                                                                       \
        if (PTR && key)                                                     \
            MAPPER_FUNC(NAME, remove_property)(PTR, key);                   \
        return (*this);                                                     \
    }                                                                       \
    int num_properties() const                                              \
    {                                                                       \
        return MAPPER_FUNC(NAME, num_properties)(PTR);                      \
    }                                                                       \
    Property property(const string_type &key) const                         \
    {                                                                       \
        char type;                                                          \
        const void *value;                                                  \
        int length;                                                         \
        if (!MAPPER_FUNC(NAME, property)(PTR, key, &length, &type, &value)) \
            return Property(key, length, type, value);                      \
        else                                                                \
            return Property(key, 0, 0, 0);                                  \
    }                                                                       \
    Property property(int index) const                                      \
    {                                                                       \
        const char *key;                                                    \
        char type;                                                          \
        const void *value;                                                  \
        int length;                                                         \
        if (!MAPPER_FUNC(NAME, property_index)(PTR, index, &key, &length,   \
                                               &type, &value))              \
            return Property(key, length, type, value);                      \
        else                                                                \
            return Property(0, 0, 0, 0);                                    \
    }                                                                       \
    const CLASS_NAME& clear_staged_properties() const                       \
    {                                                                       \
        MAPPER_FUNC(NAME, clear_staged_properties)(PTR);                    \
        return (*this);                                                     \
    }                                                                       \

#define QUERY_FUNC(OBJ, FUNC) mapper_ ## OBJ ## _query_ ## FUNC

#define QUERY_METHODS(CLASS_NAME, NAME)                                     \
    Query(MAPPER_TYPE(NAME*) query)                                         \
        { _query = query; }                                                 \
    /* override copy constructor */                                         \
    Query(const Query& orig)                                                \
        { _query = QUERY_FUNC(NAME, copy)(orig._query); }                   \
    ~Query()                                                                \
        { QUERY_FUNC(NAME, done)(_query); }                                 \
    operator MAPPER_TYPE(NAME)*() const                                     \
        { return _query; }                                                  \
    bool operator==(const Query& rhs)                                       \
        { return (_query == rhs._query); }                                  \
    bool operator!=(const Query& rhs)                                       \
        { return (_query != rhs._query); }                                  \
    Query& operator++()                                                     \
    {                                                                       \
        if (_query)                                                         \
            _query = QUERY_FUNC(NAME, next)(_query);                        \
        return (*this);                                                     \
    }                                                                       \
    Query operator++(int)                                                   \
        { Query tmp(*this); operator++(); return tmp; }                     \
    CLASS_NAME operator*()                                                  \
        { return CLASS_NAME(*_query); }                                     \
    Query begin()                                                           \
        { return (*this); }                                                 \
    Query end()                                                             \
        { return Query(0); }                                                \
                                                                            \
    /* Combination functions */                                             \
    Query& join(const Query& rhs)                                           \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, union)(_query, rhs_cpy);                  \
        return (*this);                                                     \
    }                                                                       \
    Query& intersect(const Query& rhs)                                      \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, intersection)(_query, rhs_cpy);           \
        return (*this);                                                     \
    }                                                                       \
    Query& subtract(const Query& rhs)                                       \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, difference)(_query, rhs_cpy);             \
        return (*this);                                                     \
    }                                                                       \
    Query operator+(const Query& rhs) const                                 \
    {                                                                       \
        /* need to use copies of both queries */                            \
        MAPPER_TYPE(NAME) *lhs_cpy = QUERY_FUNC(NAME, copy)(_query);        \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        return Query(QUERY_FUNC(NAME, union)(lhs_cpy, rhs_cpy));            \
    }                                                                       \
    Query operator*(const Query& rhs) const                                 \
    {                                                                       \
        /* need to use copies of both queries */                            \
        MAPPER_TYPE(NAME) *lhs_cpy = QUERY_FUNC(NAME, copy)(_query);        \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        return Query(QUERY_FUNC(NAME, intersection)(lhs_cpy, rhs_cpy));     \
    }                                                                       \
    Query operator-(const Query& rhs) const                                 \
    {                                                                       \
        /* need to use copies of both queries */                            \
        MAPPER_TYPE(NAME) *lhs_cpy = QUERY_FUNC(NAME, copy)(_query);        \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        return Query(QUERY_FUNC(NAME, difference)(lhs_cpy, rhs_cpy));       \
    }                                                                       \
    Query& operator+=(const Query& rhs)                                     \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, union)(_query, rhs_cpy);                  \
        return (*this);                                                     \
    }                                                                       \
    Query& operator*=(const Query& rhs)                                     \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, intersection)(_query, rhs_cpy);           \
        return (*this);                                                     \
    }                                                                       \
    Query& operator-=(const Query& rhs)                                     \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, difference)(_query, rhs_cpy);             \
        return (*this);                                                     \
    }                                                                       \
                                                                            \
    CLASS_NAME operator [] (int idx)                                        \
        { return CLASS_NAME(QUERY_FUNC(NAME, index)(_query, idx)); }        \
                                                                            \
    operator std::vector<CLASS_NAME>() const                                \
    {                                                                       \
        std::vector<CLASS_NAME> vec;                                        \
        /* use a copy */                                                    \
        MAPPER_TYPE(NAME) *cpy = QUERY_FUNC(NAME, copy)(_query);            \
        while (cpy) {                                                       \
            vec.push_back(CLASS_NAME(*cpy));                                \
            cpy = QUERY_FUNC(NAME, next)(cpy);                              \
        }                                                                   \
        return vec;                                                         \
    }                                                                       \
                                                                            \
    template <typename... Values>                                           \
    Query& set_property(Values... values)                                   \
    {                                                                       \
        Property p(values...);                                              \
        if (!p)                                                             \
            return (*this);                                                 \
        /* use a copy */                                                    \
        MAPPER_TYPE(NAME) *cpy = QUERY_FUNC(NAME, copy)(_query);            \
        while (cpy) {                                                       \
            mapper_ ## NAME ## _set_property(*cpy, p.name, p.length,        \
                                             p.type, p.value, p.publish);   \
            cpy = QUERY_FUNC(NAME, next)(cpy);                              \
        }                                                                   \
        return (*this);                                                     \
    }                                                                       \
    Query& remove_property(const string_type &key)                          \
    {                                                                       \
        if (!key)                                                           \
            return (*this);                                                 \
        /* use a copy */                                                    \
        MAPPER_TYPE(NAME) *cpy = QUERY_FUNC(NAME, copy)(_query);            \
        while (cpy) {                                                       \
            mapper_ ## NAME ## _remove_property(*cpy, key);                 \
            cpy = QUERY_FUNC(NAME, next)(cpy);                              \
        }                                                                   \
        return (*this);                                                     \
    }                                                                       \
    Query& set_user_data(void *user_data)                                   \
    {                                                                       \
        /* use a copy */                                                    \
        MAPPER_TYPE(NAME) *cpy = QUERY_FUNC(NAME, copy)(_query);            \
        while (cpy) {                                                       \
            mapper_ ## NAME ## _set_user_data(*cpy, user_data);             \
            cpy = QUERY_FUNC(NAME, next)(cpy);                              \
        }                                                                   \
        return (*this);                                                     \
    }                                                                       \
    Query& push()                                                           \
    {                                                                       \
        /* use a copy */                                                    \
        MAPPER_TYPE(NAME) *cpy = QUERY_FUNC(NAME, copy)(_query);            \
        while (cpy) {                                                       \
            mapper_ ## NAME ## _push(*cpy);                                 \
            cpy = QUERY_FUNC(NAME, next)(cpy);                              \
        }                                                                   \
        return (*this);                                                     \
    }                                                                       \

namespace mapper {

    class Device;
    class Signal;
    class Map;
    class Link;
    class Object;
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
            { _net = mapper_network_new(iface, group, port); _owned = true; }
        ~Network()
            { if (_owned && _net) mapper_network_free(_net); }
        operator mapper_network() const
            { return _net; }
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
            { _net = net; _owned = false; }
    private:
        mapper_network _net;
        bool _owned;
    };

    class Timetag
    {
    public:
        Timetag(mapper_timetag_t tt)
            { _tt.sec = tt.sec; _tt.frac = tt.frac; }
        Timetag(unsigned long int sec, unsigned long int frac)
            { _tt.sec = sec; _tt.frac = frac; }
        Timetag(double seconds)
            { mapper_timetag_set_double(&_tt, seconds); }
        Timetag()
            { mapper_timetag_now(&_tt); }
        uint32_t sec()
            { return _tt.sec; }
        Timetag& set_sec(uint32_t sec)
            { _tt.sec = sec; return (*this); }
        uint32_t frac()
            { return _tt.frac; }
        Timetag& set_frac (uint32_t frac)
            { _tt.frac = frac; return (*this); }
        Timetag& now()
            { mapper_timetag_now(&_tt); return (*this); }
        operator mapper_timetag_t*()
            { return &_tt; }
        operator double() const
            { return mapper_timetag_double(_tt); }
        Timetag& operator=(Timetag& tt)
            { mapper_timetag_copy(&_tt, tt._tt); return (*this); }
        Timetag& operator=(double d)
            { mapper_timetag_set_double(&_tt, d); return (*this); }
        Timetag operator+(Timetag& addend)
        {
            mapper_timetag_t temp;
            mapper_timetag_copy(&temp, _tt);
            mapper_timetag_add(&temp, *(mapper_timetag_t*)addend);
            return temp;
        }
        Timetag operator-(Timetag& subtrahend)
        {
            mapper_timetag_t temp;
            mapper_timetag_copy(&temp, _tt);
            mapper_timetag_subtract(&temp, *(mapper_timetag_t*)subtrahend);
            return temp;
        }
        Timetag& operator+=(Timetag& addend)
        {
            mapper_timetag_add(&_tt, *(mapper_timetag_t*)addend);
            return (*this);
        }
        Timetag& operator+=(double addend)
            { mapper_timetag_add_double(&_tt, addend); return (*this); }
        Timetag& operator-=(Timetag& subtrahend)
        {
            mapper_timetag_subtract(&_tt, *(mapper_timetag_t*)subtrahend);
            return (*this);
        }
        Timetag& operator-=(double subtrahend)
            { mapper_timetag_add_double(&_tt, -subtrahend); return (*this); }
        Timetag& operator*=(double multiplicand)
            { mapper_timetag_multiply(&_tt, multiplicand); return (*this); }
        bool operator<(Timetag& rhs)
        {
            return (_tt.sec < rhs._tt.sec
                    || (_tt.sec == rhs._tt.sec && _tt.frac < rhs._tt.frac));
        }
        bool operator<=(Timetag& rhs)
        {
            return (_tt.sec < rhs._tt.sec
                    || (_tt.sec == rhs._tt.sec && _tt.frac <= rhs._tt.frac));
        }
        bool operator==(Timetag& rhs)
        {
            return (_tt.sec == rhs._tt.sec && _tt.frac == rhs._tt.frac);
        }
        bool operator>=(Timetag& rhs)
        {
            return (_tt.sec > rhs._tt.sec
                    || (_tt.sec == rhs._tt.sec && _tt.frac >= rhs._tt.frac));
        }
        bool operator>(Timetag& rhs)
        {
            return (_tt.sec > rhs._tt.sec
                    || (_tt.sec == rhs._tt.sec && _tt.frac > rhs._tt.frac));
        }
    private:
        mapper_timetag_t _tt;
    };

    class Object
    {
    protected:
        friend class Property;
        virtual Object& set_property(Property *p) = 0;
    public:
        virtual Object& remove_property(const string_type &key) = 0;
        virtual Property property(const string_type &name) const = 0;
        virtual Property property(int index) const = 0;
    };

    class Property
    {
    public:
        template <typename T>
        Property(const string_type &_name, T _value, bool _publish=true)
            { name = _name; owned = false; _set(_value); publish = _publish; }
        template <typename T>
        Property(const string_type &_name, int _length, T& _value, bool _publish=true)
            { name = _name; owned = false; _set(_length, _value); }
        template <typename T, size_t N>
        Property(const string_type &_name, std::array<T, N> _value, bool _publish=true)
            { name = _name; owned = false; _set(_value); }
        template <typename T>
        Property(const string_type &_name, std::vector<T> _value, bool _publish=true)
            { name = _name; owned = false; _set(_value); }
        template <typename T>
        Property(const string_type &_name, int _length, char _type, T& _value, bool _publish=true)
            { name = _name; owned = false; _set(_length, _type, _value); }

        ~Property()
            { maybe_free(); }

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
            for (size_t i = 0; i < N && i < length; i++)
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
                for (size_t i = 0; i < N && i < length; i++) {
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
                for (size_t i = 0; i < N && i < length; i++) {
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
                for (unsigned int i = 0; i < length; i++)
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
                for (unsigned int i = 0; i < length; i++)
                    temp_v.push_back(std::string(tempp[i]));
            }
            return temp_v;
        }
        const char *name;
        char type;
        unsigned int length;
        const void *value;
        bool publish;
    protected:
        friend class Database;
        friend class Object;
        friend class Device;
        friend class Signal;
        friend class Link;
        friend class Map;
        Property(const string_type &_name, int _length, char _type,
                 const void *_value)
        {
            name = _name;
            _set(_length, _type, _value);
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
                    for (unsigned int i = 0; i < length; i++) {
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
                _set(N, _value.data());
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
                for (unsigned int i = 0; i < length; i++) {
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
                for (unsigned int i = 0; i < length; i++) {
                    ((char**)value)[i] = strdup((char*)_values[i].c_str());
                }
                owned = true;
            }
        }
        void _set(int _length, std::string _values[])
        {
            length = _length;
            type = 's';
            if (length == 1) {
                value = strdup(_values[0].c_str());
            }
            else if (length > 1) {
                // need to copy string array
                value = malloc(sizeof(char*) * length);
                for (unsigned int i = 0; i < length; i++) {
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
                for (unsigned int i = 0; i < length; i++) {
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
                for (unsigned int i = 0; i < length; i++) {
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
    };

    class signal_type {
    public:
        signal_type(mapper_signal sig)
            { _sig = sig; }
        inline signal_type(const Signal& sig); // defined later
        operator mapper_signal() const
            { return _sig; }
        mapper_signal _sig;
    };

    class Map : public Object
    {
    public:
        Map(const Map& orig)
            { _map = orig._map; }
        Map(mapper_map map)
            { _map = map; }
        Map(signal_type source, signal_type destination)
        {
            mapper_signal cast_src = source, cast_dst = destination;
            _map = mapper_map_new(1, &cast_src, 1, &cast_dst);
        }
        Map(int num_sources, signal_type sources[],
            int num_destinations, signal_type destinations[])
        {
            mapper_signal cast_dst = destinations[0];
            mapper_signal cast_src[num_sources];
            for (int i = 0; i < num_sources; i++) {
                cast_src[i] = sources[i];
            }
            _map = mapper_map_new(num_sources, cast_src, 1, &cast_dst);
        }
        template <size_t N, size_t M>
        Map(std::array<signal_type, N>& sources,
            std::array<signal_type, M>& destinations)
        {
            if (sources.empty() || destinations.empty() || M != 1) {
                _map = 0;
                return;
            }
            mapper_signal cast[N];
            for (int i = 0; i < N; i++) {
                cast[i] = sources.data()[i];
            }
            _map = mapper_map_new(N, cast, 1, destinations.data()[0]);
        }
        template <typename T>
        Map(std::vector<T>& sources, std::vector<T>& destinations)
        {
            if (!sources.size() || (destinations.size() != 1)) {
                _map = 0;
                return;
            }
            int num_sources = sources.size();
            mapper_signal cast[num_sources];
            for (int i = 0; i < num_sources; i++) {
                cast[i] = sources.data()[i];
            }
            _map = mapper_map_new(num_sources, cast, 1, destinations.data()[0]);
        }
        operator mapper_map() const
            { return _map; }
        operator bool() const
            { return _map; }
        operator mapper_id() const
            { return mapper_map_id(_map); }
        const Map& push() const
            { mapper_map_push(_map); return (*this); }
        const Map& refresh() const
            { mapper_map_refresh(_map); return (*this); }
        // this function can be const since it only sends the unmap msg
        void release() const
            { mapper_map_release(_map); }
        int num_sources() const
            { return mapper_map_num_sources(_map); }
        int num_destinations() const
            { return mapper_map_num_destinations(_map); }
        bool ready() const
            { return mapper_map_ready(_map); }
        mapper_mode mode() const
            { return mapper_map_mode(_map); }
        Map& set_mode(mapper_mode mode)
            { mapper_map_set_mode(_map, mode); return (*this); }
        const char* expression() const
            { return mapper_map_expression(_map); }
        Map& set_expression(const string_type &expression)
            { mapper_map_set_expression(_map, expression); return (*this); }
        bool muted() const
            { return mapper_map_muted(_map); }
        Map& set_muted(bool value)
            { mapper_map_set_muted(_map, (int)value); return (*this); }
        mapper_location process_location() const
            { return mapper_map_process_location(_map); }
        Map& set_process_location(mapper_location loc)
            { mapper_map_set_process_location(_map, loc); return (*this); }
        mapper_id id() const
            { return mapper_map_id(_map); }
        Map& set_user_data(void *user_data)
            { mapper_map_set_user_data(_map, user_data); return (*this); }
        void *user_data() const
            { return mapper_map_user_data(_map); }
        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            QUERY_METHODS(Map, map);

            // also enable some Map methods
            Query& release()
            {
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_query);
                while (cpy) {
                    mapper_map_release(*cpy);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
            Query& set_expression(const string_type &expression)
            {
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_query);
                while (cpy) {
                    mapper_map_set_expression(*cpy, expression);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
            Query& set_mode(mapper_mode mode)
            {
                // use a copy
                mapper_map *cpy = mapper_map_query_copy(_query);
                while (cpy) {
                    mapper_map_set_mode(*cpy, mode);
                    cpy = mapper_map_query_next(cpy);
                }
                return (*this);
            }
        private:
            mapper_map *_query;
        };
        class Slot : public Object
        {
        public:
            Slot(mapper_slot slot)
                { _slot = slot; }
            Slot(const Slot &other)
                { _slot = other._slot; }
            ~Slot() { printf("destroying slot %p\n", _slot); }
            operator mapper_slot() const
                { return _slot; }
            inline Signal signal() const;
            mapper_boundary_action bound_min() const
                { return mapper_slot_bound_min(_slot); }
            Slot& set_bound_min(mapper_boundary_action bound_min)
                { mapper_slot_set_bound_min(_slot, bound_min); return (*this); }
            mapper_boundary_action bound_max() const
                { return mapper_slot_bound_max(_slot); }
            Slot& set_bound_max(mapper_boundary_action bound_max)
                { mapper_slot_set_bound_max(_slot, bound_max); return (*this); }
            Property minimum() const
            {
                char type;
                int length;
                void *value;
                mapper_slot_minimum(_slot, &length, &type, &value);
                if (value)
                    return Property("minimum", length, type, value);
                else
                    return Property("minimum", 0, 0, 0);
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
                    return Property("maximum", 0, 0, 0);
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
            bool use_instances() const
                { return mapper_slot_use_instances(_slot); }
            Slot& set_use_instances(bool value)
            {
                mapper_slot_set_use_instances(_slot, (int)value);
                return (*this);
            }
            PROPERTY_METHODS(Slot, slot, _slot);
        protected:
            friend class Map;
        private:
            mapper_slot _slot;
        };
        Slot destination(int index = 0) const
            { return Slot(mapper_map_slot(_map, MAPPER_LOC_DESTINATION, index)); }
        Slot source(int index=0) const
            { return Slot(mapper_map_slot(_map, MAPPER_LOC_SOURCE, index)); }
        PROPERTY_METHODS(Map, map, _map);
    protected:
        friend class Database;
    private:
        mapper_map _map;
    };

    class Link : public Object
    {
    public:
        Link(const Link& orig)
            { _link = orig._link; }
        Link(mapper_link link)
            { _link = link; }
        operator mapper_link() const
            { return _link; }
        operator bool() const
            { return _link; }
        operator mapper_id() const
            { return mapper_link_id(_link); }
        const Link& push() const
            { mapper_link_push(_link); return (*this); }
        inline Device device(int idx) const;
        mapper_id id() const
            { return mapper_link_id(_link); }
        int num_maps() const
            { return mapper_link_num_maps(_link); }
        Map::Query maps() const
            { return Map::Query(mapper_link_maps(_link)); }
        Link& set_user_data(void *user_data)
            { mapper_link_set_user_data(_link, user_data); return (*this); }
        void *user_data() const
            { return mapper_link_user_data(_link); }
        PROPERTY_METHODS(Link, link, _link);
        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            QUERY_METHODS(Link, link);
        private:
            mapper_link *_query;
        };
    protected:
        friend class Database;
    private:
        mapper_link _link;
    };

    class Signal : public Object
    {
    protected:
        friend class Property;

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
        inline Device device() const;
        Map::Query maps(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Map::Query(mapper_signal_maps(_sig, dir)); }
        PROPERTY_METHODS(Signal, signal, _sig);
        const Signal& push() const
            { mapper_signal_push(_sig); return (*this); }
        mapper_id id() const
            { return mapper_signal_id(_sig); }
        std::string name() const
            { return std::string(mapper_signal_name(_sig)); }
        mapper_direction direction() const
            { return mapper_signal_direction(_sig); }
        char type() const
            { return mapper_signal_type(_sig); }
        int length() const
            { return mapper_signal_length(_sig); }

        mapper_signal_group group()
            { return mapper_signal_signal_group(_sig); }
        void set_group(mapper_signal_group group)
            { mapper_signal_set_group(_sig, group); }

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
            { return mapper_signal_value(_sig, (mapper_timetag_t*)tt); }
        int query_remotes() const
            { return mapper_signal_query_remotes(_sig, MAPPER_NOW); }
        int query_remotes(Timetag tt) const
            { return mapper_signal_query_remotes(_sig, *tt); }
        Signal& set_user_data(void *user_data)
            { mapper_signal_set_user_data(_sig, user_data); return (*this); }
        void *user_data() const
            { return mapper_signal_user_data(_sig); }
        Signal& set_callback(mapper_signal_update_handler *h)
            { mapper_signal_set_callback(_sig, h); return (*this); }
        int num_maps(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_signal_num_maps(_sig, dir); }
        Property minimum() const
        {
            void *value = mapper_signal_minimum(_sig);
            if (value)
                return Property("minimum", mapper_signal_length(_sig),
                                mapper_signal_type(_sig), value);
            else
                return Property("minimum", 0, 0, 0);
        }
        Signal& set_minimum(void *value)
            { mapper_signal_set_minimum(_sig, value); return (*this); }
        Property maximum() const
        {
            void *value = mapper_signal_maximum(_sig);
            if (value)
                return Property("maximum", mapper_signal_length(_sig),
                                mapper_signal_type(_sig), value);
            else
                return Property("maximum", 0, 0, 0);
        }
        Signal& set_maximum(void *value)
            { mapper_signal_set_maximum(_sig, value); return (*this); }
        float rate()
            { return mapper_signal_rate(_sig); }
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
            Instance& update(int *value, int count, Timetag tt)
            {
                if (mapper_signal_type(_sig) == 'i')
                    mapper_signal_instance_update(_sig, _id, value, count, *tt);
                return (*this);
            }
            Instance& update(float *value, int count, Timetag tt)
            {
                if (mapper_signal_type(_sig) == 'f')
                    mapper_signal_instance_update(_sig, _id, value, count, *tt);
                return (*this);
            }
            Instance& update(double *value, int count, Timetag tt)
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
            {
                mapper_timetag_t *_tt = tt;
                return mapper_signal_instance_value(_sig, _id, _tt);
            }
        protected:
            friend class Signal;
        private:
            mapper_id _id;
            mapper_signal _sig;
        };
        Instance instance()
        {
            mapper_id id = mapper_device_generate_unique_id(mapper_signal_device(_sig));
            // TODO: wait before activating instance?
            mapper_signal_instance_set_user_data(_sig, id, 0);
            return Instance(_sig, id);
        }
        Instance instance(mapper_id id)
        {
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
        Instance oldest_active_instance()
        {
            return Instance(_sig,
                            mapper_signal_oldest_active_instance(_sig));
        }
        Instance newest_active_instance()
        {
            return Instance(_sig,
                            mapper_signal_newest_active_instance(_sig));
        }
        int num_instances() const
            { return mapper_signal_num_instances(_sig); }
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
                                            int flags)
        {
            mapper_signal_set_instance_event_callback(_sig, h, flags);
            return (*this);
        }

        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            QUERY_METHODS(Signal, signal);
        private:
            mapper_signal *_query;
        };
    };

    class Device : public Object
    {
    public:
        Device(const string_type &name_prefix, int port, const Network& net)
        {
            _dev = mapper_device_new(name_prefix, port, net);
            _db = mapper_device_database(_dev);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        Device(const string_type &name_prefix)
        {
            _dev = mapper_device_new(name_prefix, 0, 0);
            _db = mapper_device_database(_dev);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        Device(const Device& orig) {
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
            _owned = false;
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
            { mapper_device_set_user_data(_dev, user_data); return (*this); }
        void *user_data() const
            { return mapper_device_user_data(_dev); }

        Signal add_signal(mapper_direction dir, int num_instances,
                          const string_type &name, int length, char type,
                          const string_type &unit=0,
                          void *minimum=0, void *maximum=0,
                          mapper_signal_update_handler handler=0,
                          void *user_data=0)
        {
            return Signal(mapper_device_add_signal(_dev, dir, num_instances,
                                                   name, length, type,
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
        Device& remove_signal(Signal& sig)
            { mapper_device_remove_signal(_dev, sig); return (*this); }
        mapper_signal_group add_signal_group()
            { return mapper_device_add_signal_group(_dev); }
        void remove_signal_group(mapper_signal_group group)
            { mapper_device_remove_signal_group(_dev, group); }

        PROPERTY_METHODS(Device, device, _dev);
        const Device& push() const
            { mapper_device_push(_dev); return (*this); }

        Network network() const
            { return Network(mapper_device_network(_dev)); }

        int num_signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_device_num_signals(_dev, dir); }
        int num_links(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_device_num_links(_dev, dir); }
        int num_maps(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_device_num_maps(_dev, dir); }

        Signal signal(const string_type& name)
            { return Signal(mapper_device_signal_by_name(_dev, name)); }
        Signal signal(mapper_id id)
            { return Signal(mapper_device_signal_by_id(_dev, id)); }
        Signal::Query signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Signal::Query(mapper_device_signals(_dev, dir)); }

        Device& set_link_callback(mapper_device_link_handler h)
            { mapper_device_set_link_callback(_dev, h); return (*this); }
        Device& set_map_callback(mapper_device_map_handler h)
            { mapper_device_set_map_callback(_dev, h); return (*this); }
        Link link(Device remote)
        {
            return Link(mapper_device_link_by_remote_device(_dev, remote._dev));
        }
        Link::Query links(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Link::Query(mapper_device_links(_dev, dir)); }
        Map::Query maps(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Map::Query(mapper_device_maps(_dev, dir)); }

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
        Device& start_queue(Timetag tt)
            { mapper_device_start_queue(_dev, *tt); return (*this); }
        Device& send_queue(Timetag tt)
            { mapper_device_send_queue(_dev, *tt); return (*this); }
//        lo::Server lo_server()
//            { return lo::Server(mapper_device_lo_server(_dev)); }

        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            QUERY_METHODS(Device, device);
        private:
            mapper_device *_query;
        };
    private:
        mapper_device _dev;
        mapper_database _db;
        bool _owned;
        int* _refcount_ptr;
        int incr_refcount()
            { return _refcount_ptr ? ++(*_refcount_ptr) : 0; }
        int decr_refcount()
            { return _refcount_ptr ? --(*_refcount_ptr) : 0; }
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
    public:
        Database(Network& net, int flags = MAPPER_OBJ_ALL)
        {
            _db = mapper_database_new(net, flags);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        Database(int flags = MAPPER_OBJ_ALL)
        {
            _db = mapper_database_new(0, flags);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        Database(const Database& orig)
        {
            _db = orig._db;
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            if (_owned)
                incr_refcount();
        }
        Database(mapper_database db)
        {
            _db = db;
            _owned = false;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        ~Database()
        {
            if (_owned && _db && decr_refcount() <= 0)
                mapper_database_free(_db);
        }
        int poll(int block_ms=0) const
            { return mapper_database_poll(_db, block_ms); }
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
            { mapper_database_request_devices(_db); return (*this); }
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

        int num_devices() const
            { return mapper_database_num_devices(_db); }
        Device device(mapper_id id) const
            { return Device(mapper_database_device_by_id(_db, id)); }
        Device device(const string_type &name) const
            { return Device(mapper_database_device_by_name(_db, name)); }
        Device::Query devices() const
            { return Device::Query(mapper_database_devices(_db)); }
        Device::Query devices(const string_type &name) const
        {
            return Device::Query(mapper_database_devices_by_name(_db, name));
        }
        Device::Query devices(const Property& p, mapper_op op) const
        {
            return Device::Query(
                mapper_database_devices_by_property(_db, p.name, p.length,
                                                    p.type, p.value, op));
        }
        inline Device::Query devices(const Property& p) const
            { return devices(p, MAPPER_OP_EXISTS); }

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

        int num_signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_database_num_signals(_db, dir); }
        Signal signal(mapper_id id) const
            { return Signal(mapper_database_signal_by_id(_db, id)); }
        Signal::Query signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Signal::Query(mapper_database_signals(_db, dir)); }
        Signal::Query signals(const string_type &name) const
        {
            return Signal::Query(mapper_database_signals_by_name(_db, name));
        }
        Signal::Query signals(const Property& p, mapper_op op) const
        {
            return Signal::Query(
                mapper_database_signals_by_property(_db, p.name, p.length,
                                                    p.type, p.value, op));
        }
        inline Signal::Query signals(const Property& p) const
            { return signals(p, MAPPER_OP_EXISTS); }

        // database links
        const Database& add_link_callback(mapper_database_link_handler *h,
                                          void *user_data) const
        {
            mapper_database_add_link_callback(_db, h, user_data);
            return (*this);
        }
        const Database& remove_link_callback(mapper_database_link_handler *h,
                                             void *user_data) const
        {
            mapper_database_remove_link_callback(_db, h, user_data);
            return (*this);
        }

        int num_links() const
            { return mapper_database_num_links(_db); }
        Link link(mapper_id id) const
            { return Link(mapper_database_link_by_id(_db, id)); }
        Link::Query links() const
            { return Link::Query(mapper_database_links(_db)); }
        Link::Query links(const Property& p, mapper_op op) const
        {
            return Link::Query(
                mapper_database_links_by_property(_db, p.name, p.length, p.type,
                                                  p.value, op));
        }
        inline Link::Query links(const Property& p) const
            { return links(p, MAPPER_OP_EXISTS); }

        // database maps
        const Database& add_map_callback(mapper_database_map_handler *h,
                                         void *user_data) const
        {
            mapper_database_add_map_callback(_db, h, user_data);
            return (*this);
        }
        const Database& remove_map_callback(mapper_database_map_handler *h,
                                      void *user_data) const
        {
            mapper_database_remove_map_callback(_db, h, user_data);
            return (*this);
        }

        int num_maps() const
            { return mapper_database_num_maps(_db); }
        Map map(mapper_id id) const
            { return Map(mapper_database_map_by_id(_db, id)); }
        Map::Query maps() const
            { return Map::Query(mapper_database_maps(_db)); }
        Map::Query maps(const Property& p, mapper_op op) const
        {
            return Map::Query(
                mapper_database_maps_by_property(_db, p.name, p.length, p.type,
                                                 p.value, op));
        }
        inline Map::Query maps(const Property& p) const
            { return maps(p, MAPPER_OP_EXISTS); }
    private:
        mapper_database _db;
        bool _owned;
        int* _refcount_ptr;
        int incr_refcount()
            { return _refcount_ptr ? ++(*_refcount_ptr) : 0; }
        int decr_refcount()
            { return _refcount_ptr ? --(*_refcount_ptr) : 0; }
    };

    Device Link::device(int idx) const
        { return Device(mapper_link_device(_link, idx)); }

    signal_type::signal_type(const Signal& sig)
        { _sig = (mapper_signal)sig; }

    Signal Map::Slot::signal() const
        { return Signal(mapper_slot_signal(_slot)); }

    Device Signal::device() const
        { return Device(mapper_signal_device(_sig)); }

    inline std::string version()
        { return std::string(mapper_version()); }
};

#endif // _MAPPER_CPP_H_
