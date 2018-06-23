
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
#include <iostream>

/* TODO:
 *      signal update handlers
 *      instance event handlers
 *      graph handlers
 *      device mapping handlers
 */

//signal_update_handler(Signal sig, instance_id, val, len, Time)
//- optional: instance_id, time, len
//
//possible forms:
//(Signal sig, void *val)
//(Signal sig, void *val, Time time)
//(Signal sig, int instance_id, void *val)
//(Signal sig, int instance_id, void *val, Time time)
//(Signal sig, void *val, int len)
//(Signal sig, void *val, int len, Time time)
//(Signal sig, int instance_id, void *val, int len)
//(Signal sig, int instance_id, void *val, int len, Time time)

#define MAPPER_TYPE(NAME) mapper_ ## NAME
#define MAPPER_FUNC(OBJ, FUNC) mapper_ ## OBJ ## _ ## FUNC

#define OBJECT_METHODS(CLASS_NAME)                                          \
protected:                                                                  \
    void set_prop(Property *p)                                              \
    {                                                                       \
        mapper_object_set_prop(_obj, p->prop, p->name, p->len, p->type,     \
                               p->val, p->publish);                         \
    }                                                                       \
public:                                                                     \
    template <typename... Values>                                           \
    CLASS_NAME& set_prop(Values... vals)                                    \
    {                                                                       \
        Property p(vals...);                                                \
        if (p)                                                              \
            set_prop(&p);                                                   \
        return (*this);                                                     \
    }                                                                       \
    CLASS_NAME& remove_prop(mapper_property prop) override                  \
    {                                                                       \
        mapper_object_remove_prop(_obj, prop, NULL);                        \
        return (*this);                                                     \
    }                                                                       \
    CLASS_NAME& remove_prop(const string_type &name) override               \
    {                                                                       \
        if (name)                                                           \
            mapper_object_remove_prop(_obj, MAPPER_PROP_UNKNOWN, name);     \
        return (*this);                                                     \
    }                                                                       \
    const CLASS_NAME& push() const override                                 \
    {                                                                       \
        mapper_object_push(_obj);                                           \
        return (*this);                                                     \
    }                                                                       \

#define OBJECT_LIST_METHODS(CLASS_NAME)                                     \
public:                                                                     \
    List(mapper_object* obj) : Object::List(obj) {}                         \
    List(const List& orig)  : Object::List(orig) {}                         \
    virtual CLASS_NAME operator*()                                          \
    {                                                                       \
        return CLASS_NAME(*_list);                                          \
    }                                                                       \
    /*! Retrieve an indexed item in the List.                            */ \
    /*  \param idx           The index of the element to retrieve.       */ \
    /*  \return              The retrieved Object.                       */ \
    virtual CLASS_NAME operator [] (int idx)                                \
    {                                                                       \
        return CLASS_NAME(mapper_object_list_get_index(_list, idx));        \
    }                                                                       \
    /*! Convert this List to a std::vector of CLASS_NAME.                */ \
    /*  \return              The converted List results.                 */ \
    virtual operator std::vector<CLASS_NAME>() const                        \
    {                                                                       \
        std::vector<CLASS_NAME> vec;                                        \
        /* use a copy */                                                    \
        mapper_object *cpy = mapper_object_list_copy(_list);                \
        while (cpy) {                                                       \
            vec.push_back(CLASS_NAME(*cpy));                                \
            cpy = mapper_object_list_next(cpy);                             \
        }                                                                   \
        return vec;                                                         \
    }                                                                       \

namespace mapper {

    class Device;
    class Signal;
    class Map;
    class Object;
    class Property;
    class Graph;

    // Helper class to allow polymorphism on "const char *" and "std::string".
    class string_type {
    public:
        string_type(const char *s=0) { _s = s; }
        string_type(const std::string& s) { _s = s.c_str(); }
        operator const char*() const { return _s; }
        const char *_s;
    };

    /*! libmapper uses NTP timetags for communication and synchronization. */
    class Time
    {
    public:
        Time(mapper_time_t time)
            { _time.sec = time.sec; _time.frac = time.frac; }
        Time(unsigned long int sec, unsigned long int frac)
            { _time.sec = sec; _time.frac = frac; }
        Time(double seconds)
            { mapper_time_set_double(&_time, seconds); }
        Time()
            { mapper_time_now(&_time); }
        uint32_t sec()
            { return _time.sec; }
        Time& set_sec(uint32_t sec)
            { _time.sec = sec; return (*this); }
        uint32_t frac()
            { return _time.frac; }
        Time& set_frac (uint32_t frac)
            { _time.frac = frac; return (*this); }
        Time& now()
            { mapper_time_now(&_time); return (*this); }
        operator mapper_time_t*()
            { return &_time; }
        operator double() const
            { return mapper_time_get_double(_time); }
        Time& operator=(Time& time)
            { mapper_time_copy(&_time, time._time); return (*this); }
        Time& operator=(double d)
            { mapper_time_set_double(&_time, d); return (*this); }
        Time operator+(Time& addend)
        {
            mapper_time_t temp;
            mapper_time_copy(&temp, _time);
            mapper_time_add(&temp, *(mapper_time_t*)addend);
            return temp;
        }
        Time operator-(Time& subtrahend)
        {
            mapper_time_t temp;
            mapper_time_copy(&temp, _time);
            mapper_time_subtract(&temp, *(mapper_time_t*)subtrahend);
            return temp;
        }
        Time& operator+=(Time& addend)
        {
            mapper_time_add(&_time, *(mapper_time_t*)addend);
            return (*this);
        }
        Time& operator+=(double addend)
            { mapper_time_add_double(&_time, addend); return (*this); }
        Time& operator-=(Time& subtrahend)
        {
            mapper_time_subtract(&_time, *(mapper_time_t*)subtrahend);
            return (*this);
        }
        Time& operator-=(double subtrahend)
            { mapper_time_add_double(&_time, -subtrahend); return (*this); }
        Time& operator*=(double multiplicand)
            { mapper_time_multiply(&_time, multiplicand); return (*this); }
        bool operator<(Time& rhs)
        {
            return (_time.sec < rhs._time.sec
                    || (_time.sec == rhs._time.sec && _time.frac < rhs._time.frac));
        }
        bool operator<=(Time& rhs)
        {
            return (_time.sec < rhs._time.sec
                    || (_time.sec == rhs._time.sec && _time.frac <= rhs._time.frac));
        }
        bool operator==(Time& rhs)
        {
            return (_time.sec == rhs._time.sec && _time.frac == rhs._time.frac);
        }
        bool operator>=(Time& rhs)
        {
            return (_time.sec > rhs._time.sec
                    || (_time.sec == rhs._time.sec && _time.frac >= rhs._time.frac));
        }
        bool operator>(Time& rhs)
        {
            return (_time.sec > rhs._time.sec
                    || (_time.sec == rhs._time.sec && _time.frac > rhs._time.frac));
        }
    private:
        mapper_time_t _time;
    };

    class Property
    {
    public:
        template <typename T>
        Property(mapper_property _prop, T _val, bool _publish=true)
        {
            prop = _prop;
            name = NULL;
            owned = false;
            _set(_val);
            publish = _publish;
        }
        template <typename T>
        Property(const string_type &_name, T _val, bool _publish=true)
        {
            prop = MAPPER_PROP_UNKNOWN;
            name = _name;
            owned = false;
            _set(_val);
            publish = _publish;
        }
        template <typename T>
        Property(mapper_property _prop, int _len, T& _val, bool _publish=true)
        {
            prop = _prop;
            name = NULL;
            owned = false;
            _set(_len, _val);
        }
        template <typename T>
        Property(const string_type &_name, int _len, T& _val, bool _publish=true)
        {
            prop = MAPPER_PROP_UNKNOWN;
            name = _name;
            owned = false;
            _set(_len, _val);
        }
        template <typename T, size_t N>
        Property(mapper_property _prop, std::array<T, N> _val, bool _publish=true)
        {
            prop = _prop;
            name = NULL;
            owned = false;
            _set(_val);
        }
        template <typename T, size_t N>
        Property(const string_type &_name, std::array<T, N> _val, bool _publish=true)
        {
            prop = MAPPER_PROP_UNKNOWN;
            name = _name;
            owned = false;
            _set(_val);
        }
        template <typename T>
        Property(mapper_property _prop, std::vector<T> _val, bool _publish=true)
        {
            prop = _prop;
            name = NULL;
            owned = false;
            _set(_val);
        }
        template <typename T>
        Property(const string_type &_name, std::vector<T> _val, bool _publish=true)
        {
            prop = MAPPER_PROP_UNKNOWN;
            name = _name;
            owned = false;
            _set(_val);
        }
        template <typename T>
        Property(mapper_property _prop, int _len, char _type, T& _val,
                 bool _publish=true)
        {
            prop = _prop;
            name = NULL;
            owned = false;
            _set(_len, _type, _val);
        }
        template <typename T>
        Property(const string_type &_name, int _len, char _type, T& _val,
                 bool _publish=true)
        {
            prop = MAPPER_PROP_UNKNOWN;
            name = _name;
            owned = false;
            _set(_len, _type, _val);
        }

        ~Property()
            { maybe_free(); }

        template <typename T>
        operator const T() const
            { return *(const T*)val; }
        operator const bool() const
        {
            if (!len || !type)
                return false;
            switch (type) {
                case MAPPER_INT32:
                    return *(int*)val != 0;
                    break;
                case MAPPER_FLOAT:
                    return *(float*)val != 0.f;
                    break;
                case MAPPER_DOUBLE:
                    return *(double*)val != 0.;
                    break;
                default:
                    return val != 0;
            }
        }
        template <typename T>
        operator const T*() const
            { return (const T*)val; }
        operator const char**() const
            { return (const char**)val; }
        template <typename T, size_t N>
        operator const std::array<T, N>() const
        {
            std::array<T, N> temp_a;
            for (size_t i = 0; i < N && i < len; i++)
                temp_a[i] = ((T*)val)[i];
            return temp_a;
        }
        template <size_t N>
        operator const std::array<const char *, N>() const
        {
            std::array<const char*, N> temp_a;
            if (len == 1)
                temp_a[0] = (const char*)val;
            else {
                const char **tempp = (const char**)val;
                for (size_t i = 0; i < N && i < len; i++) {
                    temp_a[i] = tempp[i];
                }
            }
            return temp_a;
        }
        template <size_t N>
        operator const std::array<std::string, N>() const
        {
            std::array<std::string, N> temp_a;
            if (len == 1)
                temp_a[0] = std::string((const char*)val);
            else {
                const char **tempp = (const char**)val;
                for (size_t i = 0; i < N && i < len; i++) {
                    temp_a[i] = std::string(tempp[i]);
                }
            }
            return temp_a;
        }
        template <typename T>
        operator const std::vector<T>() const
        {
            std::vector<T> temp_v;
            for (int i = 0; i < len; i++)
                temp_v.push_back(((T*)val)[i]);
            return temp_v;
        }
        operator const std::vector<const char *>() const
        {
            std::vector<const char*> temp_v;
            if (len == 1)
                temp_v.push_back((const char*)val);
            else {
                const char **tempp = (const char**)val;
                for (unsigned int i = 0; i < len; i++)
                    temp_v.push_back(tempp[i]);
            }
            return temp_v;
        }
        operator const std::vector<std::string>() const
        {
            std::vector<std::string> temp_v;
            if (len == 1)
                temp_v.push_back(std::string((const char*)val));
            else {
                const char **tempp = (const char**)val;
                for (unsigned int i = 0; i < len; i++)
                    temp_v.push_back(std::string(tempp[i]));
            }
            return temp_v;
        }
//        friend std::ostream& operator<<(std::ostream& os, const Property& p);

        mapper_property prop;
        const char *name;
        mapper_type type;
        unsigned int len;
        const void *val;
        bool publish;
    protected:
        friend class Graph;
        friend class Object;
        Property(mapper_property _prop, const string_type &_name, int _len,
                 char _type, const void *_val)
        {
            prop = _prop;
            name = _name;
            _set(_len, _type, _val);
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
            if (owned && val) {
                if (type == MAPPER_STRING && len > 1) {
                    for (unsigned int i = 0; i < len; i++) {
                        free(((char**)val)[i]);
                    }
                }
                free((void*)val);
                owned = false;
            }
        }
        void _set(int _len, bool _val[])
        {
            int *ival = (int*)malloc(sizeof(int)*_len);
            if (!ival)
                return;
            for (int i = 0; i < _len; i++)
                ival[i] = (int)_val[i];
            val = ival;
            len = _len;
            type = MAPPER_INT32;
            owned = true;
        }
        void _set(int _len, int _val[])
            { val = _val; len = _len; type = MAPPER_INT32; }
        void _set(int _len, float _val[])
            { val = _val; len = _len; type = MAPPER_FLOAT; }
        void _set(int _len, double _val[])
            { val = _val; len = _len; type = MAPPER_DOUBLE; }
        void _set(int _len, char _val[])
            { val = _val; len = _len; type = MAPPER_CHAR; }
        void _set(int _len, const char *_val[])
        {
            len = _len;
            type = MAPPER_STRING;
            if (_len == 1)
                val = _val[0];
            else
                val = _val;
        }
        template <typename T>
        void _set(T _val)
        {
            memcpy(&_scalar, &_val, sizeof(_scalar));
            _set(1, (T*)&_scalar);
        }
        template <typename T, size_t N>
        void _set(std::array<T, N>& _val)
        {
            if (!_val.empty())
                _set(N, _val.data());
            else
                len = 0;
        }
        template <size_t N>
        void _set(std::array<const char*, N>& _vals)
        {
            len = N;
            type = MAPPER_STRING;
            if (len == 1) {
                val = strdup(_vals[0]);
            }
            else if (len > 1) {
                // need to copy string array
                val = (char**)malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++) {
                    ((char**)val)[i] = strdup((char*)_vals[i]);
                }
                owned = true;
            }
        }
        template <size_t N>
        void _set(std::array<std::string, N>& _vals)
        {
            len = N;
            type = MAPPER_STRING;
            if (len == 1) {
                val = strdup(_vals[0].c_str());
            }
            else if (len > 1) {
                // need to copy string array
                val = (char**)malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++) {
                    ((char**)val)[i] = strdup((char*)_vals[i].c_str());
                }
                owned = true;
            }
        }
        void _set(int _len, std::string _vals[])
        {
            len = _len;
            type = MAPPER_STRING;
            if (len == 1) {
                val = strdup(_vals[0].c_str());
            }
            else if (len > 1) {
                // need to copy string array
                val = malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++) {
                    ((char**)val)[i] = strdup((char*)_vals[i].c_str());
                }
                owned = true;
            }
        }
        template <typename T>
        void _set(std::vector<T> _val)
            { _set((int)_val.size(), _val.data()); }
        void _set(std::vector<const char*>& _val)
        {
            len = (int)_val.size();
            type = MAPPER_STRING;
            if (len == 1)
                val = strdup(_val[0]);
            else {
                // need to copy string array since std::vector may free it
                val = malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++) {
                    ((char**)val)[i] = strdup((char*)_val[i]);
                }
                owned = true;
            }
        }
        void _set(std::vector<std::string>& _val)
        {
            len = (int)_val.size();
            type = MAPPER_STRING;
            if (len == 1) {
                val = strdup(_val[0].c_str());
            }
            else if (len > 1) {
                // need to copy string array
                val = malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++) {
                    ((char**)val)[i] = strdup((char*)_val[i].c_str());
                }
                owned = true;
            }
        }
        void _set(int _len, char _type, const void *_val)
        {
            type = _type;
            val = _val;
            len = _len;
        }
    };

    class Object
    {
    protected:
        int* _refcount_ptr;
        int incr_refcount()
            { return _refcount_ptr ? ++(*_refcount_ptr) : 0; }
        int decr_refcount()
            { return _refcount_ptr ? --(*_refcount_ptr) : 0; }
        bool _owned;

        Object(mapper_object obj) { _obj = obj; }

        friend class Property;
        void set_prop(Property *p)
        {
            mapper_object_set_prop(_obj, p->prop, p->name, p->len, p->type,
                                   p->val, p->publish);
        }

        mapper_object _obj;
    public:
        operator mapper_object() const
            { return _obj; }

        /*! Get the specific type of an Object.
         *  \return         Object type. */
        mapper_object_type type() const
            { return mapper_object_get_type(_obj); }

        /*! Get the underlying Graph.
         *  \return         Graph. */
        inline Graph graph() const;

        /*! Set arbitrary properties for an Object.
         *  \param vals     The Properties to add or modify.
         *  \return         Self. */
        template <typename... Values>
        Object& set_prop(Values... vals)
        {
            Property p(vals...);
            if (p)
                set_prop(&p);
            return (*this);
        }

        /*! Remove a Property from a CLASS_NAME by symbolic identifier.
         *  \param prop    The Property to remove.
         *  \return        Self. */
        virtual Object& remove_prop(mapper_property prop)
        {
            mapper_object_remove_prop(_obj, prop, NULL);
            return (*this);
        }

        /*! Remove a named Property from a CLASS_NAME.
         *  \param name    The Property to remove.
         *  \return        Self. */
        virtual Object& remove_prop(const string_type &name)
        {
            if (name)
                mapper_object_remove_prop(_obj, MAPPER_PROP_UNKNOWN, name);
            return (*this);
        }

        /*! Push "staged" property changes out to the distributed graph.
         *  \return         Self. */
        virtual const Object& push() const
        {
            mapper_object_push(_obj);
            return (*this);
        }

        /*! Retrieve the number of Properties owned by an Object.
         *  \param name     The name of the Property to check.
         *  \return         The number of Properties. */
        int num_props(bool staged = false) const
            { return mapper_object_get_num_props(_obj, staged); }

        /*! Retrieve a Property by name.
         *  \param name     The name of the Property to retrieve.
         *  \return         The retrieved Property. */
        Property property(const string_type &name=NULL) const
        {
            mapper_property prop;
            mapper_type type;
            const void *val;
            int len;
            prop = mapper_object_get_prop_by_name(_obj, name, &len, &type, &val);
            return Property(prop, name, len, type, val);
        }

        Property operator [] (const string_type &name) const
            { return property(name); }

        /*! Retrieve a Property by index.
         *  \param index    The index of or symbolic identifier of the Property
         *                  to retrieve.
         *  \return         The retrieved Property. */
        Property property(mapper_property prop) const
        {
            const char *name;
            mapper_type type;
            const void *val;
            int len;
            prop = mapper_object_get_prop_by_index(_obj, prop, &name, &len,
                                                   &type, &val);
            return Property(prop, name, len, type, val);
        }

        Property operator [] (mapper_property prop) const
            { return property(prop); }

        /*! List objects provide a lazily-computed iterable list of results
         *  from running queries against a mapper::Graph. */
        class List : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            List(mapper_object *list)
            { _list = list; }
            /* override copy constructor */
            List(const List& orig)
                { _list = mapper_object_list_copy(orig._list); }
            ~List()
                { mapper_object_list_free(_list); }

            Object operator*()
                { return Object(*_list); }
            Object operator [] (int idx)
                { return Object(mapper_object_list_get_index(_list, idx)); }
            virtual operator std::vector<Object>() const
            {
                std::vector<Object> vec;
                /* use a copy */
                mapper_object *cpy = mapper_object_list_copy(_list);
                while (cpy) {
                    vec.push_back(Object(*cpy));
                    cpy = mapper_object_list_next(cpy);
                }
                return vec;
            }

            bool operator==(const List& rhs)
                { return (_list == rhs._list); }
            bool operator!=(const List& rhs)
                { return (_list != rhs._list); }
            List& operator++()
            {
                if (_list)
                    _list = mapper_object_list_next(_list);
                return (*this);
            }
            List operator++(int)
                {
                    List tmp(*this); operator++(); return tmp; }
            List begin()
                { return (*this); }
            List end()
                { return List(0); }

            int length()
                { return mapper_object_list_get_length(_list); }

            /* Combination functions */
            /*! Add items found in List rhs from this List (without duplication).
             *  \param rhs          A second List.
             *  \return             Self. */
            List& join(const List& rhs)
            {
                /* need to use copy of rhs List */
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                _list = mapper_object_list_union(_list, rhs_cpy);
                return (*this);
            }

            /*! Remove items NOT found in List rhs from this List
             *  \param rhs          A second List.
             *  \return             Self. */
            List& intersect(const List& rhs)
            {
                /* need to use copy of rhs List */
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                _list = mapper_object_list_intersection(_list, rhs_cpy);
                return (*this);
            }

            /*! Filter items NOT found this List
             *  \param p            Property to match.
             *  \param op           The comparison operator.
             *  \return             Self. */
            List& filter(const Property& p, mapper_op op)
            {
                _list = mapper_object_list_filter(_list, p.prop, p.name,
                                                  p.len, p.type, p.val, op);
                return (*this);
            }

            /*! Remove items found in List rhs from this List
             *  \param rhs          A second list.
             *  \return             Self. */
            List& subtract(const List& rhs)
            {
                /* need to use copy of rhs List */
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                _list = mapper_object_list_difference(_list, rhs_cpy);
                return (*this);
            }

            /*! Add items found in List rhs from this List (without duplication).
             *  \param rhs          A second List.
             *  \return             A new List containing the results. */
            List operator+(const List& rhs) const
            {
                /* need to use copies of both lists */
                mapper_object *lhs_cpy = mapper_object_list_copy(_list);
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                return List(mapper_object_list_union(lhs_cpy, rhs_cpy));
            }

            /*! Remove items NOT found in List rhs from this List
             *  \param rhs          A second List.
             *  \return             A new List containing the results. */
            List operator*(const List& rhs) const
            {
                /* need to use copies of both lists */
                mapper_object *lhs_cpy = mapper_object_list_copy(_list);
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                return List(mapper_object_list_intersection(lhs_cpy, rhs_cpy));
            }

            /*! Remove items found in List rhs from this List
             *  \param rhs          A second List.
             *  \return             A new List containing the results. */
            List operator-(const List& rhs) const
            {
                /* need to use copies of both queries */
                mapper_object *lhs_cpy = mapper_object_list_copy(_list);
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                return List(mapper_object_list_difference(lhs_cpy, rhs_cpy));
            }

            /*! Add items found in List rhs from this List (without duplication).
             *  \param rhs          A second List.
             *  \return             Self. */
            List& operator+=(const List& rhs)
            {
                /* need to use copy of rhs List */
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                _list = mapper_object_list_union(_list, rhs_cpy);
                return (*this);
            }

            /*! Remove items NOT found in List rhs from this List
             *  \param rhs          A second List.
             *  \return             Self. */
            List& operator*=(const List& rhs)
            {
                /* need to use copy of rhs List */
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                _list = mapper_object_list_intersection(_list, rhs_cpy);
                return (*this);
            }

            /*! Remove items found in List rhs from this List
             *  \param rhs          A second List.
             *  \return             Self. */
            List& operator-=(const List& rhs)
            {
                /* need to use copy of rhs List */
                mapper_object *rhs_cpy = mapper_object_list_copy(rhs._list);
                _list = mapper_object_list_difference(_list, rhs_cpy);
                return (*this);
            }

            /*! Set properties for each Object in the List.
             *  \param vals     The Properties to add of modify.
             *  \return         Self. */
            template <typename... Values>
            List& set_prop(Values... vals)
            {
                Property p(vals...);
                if (!p)
                    return (*this);
                /* use a copy */
                mapper_object *cpy = mapper_object_list_copy(_list);
                while (cpy) {
                    mapper_object_set_prop(*cpy, p.prop, p.name, p.len, p.type,
                                           p.val, p.publish);
                    cpy = mapper_object_list_next(cpy);
                }
                return (*this);
            }
        protected:
            mapper_object *_list;
        };
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

    /*! Maps define dataflow connections between sets of Signals. A Map consists
     *  of one or more source Signals, one or more destination Signal (currently),
     *  restricted to one) and properties which determine how the source data is
     *  processed.*/
    class Map : public Object
    {
    public:
        Map(const Map& orig) : Object(orig._obj) {}
        Map(mapper_map map) : Object(map) {}

        /*! Create a map between a pair of Signals.
         *  \param src          Source Signal.
         *  \param dst          Destination Signal object.
         *  \return             A new Map object – either loaded from the
         *                      Graph (if the Map already existed) or newly
         *                      created. In the latter case the Map will not
         *                      take effect until it has been added to the
         *                      distributed graph using push(). */
        Map(signal_type src, signal_type dst) : Object(NULL)
        {
            mapper_signal cast_src = src, cast_dst = dst;
            _obj = mapper_map_new(1, &cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of Signals.
         *  \param num_srcs     The number of source signals in this map.
         *  \param srcs         Array of source Signal objects.
         *  \param num_dsts     The number of destination signals in this map.
         *                      Currently restricted to 1.
         *  \param dsts         Array of destination Signal objects.
         *  \return             A new Map object – either loaded from the
         *                      Graph (if the Map already existed) or newly
         *                      created. In the latter case the Map will not
         *                      take effect until it has been added to the
         *                      distributed graph using push(). */
        Map(int num_srcs, signal_type srcs[],
            int num_dsts, signal_type dsts[]) : Object(NULL)
        {
            mapper_signal cast_src[num_srcs], cast_dst = dsts[0];
            for (int i = 0; i < num_srcs; i++) {
                cast_src[i] = srcs[i];
            }
            _obj = mapper_map_new(num_srcs, cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of Signals.
         *  \param srcs         std::array of source Signal objects.
         *  \param dsts         std::array of destination Signal objects.
         *  \return             A new Map object – either loaded from the
         *                      Graph (if the Map already existed) or newly
         *                      created. In the latter case the Map will not
         *                      take effect until it has been added to the
         *                      distributed graph using push(). */
        template <size_t N, size_t M>
        Map(std::array<signal_type, N>& srcs,
            std::array<signal_type, M>& dsts) : Object(NULL)
        {
            if (srcs.empty() || dsts.empty() || M != 1) {
                _obj = 0;
                return;
            }
            mapper_signal cast_src[N], cast_dst = dsts.data()[0];
            for (int i = 0; i < N; i++) {
                cast_src[i] = srcs.data()[i];
            }
            _obj = mapper_map_new(N, cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of Signals.
         *  \param srcs         std::vector of source Signal objects.
         *  \param dsts         std::vector of destination Signal objects.
         *  \return             A new Map object – either loaded from the
         *                      Graph (if the Map already existed) or newly
         *                      created. In the latter case the Map will not
         *                      take effect until it has been added to the
         *                      distributed graph using push(). */
        template <typename T>
        Map(std::vector<T>& srcs, std::vector<T>& dsts) : Object(NULL)
        {
            if (!srcs.size() || (dsts.size() != 1)) {
                _obj = 0;
                return;
            }
            int num_srcs = srcs.size();
            mapper_signal cast_src[num_srcs], cast_dst = dsts.data()[0];
            for (int i = 0; i < num_srcs; i++) {
                cast_src[i] = srcs.data()[i];
            }
            _obj = mapper_map_new(num_srcs, cast_src, 1, &cast_dst);
        }

        /*! Return C data structure mapper_map corresponding to this object.
         *  \return     C mapper_map data structure. */
        operator mapper_map() const
            { return _obj; }

        /*! Cast to a boolean value based on whether the underlying C map
         *  exists.
         *  \return     True if mapper_map exists, otherwise false. */
        operator bool() const
            { return _obj; }

        /*! Re-create stale map if necessary.
         *  \return     Self. */
        const Map& refresh() const
            { mapper_map_refresh(_obj); return (*this); }

        /*! Release the Map between a set of Signals. */
        // this function can be const since it only sends the unmap msg
        void release() const
            { mapper_map_release(_obj); }

        /*! Get the number of Signals for this Map.
         *  \param loc      MAPPER_LOC_SRC for source signals,
         *                  MAPPER_LOC_DST for destination signals, or
         *                  MAPPER_LOC_ANY (default) for all signals.
         *  \return     The number of signals. */
        int num_signals(mapper_location loc=MAPPER_LOC_ANY) const
            { return mapper_map_get_num_signals(_obj, loc); }

        /*! Detect whether a Map is completely initialized.
         *  \return     True if map is completely initialized, false otherwise. */
        bool ready() const
            { return mapper_map_ready(_obj); }

//        /*! Get the scopes property for a this map.
//         *  \return     An Device::List containing the list of results.  Use
//         *              Device::List::next() to iterate. */
//        Device::List scopes() const
//            { return Device::List((void**)mapper_map_get_scopes(_obj)); }

        /*! Add a scope to this map. Map scopes configure the propagation of
         *  Signal instance updates across the Map. Changes to remote Maps will
         *  not take effect until synchronized with the distributed graph using
         *  push().
         *  \param dev      Device to add as a scope for this Map. After taking
         *                  effect, this setting will cause instance updates
         *                  originating from the specified Device to be
         *                  propagated across the Map.
         *  \return         Self. */
        inline Map& add_scope(Device dev);

        /*! Remove a scope from this Map. Map scopes configure the propagation
         *  of Signal instance updates across the Map. Changes to remote Maps
         *  will not take effect until synchronized with the distributed graph
         *  using push().
         *  \param dev      Device to remove as a scope for this Map. After
         *                  taking effect, this setting will cause instance
         *                  updates originating from the specified Device to be
         *                  blocked from propagating across the Map.
         *  \return         Self. */
        inline Map& remove_scope(Device dev);

        /*! Get the index of the Map endpoint matching a specific Signal.
         *  \param sig          The Signal to look for.
         *  \return         	The index of the signal in this map, or -1 if
         *                      not found. */
        int index(signal_type sig) const
            { return mapper_map_get_signal_index(_obj, (mapper_signal)sig); }

        /*! Retrieve a Signal from this Map.
         *  \param index        The signal index.
         *  \return             The Signal object. */
        Signal signal(mapper_location loc, int index = 0) const;

        Signal source(int index = 0) const;

        Signal destination(int index = 0) const;

        std::vector<Signal> signals(mapper_location loc = MAPPER_LOC_ANY) const;

        OBJECT_METHODS(Map);

        class List : public Object::List
        {
            OBJECT_LIST_METHODS(Map);

            /*! Release each Map in the List.
             *  \return             Self. */
            List& release()
            {
                // use a copy
                mapper_object *cpy = mapper_object_list_copy(_list);
                while (cpy) {
                    if (MAPPER_OBJ_MAP == mapper_object_get_type(*cpy))
                        mapper_map_release((mapper_map)*cpy);
                    cpy = mapper_object_list_next(cpy);
                }
                return (*this);
            }
        };

    protected:
        friend class Graph;
    };

    /*! Signals define inputs or outputs for Devices.  A Signal consists of a
     *  scalar or vector value of some integer or floating-point type.  A
     *  Signal is created by adding an input or output to a Device.  It
     *  can optionally be provided with some metadata such as a range, unit, or
     *  other properties.  Signals can be mapped by creating Maps using remote
     *  requests on the network, usually generated by a standalone GUI. */
    class Signal : public Object
    {
    public:
        Signal(mapper_signal sig) : Object(sig) {}
        operator mapper_signal() const
            { return _obj; }
        operator bool() const
            { return _obj ? true : false; }
        inline Device device() const;
        Map::List maps(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Map::List(mapper_signal_get_maps(_obj, dir)); }

        mapper_signal_group group()
            { return mapper_signal_get_group(_obj); }
        void set_group(mapper_signal_group group)
            { mapper_signal_set_group(_obj, group); }

        /* Value update functions*/
        Signal& set_value(int *val, int len, Time time)
        {
            mapper_signal_set_value(_obj, 0, len, MAPPER_INT32, val, *time);
            return (*this);
        }
        Signal& set_value(float *val, int len, Time time)
        {
            mapper_signal_set_value(_obj, 0, len, MAPPER_FLOAT, val, *time);
            return (*this);
        }
        Signal& set_value(double *val, int len, Time time)
        {
            mapper_signal_set_value(_obj, 0, len, MAPPER_DOUBLE, val, *time);
            return (*this);
        }
        template <typename T>
        Signal& set_value(T val)
            { return set_value(&val, 1, 0); }
        template <typename T>
        Signal& set_value(T* val)
            { return set_value(val, 1, 0); }
        template <typename T, int len>
        Signal& set_value(T* val)
            { return set_value(val, len, 0); }
        template <typename T>
        Signal& set_value(T* val, Time time)
            { return set_value(val, 1, time); }
        template <typename T, size_t N>
        Signal& set_value(std::array<T,N> val)
            { return set_value(&val[0], N, 0); }
        template <typename T>
        Signal& set_value(std::vector<T> val, Time time=0)
            { return set_value(&val[0], (int)val.size(), *time); }
        const void *value() const
            { return mapper_signal_get_value(_obj, 0, 0); }
        const void *value(Time time) const
            { return mapper_signal_get_value(_obj, 0, (mapper_time_t*)time); }
        Signal& set_callback(mapper_signal_update_handler *h)
            { mapper_signal_set_callback(_obj, h); return (*this); }

        class Instance {
        public:
            Instance(mapper_signal sig, mapper_id id)
                { _sig = sig; _id = id; }
            bool operator == (Instance i)
                { return (_id == i._id); }
            operator mapper_id() const
                { return _id; }
            Instance& set_value(int *val, int len, Time time)
            {
                mapper_signal_set_value(_sig, _id, len, MAPPER_INT32, val, *time);
                return (*this);
            }
            Instance& set_value(float *val, int len, Time time)
            {
                mapper_signal_set_value(_sig, _id, len, MAPPER_FLOAT, val, *time);
                return (*this);
            }
            Instance& set_value(double *val, int len, Time time)
            {
                mapper_signal_set_value(_sig, _id, len, MAPPER_DOUBLE, val, *time);
                return (*this);
            }

            void release()
                { mapper_signal_release_instance(_sig, _id, MAPPER_NOW); }
            void release(Time time)
                { mapper_signal_release_instance(_sig, _id, *time); }

            template <typename T>
            Instance& set_value(T val)
                { return set_value(&val, 1, 0); }
            template <typename T>
            Instance& set_value(T* val, int len=0)
                { return set_value(val, len, 0); }
            template <typename T>
            Instance& set_value(T* val, Time time)
                { return set_value(val, 1, time); }
            template <typename T, size_t N>
            Instance& set_value(std::array<T,N> val, Time time=0)
                { return set_value(&val[0], N, time); }
            template <typename T>
            Instance& set_value(std::vector<T> val, Time time=0)
                { return set_value(&val[0], val.size(), time); }

            mapper_id id() const
                { return _id; }

            Instance& set_user_data(void *user_data)
            {
                mapper_signal_set_instance_user_data(_sig, _id, user_data);
                return (*this);
            }
            void *user_data() const
                { return mapper_signal_get_instance_user_data(_sig, _id); }

            const void *value() const
                { return mapper_signal_get_value(_sig, _id, 0); }
            const void *value(Time time) const
            {
                mapper_time_t *_time = time;
                return mapper_signal_get_value(_sig, _id, _time);
            }
        protected:
            friend class Signal;
        private:
            mapper_id _id;
            mapper_signal _sig;
        };
        Instance instance()
        {
            mapper_id id = mapper_device_generate_unique_id(mapper_signal_get_device(_obj));
            // TODO: wait before activating instance?
            mapper_signal_set_instance_user_data(_obj, id, 0);
            return Instance(_obj, id);
        }
        Instance instance(mapper_id id)
        {
            // TODO: wait before activating instance?
            mapper_signal_set_instance_user_data(_obj, id, 0);
            return Instance(_obj, id);
        }
        Signal& reserve_instances(int num, mapper_id *ids = 0)
        {
            mapper_signal_reserve_instances(_obj, num, ids, 0);
            return (*this);
        }
        Signal& reserve_instances(int num, mapper_id *ids, void **user_data)
        {
            mapper_signal_reserve_instances(_obj, num, ids, user_data);
            return (*this);
        }
        Instance active_instance_at_index(int index) const
        {
            return Instance(_obj, mapper_signal_get_active_instance_id(_obj, index));
        }
        Signal& remove_instance(Instance instance)
            { mapper_signal_remove_instance(_obj, instance._id); return (*this); }
        Instance oldest_active_instance()
        {
            return Instance(_obj, mapper_signal_get_oldest_instance_id(_obj));
        }
        Instance newest_active_instance()
        {
            return Instance(_obj, mapper_signal_get_newest_instance_id(_obj));
        }
        int num_instances() const
            { return mapper_signal_get_num_instances(_obj); }
        int num_active_instances() const
            { return mapper_signal_get_num_active_instances(_obj); }
        int num_reserved_instances() const
            { return mapper_signal_get_num_reserved_instances(_obj); }
        Signal& set_stealing_mode(mapper_stealing_type mode)
        {
            mapper_signal_set_stealing_mode(_obj, mode);
            return (*this);
        }
        mapper_stealing_type stealing_mode() const
            { return mapper_signal_get_stealing_mode(_obj); }
        Signal& set_instance_event_callback(mapper_instance_event_handler h,
                                            int flags)
        {
            mapper_signal_set_instance_event_callback(_obj, h, flags);
            return (*this);
        }

        OBJECT_METHODS(Signal);

        class List : public Object::List
        {
            OBJECT_LIST_METHODS(Signal);
        };
    };

    /*! A Device is an entity on the network which has input and/or output
     *  Signals.  The Device is the primary interface through which a
     *  program uses libmapper.  A Device must have a name, to which a unique
     *  ordinal is subsequently appended.  It can also be given other
     *  user-specified metadata.  Device Signals can be connected, which is
     *  usually effected by requests from an external GUI. */
    class Device : public Object
    {
    public:
        /*! Allocate and initialize a Device.
         *  \param name_prefix  A short descriptive string to identify the Device.
         *                      Must not contain spaces or the slash character '/'.
         *  \param port         An optional port for starting the port allocation
         *                      scheme.
         *  \param graph        A previously allocated Graph object to use.
         *  \return             A newly allocated Device. */
        Device(const string_type &name_prefix, const Graph& graph);

        /*! Allocate and initialize a Device.
         *  \param name_prefix  A short descriptive string to identify the Device.
         *                      Must not contain spaces or the slash character '/'.
         *  \return             A newly allocated Device. */
        Device(const string_type &name_prefix) : Object(NULL)
        {
            _obj = mapper_device_new(name_prefix, 0);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        Device(const Object& dev) : Object(dev) {
            if (_owned)
                incr_refcount();
        }
        Device(mapper_device dev) : Object(dev)
        {
            _owned = false;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        ~Device()
        {
            if (_owned && _obj && decr_refcount() <= 0)
                mapper_device_free(_obj);
        }
        operator mapper_device() const
            { return _obj; }

        Signal add_signal(mapper_direction dir, int num_instances,
                          const string_type &name, int len, mapper_type type,
                          const string_type &unit=0, void *min=0, void *max=0,
                          mapper_signal_update_handler handler=0)
        {
            return Signal(mapper_device_add_signal(_obj, dir, num_instances,
                                                   name, len, type, unit,
                                                   min, max, handler));
        }
        Device& remove_signal(Signal& sig)
            { mapper_device_remove_signal(_obj, sig); return (*this); }

        mapper_signal_group add_signal_group()
            { return mapper_device_add_signal_group(_obj); }
        void remove_signal_group(mapper_signal_group group)
            { mapper_device_remove_signal_group(_obj, group); }

        Signal::List signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Signal::List(mapper_device_get_signals(_obj, dir)); }

        Map::List maps(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Map::List(mapper_device_get_maps(_obj, dir)); }

        int poll(int block_ms=0) const
            { return mapper_device_poll(_obj, block_ms); }

        bool ready() const
            { return mapper_device_ready(_obj); }
        Time start_queue(Time time=MAPPER_NOW)
            { mapper_device_start_queue(_obj, *time); return time; }
        Device& send_queue(Time time)
            { mapper_device_send_queue(_obj, *time); return (*this); }

        OBJECT_METHODS(Device);

        class List : public Object::List
        {
            OBJECT_LIST_METHODS(Device);
        };
    };

    class device_type {
    public:
        device_type(mapper_device dev) { _dev = dev; }
        device_type(const Device& dev) { _dev = (mapper_device)dev; }
        operator mapper_device() const { return _dev; }
        mapper_device _dev;
    };

    /*! Graphs are the primary interface through which a program may observe
     *  the network and store information about Devices and Signals that are
     *  present.  Each Graph stores records of Devices, Signals, and Maps,
     *  which can be queried. */
    class Graph
    {
    public:
        /*! Create a peer in the libmapper distributed graph.
         *  \param flags    Sets whether the graph should automatically
         *                  subscribe to information about Signals and Maps when
         *                  it encounters a previously-unseen Device.
         *  \return         The new Graph. */
        Graph(int flags = MAPPER_OBJ_ALL)
        {
            _graph = mapper_graph_new(flags);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        Graph(const Graph& orig)
        {
            _graph = orig._graph;
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            if (_owned)
                incr_refcount();
        }
        Graph(mapper_graph graph)
        {
            _graph = graph;
            _owned = false;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        ~Graph()
        {
            if (_owned && _graph && decr_refcount() <= 0)
                mapper_graph_free(_graph);
        }
        operator mapper_graph() const
            { return _graph; }

        /*! Specify the network interface to use.
         *  \param interface    A string specifying the name of the network
         *                      interface to use. */
        Graph& set_interface(const string_type &interface)
            { mapper_graph_set_interface(_graph, interface); return (*this); }

        /*! Return a string indicating the name of the network interface in use.
         *  \return     A string containing the name of the network interface. */
        std::string interface() const
        {
            const char *iface = mapper_graph_get_interface(_graph);
            return iface ? std::string(iface) : 0;
        }

        /*! Specify the multicast group and port to use.
         *  \param group    A string specifying the multicast group to use.
         *  \param port     The multicast port to use. */
        Graph& set_multicast_addr(const string_type &group, int port)
        {
            mapper_graph_set_multicast_addr(_graph, group, port);
            return (*this);
        }

        /*! Retrieve the multicast url currently in use.
         *  \return     A string specifying the multicast url in use. */
        std::string multicast_addr() const
            { return std::string(mapper_graph_get_multicast_addr(_graph)); }

        /*! Update a Graph.
         *  \param block_ms     The number of milliseconds to block, or 0 for
         *                      non-blocking behaviour.
         *  \return             The number of handled messages. */
        int poll(int block_ms=0) const
            { return mapper_graph_poll(_graph, block_ms); }

        // subscriptions
        /*! Send a request to the network for all active Devices to report in.
         *  \return         Self. */
        const Graph& request_devices() const
            { mapper_graph_request_devices(_graph); return (*this); }

        /*! Subscribe to information about a specific Device.
         *  \param dev      The Device of interest.
         *  \param flags    Bitflags setting the type of information of interest.
         *                  Can be a combination of MAPPER_OBJ_DEVICE,
         *                  MAPPER_OBJ_INPUT_SIGNALS, MAPPER_OBJ_OUTPUT_SIGNALS,
         *                  MAPPER_OBJ_SIGNALS, MAPPER_OBJ_INCOMING_MAPS,
         *                  MAPPER_OBJ_OUTGOING_MAPS, MAPPER_OBJ_MAPS, or simply
         *                  MAPPER_OBJ_ALL for all information.
         *  \param timeout  The desired duration in seconds for this 
         *                  subscription. If set to -1, the graph will
         *                  automatically renew the subscription until it is
         *                  freed or this function is called again.
         *  \return         Self. */
        const Graph& subscribe(const device_type& dev, int flags, int timeout)
        {
            mapper_graph_subscribe(_graph, dev, flags, timeout);
            return (*this);
        }

        /*! Subscribe to information about all discovered Devices.
         *  \param flags    Bitflags setting the type of information of interest.
         *                  Can be a combination of MAPPER_OBJ_DEVICE,
         *                  MAPPER_OBJ_INPUT_SIGNALS, MAPPER_OBJ_OUTPUT_SIGNALS,
         *                  MAPPER_OBJ_SIGNALS, MAPPER_OBJ_INCOMING_MAPS,
         *                  MAPPER_OBJ_OUTGOING_MAPS, MAPPER_OBJ_MAPS, or simply
         *                  MAPPER_OBJ_ALL for all information.
         *  \return         Self. */
        const Graph& subscribe(int flags)
            { mapper_graph_subscribe(_graph, 0, flags, -1); return (*this); }

        /*! Unsubscribe from information about a specific Device.
         *  \param dev      The Device of interest.
         *  \return         Self. */
        const Graph& unsubscribe(const device_type& dev)
        {
            mapper_graph_unsubscribe(_graph, dev);
            return (*this);
        }

        /*! Cancel all subscriptions.
         *  \return         Self. */
        const Graph& unsubscribe()
            { mapper_graph_unsubscribe(_graph, 0); return (*this); }

        // graph signals
        /*! Register a callback for when an Object record is added, updated, or
         *  removed.
         *  \param h        Callback function.
         *  \param types    Bitflags setting the type of information of interest.
         *                  Can be a combination of mapper_object_type values.
         *  \param user     A user-defined pointer to be passed to the
         *                  callback for context.
         *  \return         Self. */
        const Graph& add_callback(mapper_graph_handler *h, int types,
                                  void *user) const
        {
            mapper_graph_add_callback(_graph, h, types, user);
            return (*this);
        }

        /*! Remove an Object record callback from the Graph service.
         *  \param h        Callback function.
         *  \param user     The user context pointer that was originally
         *                  specified when adding the callback
         *  \return         Self. */
        const Graph& remove_callback(mapper_graph_handler *h, void *user) const
        {
            mapper_graph_remove_callback(_graph, h, user);
            return (*this);
        }

        // graph devices
        Device::List devices() const
            { return Device::List(mapper_graph_get_objects(_graph, MAPPER_OBJ_DEVICE)); }

        // graph signals
        Signal::List signals() const
            { return Signal::List(mapper_graph_get_objects(_graph, MAPPER_OBJ_SIGNAL)); }

        // graph maps
        Map::List maps() const
            { return Map::List(mapper_graph_get_objects(_graph, MAPPER_OBJ_MAP)); }


    private:
        mapper_graph _graph;
        int* _refcount_ptr;
        int incr_refcount()
            { return _refcount_ptr ? ++(*_refcount_ptr) : 0; }
        int decr_refcount()
            { return _refcount_ptr ? --(*_refcount_ptr) : 0; }
        bool _owned;
    };

    Graph Object::graph() const
        { return Graph(mapper_object_get_graph(_obj)); }

    Device::Device(const string_type &name, const Graph& graph) : Object(NULL)
    {
        _obj = mapper_device_new(name, graph);
    }

    signal_type::signal_type(const Signal& sig)
        { _sig = (mapper_signal)sig; }

    Map& Map::add_scope(Device dev)
        { mapper_map_add_scope(_obj, mapper_device(dev)); return (*this); }

    Map& Map::remove_scope(Device dev)
        { mapper_map_remove_scope(_obj, mapper_device(dev)); return (*this); }

    Signal Map::signal(mapper_location loc, int index) const
        { return Signal(mapper_map_get_signal(_obj, loc, index)); }

    Signal Map::source(int index) const
        { return this->signal(MAPPER_LOC_SRC, index); }

    Signal Map::destination(int index) const
        { return this->signal(MAPPER_LOC_DST, index); }

    std::vector<Signal> Map::signals(mapper_location loc) const
    {
        std::vector<Signal> vec;
        int len = mapper_map_get_num_signals(_obj, loc);
        for (int i = 0; i < len; i++)
            vec.push_back(this->signal(loc, i));
        return vec;
    }

    Device Signal::device() const
        { return Device(mapper_signal_get_device(_obj)); }

    inline std::string version()
        { return std::string(mapper_version()); }
};

std::ostream& operator<<(std::ostream& os, const mapper::Property& p)
{
    if (p.len <= 0 || p.type == MAPPER_NULL)
        return os << "NULL";
    switch (p.type) {
        case MAPPER_INT32:
            if (p.len == 1)
                os << *(int*)p.val;
            else if (p.len > 1) {
                os << "[";
                int *vals = (int*)p.val;
                for (int i = 0; i < p.len; i++)
                    os << vals[i] << ", ";
                os << "\b\b]";
            }
            break;
        case MAPPER_INT64:
            if (p.len == 1)
                os << *(int64_t*)p.val;
            else if (p.len > 1) {
                os << "[";
                int64_t *vals = (int64_t*)p.val;
                for (int i = 0; i < p.len; i++)
                    os << vals[i] << ", ";
                os << "\b\b]";
            }
            break;
        case MAPPER_FLOAT:
            if (p.len == 1)
                os << *(float*)p.val;
            else if (p.len > 1) {
                os << "[";
                float *vals = (float*)p.val;
                for (int i = 0; i < p.len; i++)
                    os << vals[i] << ", ";
                os << "\b\b]";
            }
            break;
        case MAPPER_DOUBLE:
            if (p.len == 1)
                os << *(double*)p.val;
            else if (p.len > 1) {
                os << "[";
                double *vals = (double*)p.val;
                for (int i = 0; i < p.len; i++)
                    os << vals[i] << ", ";
                os << "\b\b]";
            }
            break;
        case MAPPER_BOOL:
            if (p.len == 1)
                os << *(bool*)p.val;
            else if (p.len > 1) {
                os << "[";
                bool *vals = (bool*)p.val;
                for (int i = 0; i < p.len; i++)
                    os << vals[i] << ", ";
                os << "\b\b]";
            }
            break;
        case MAPPER_STRING:
            if (p.len == 1)
                os << (const char*)p.val;
            else if (p.len > 1) {
                os << "[";
                const char **vals = (const char**)p.val;
                for (int i = 0; i < p.len; i++)
                    os << vals[i] << ", ";
                os << "\b\b]";
            }
            break;
        default:
            os << "Property type not handled by ostream operator!";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const mapper::Device dev)
{
    return os << "<mapper::Device '" << dev[MAPPER_PROP_NAME] << "'>";
}

std::ostream& operator<<(std::ostream& os, const mapper::Signal sig)
{
    os << "<mapper::Signal '" << sig.device()[MAPPER_PROP_NAME]
       << ":" << sig[MAPPER_PROP_NAME] << "'>";
    return os;
}

std::ostream& operator<<(std::ostream& os, const mapper::Map map)
{
    os << "<mapper::Map ";

    // add sources
    int num_srcs = map.num_signals(MAPPER_LOC_SRC);
    if (num_srcs > 1)
        os << "[";
    for (int i = 0; i < num_srcs; i++) {
        os << map.signal(MAPPER_LOC_SRC, i)  << ", ";
    }
    os << "\b\b";
    if (num_srcs > 1)
        os << "]";

    os << " -> ";

    // add destination
    os << map.signal(MAPPER_LOC_DST);

    os << ">";
    return os;
}

std::ostream& operator<<(std::ostream& os, const mapper::Object& o)
{
    mapper_object obj = (mapper_object)o;
    switch (mapper_object_get_type(obj)){
        case MAPPER_OBJ_DEVICE:
            os << mapper::Device(obj);
            break;
        case MAPPER_OBJ_SIGNAL: {
            os << mapper::Signal(obj);
            break;
        }
        case MAPPER_OBJ_MAP: {
            os << mapper::Map(obj);
            break;
        }
        default:
            break;
    }
    return os;
}

#endif // _MAPPER_CPP_H_
