#ifndef _MPR_CPP_H_
#define _MPR_CPP_H_

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

#ifdef interface
#undef interface
#endif

#define RETURN_SELF   \
{ return (*this); }

#define MPR_TYPE(NAME) mpr_ ## NAME
#define MPR_FUNC(OBJ, FUNC) mpr_ ## OBJ ## _ ## FUNC

#define OBJ_METHODS(CLASS_NAME)                                             \
public:                                                                     \
    template <typename... Values>                                           \
    CLASS_NAME& set_property(const Values... vals)                          \
        { Object::set_property(vals...); RETURN_SELF }                      \
    template <typename T>                                                   \
    CLASS_NAME& remove_property(const T prop)                               \
        { Object::remove_property(prop); RETURN_SELF }                      \
    const CLASS_NAME& push() const                                          \
        { Object::push(); RETURN_SELF }                                     \

namespace mapper {

    class Object;
    class Device;
    class Signal;
    class Map;
    class PropVal;
    class Graph;

    /*! The set of possible datatypes. */
    enum class Type : char
    {
        DEVICE      = MPR_DEV,      /*!< Devices only. */
        SIGNAL_IN   = MPR_SIG_IN,   /*!< Input signals. */
        SIGNAL_OUT  = MPR_SIG_OUT,  /*!< Output signals. */
        SIGNAL      = MPR_SIG,      /*!< All signals. */
        MAP_IN      = MPR_MAP_IN,   /*!< Incoming maps. */
        MAP_OUT     = MPR_MAP_OUT,  /*!< Outgoing maps. */
        MAP         = MPR_MAP,      /*!< All maps. */
        OBJECT      = MPR_OBJ,      /*!< All objects: devs, sigs, maps. */
        LIST        = MPR_LIST,     /*!< object query. */
        BOOLEAN     = MPR_BOOL,     /*!< Boolean value. */
        TYPE        = MPR_TYPE,     /*!< libmapper data type. */
        DOUBLE      = MPR_DBL,      /*!< 64-bit floating point. */
        FLOAT       = MPR_FLT,      /*!< 32-bit floating point. */
        INT64       = MPR_INT64,    /*!< 64-bit integer. */
        INT32       = MPR_INT32,    /*!< 32-bit integer. */
        STRING      = MPR_STR,      /*!< String. */
        TIME        = MPR_TIME,     /*!< 64-bit NTP timestamp. */
        POINTER     = MPR_PTR       /*!< pointer. */
    };

    /*! The set of possible directions for a signal. */
    enum class Direction : int
    {
        INCOMING    = MPR_DIR_IN,   /*!< Incoming */
        OUTGOING    = MPR_DIR_OUT,  /*!< Outgoing */
        ANY         = MPR_DIR_ANY,  /*!< Either incoming or outgoing */
        BOTH        = MPR_DIR_BOTH  /*!< Both directions apply.  Currently signals cannot be both
                                     * inputs and outputs, so this value is only used for querying
                                     * device maps that touch only local signals. */
    };

    /*! Possible operations for composing queries. */
    enum class Operator
    {
        DOES_NOT_EXIST          = MPR_OP_NEX,   /*!< Property does not exist for this entity. */
        EQUAL                   = MPR_OP_EQ,    /*!< Property value == query value. */
        EXISTS                  = MPR_OP_EX,    /*!< Property exists for this entity. */
        GREATER_THAN            = MPR_OP_GT,    /*!< Property value > query value. */
        GREATER_THAN_OR_EQUAL   = MPR_OP_GTE,   /*!< Property value >= query value */
        LESS_THAN               = MPR_OP_LT,    /*!< Property value < query value */
        LESS_THAN_OR_EQUAL      = MPR_OP_LTE,   /*!< Property value <= query value */
        NOT_EQUAL               = MPR_OP_NEQ,   /*!< Property value != query value */
        ALL                     = MPR_OP_ALL,   /*!< Applies to all elements of value */
        ANY                     = MPR_OP_ANY,   /*!< Applies to any element of value */
        NONE                    = MPR_OP_NONE
    };

    /*! Symbolic identifiers for core object properties. */
    enum class Property
    {
        //BUNDLE              = MPR_PROP_BUNDLE,
        DEVICE              = MPR_PROP_DEV,         /*!< Parent Device for a Signal object. */
        DIRECTION           = MPR_PROP_DIR,         /*!< Direction of a Signal (output or input). */
        EPHEMERAL           = MPR_PROP_EPHEM,       /*!< For Signals: whether Instances are ephemeral. */
        EXPRESSION          = MPR_PROP_EXPR,        /*!< Signal processing expression for a Map. */
        HOST                = MPR_PROP_HOST,        /*!< Network host IP. */
        ID                  = MPR_PROP_ID,          /*!< Unique identifier. */
        IS_LOCAL            = MPR_PROP_IS_LOCAL,    /*!< Whether the object is local or remote. */
        JITTER              = MPR_PROP_JITTER,      /*!< Estimated jitter of value updates. */
        LENGTH              = MPR_PROP_LEN,         /*!< Vector length. */
        LIBVERSION          = MPR_PROP_LIBVER,      /*!< Version of libmapper used by an object. */
        LINKED              = MPR_PROP_LINKED,      /*!< Linked remote devices. */
        MAX                 = MPR_PROP_MAX,         /*!< Maximum value. */
        MIN                 = MPR_PROP_MIN,         /*!< Minimum value. */
        MUTED               = MPR_PROP_MUTED,       /*!< For Maps: whether updates are processed. */
        NAME                = MPR_PROP_NAME,        /*!< Object name. */
        NUM_INSTANCES       = MPR_PROP_NUM_INST,    /*!< Number of associated Instances. */
        NUM_MAPS            = MPR_PROP_NUM_MAPS,    /*!< Number of associated maps. */
        NUM_MAPS_IN         = MPR_PROP_NUM_MAPS_IN, /*!< Number of associated incoming maps. */
        NUM_MAPS_OUT        = MPR_PROP_NUM_MAPS_OUT,/*!< Number of associated outgoing maps. */
        NUM_SIGNALS_IN      = MPR_PROP_NUM_SIGS_IN, /*!< Number of associated incoming signals. */
        NUM_SIGNALS_OUT     = MPR_PROP_NUM_SIGS_OUT,/*!< Number of associated outgoing signals. */
        ORDINAL             = MPR_PROP_ORDINAL,     /*!< Ordinal associated with a Device. */
        PERIOD              = MPR_PROP_PERIOD,      /*!< Estimated period of value updates. */
        PORT                = MPR_PROP_PORT,        /*!< Network port used for peer-to-peer comms. */
        PROCESS_LOCATION    = MPR_PROP_PROCESS_LOC, /*!< For Maps: location where processing occurs. */
        PROTOCOL            = MPR_PROP_PROTOCOL,    /*!< For Maps: network protocol used for comms. */
        //RATE                = MPR_PROP_RATE,
        SCOPE               = MPR_PROP_SCOPE,       /*!< For Maps: scope governing update propagation. */
        SIGNAL              = MPR_PROP_SIG,         /*!< Associated Signal(s). */
        /* MPR_PROP_SLOT DELIBERATELY OMITTED */
        STATUS              = MPR_PROP_STATUS,      /*!< Current status of an object. */
        STEAL_MODE          = MPR_PROP_STEAL_MODE,  /*!< For Signals: mode used for Instance stealing. */
        SYNCED              = MPR_PROP_SYNCED,      /*!< For Remote objects: last ping time. */
        TYPE                = MPR_PROP_TYPE,        /*!< Data type. */
        UNIT                = MPR_PROP_UNIT,        /*!< Unit associated with a value. */
        USE_INSTANCES       = MPR_PROP_USE_INST,    /*!< Whether a Signal or Map uses Instances. */
        VERSION             = MPR_PROP_VERSION,     /*!< Object version. */
    };

    typedef mpr_id Id;

    // Helper class to allow polymorphism on "const char *" and "std::string".
    class str_type {
    public:
        str_type(const char *s=0) { _s = s; }
        str_type(const std::string& s) { _s = s.c_str(); }
        operator const char*() const { return _s; }
        const char *_s;
    };

    /*! libmapper uses NTP timetags for communication and synchronization. */
    class Time
    {
    public:
        /*! Allocate and initialize a Time object.
         *  \param time         The mpr_time value to copy.
         *  \return             Self. */
        Time(mpr_time time)
            { _time.sec = time.sec; _time.frac = time.frac; }

        /*! Allocate and initialize a Time object.
         *  \param sec          Seconds since January 1, 1900 epoch.
         *  \param frac         Fractions of a second.
         *  \return             Self. */
        Time(unsigned long int sec, unsigned long int frac)
            { _time.sec = sec; _time.frac = frac; }

        /*! Allocate and initialize a Time object.
         *  \param seconds      Time to initialize.
         *  \return             Self. */
        Time(double seconds)
            { mpr_time_set_dbl(&_time, seconds); }

        /*! Allocate and initialize a Time object to the current time.
         *  \return             Self. */
        Time()
            { mpr_time_set(&_time, MPR_NOW); }

        /*! Retrieve the seconds part of the Time.
         *  \return             Self. */
        uint32_t sec()
            { return _time.sec; }

        /*! Set the seconds part of the Time.
         *  \param sec          The value to set.
         *  \return             Self. */
        Time& set_sec(uint32_t sec)
            { _time.sec = sec; RETURN_SELF }

        /*! Retrieve the fraction part of the Time.
         *  \return             Self. */
        uint32_t frac()
            { return _time.frac; }

        /*! Set the fraction part of the Time.
         *  \param frac         The value to set.
         *  \return             Self. */
        Time& set_frac (uint32_t frac)
            { _time.frac = frac; RETURN_SELF }

        /*! Update to the current time.
         *  \return             Self. */
        Time& now()
            { mpr_time_set(&_time, MPR_NOW); RETURN_SELF }

        /*! Cast to mpr_time.
         *  \return             Self. */
        operator mpr_time*()
            { return &_time; }

        /*! Retrieve the value translated to a double.
         *  \param frac         The value to set.
         *  \return             Self. */
        operator double() const
            { return mpr_time_as_dbl(_time); }

        /*! Copy the value from another Time object.
         *  \param time         The object to copy.
         *  \return             Self. */
        Time& operator=(Time& time)
            { mpr_time_set(&_time, time._time); RETURN_SELF }

        /*! Set the value.
         *  \param d            The value to set.
         *  \return             Self. */
        Time& operator=(double d)
            { mpr_time_set_dbl(&_time, d); RETURN_SELF }

        /*! Addition operator for Time objects.
         *  \param addend       The addend.
         *  \return             Self. */
        Time operator+(Time& addend)
        {
            mpr_time temp;
            mpr_time_set(&temp, _time);
            mpr_time_add(&temp, *(mpr_time*)addend);
            return temp;
        }

        /*! Subtraction operator for Time objects.
         *  \param subtrahend   The subtrahend.
         *  \return             Self. */
        Time operator-(Time& subtrahend)
        {
            mpr_time temp;
            mpr_time_set(&temp, _time);
            mpr_time_sub(&temp, *(mpr_time*)subtrahend);
            return temp;
        }

        /*! Compound assignment addition operator for Time objects.
         *  \param addend       The addend.
         *  \return             Self. */
        Time& operator+=(Time& addend)
            { mpr_time_add(&_time, *(mpr_time*)addend); RETURN_SELF }

        /*! Compound assignment addition operator for a Time object and a double.
         *  \param addend       The addend.
         *  \return             Self. */
        Time& operator+=(double addend)
            { mpr_time_add_dbl(&_time, addend); RETURN_SELF }

        /*! Compound assignment subtraction operator for Time objects.
         *  \param subtrahend   The subtrahend.
         *  \return             Self. */
        Time& operator-=(Time& subtrahend)
            { mpr_time_sub(&_time, *(mpr_time*)subtrahend); RETURN_SELF }

        /*! Compound assignment subtraction operator for a Time object and a double.
         *  \param subtrahend   The subtrahend.
         *  \return             Self. */
        Time& operator-=(double subtrahend)
            { mpr_time_add_dbl(&_time, -subtrahend); RETURN_SELF }

        /*! Compound assignment multiplication operator for a Time object and a double.
         *  \param multiplicand The multiplicand.
         *  \return             Self. */
        Time& operator*=(double multiplicand)
            { mpr_time_mul(&_time, multiplicand); RETURN_SELF }

        /*! Comparison less-than operator for Time objects.
         *  \param rhs          The Time object to compare.
         *  \return             Self. */
        bool operator<(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) < 0; }

        /*! Comparison less-than-of-equal operator for Time objects.
         *  \param rhs          The Time object to compare.
         *  \return             Self. */
        bool operator<=(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) <= 0; }

        /*! Comparison equality operator for Time objects.
         *  \param rhs          The Time object to compare.
         *  \return             Self. */
        bool operator==(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) == 0; }

        /*! Comparison greater-than operator for Time objects.
         *  \param rhs          The Time object to compare.
         *  \return             Self. */
        bool operator>=(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) >= 0; }

        /*! Comparison greater-than-or-equal operator for Time objects.
         *  \param rhs          The Time object to compare.
         *  \return             Self. */
        bool operator>(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) > 0; }

        /*! Comparison inequality operator for Time objects.
         *  \param rhs          The Time object to compare.
         *  \return             Self. */
        bool operator!=(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) != 0; }

        friend std::ostream& operator<<(std::ostream& os, const mapper::Time& time);
    private:
        mpr_time _time;
    };

    /*! List objects provide a lazily-computed iterable list of results
     *  from running queries against a mapper::Graph. */
    template <class T>
    class List
    {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = int;
        using difference_type = int;
        using pointer = int*;
        using reference = int&;

        List(mpr_list list)
            { _list = list; }
        /* Copy constructor */
        List(const List& orig)
            { _list = mpr_list_get_cpy(orig._list); }
        /* Move constructor */
        List(List&& orig) noexcept
            { _list = orig._list; orig._list = NULL; }
        /* Copy assignment operator */
        List& operator=(const List& orig) noexcept
            { _list = mpr_list_get_cpy(orig._list); return *this; }
        /* Move assignment operator */
        List& operator=(List&& orig) noexcept
            { _list = orig._list; orig._list = NULL; return *this; }

        ~List()
            { mpr_list_free(_list); }

        operator mpr_list() { return _list; }

        bool operator==(const List& rhs)
            { return (_list == rhs._list); }
        bool operator!=(const List& rhs)
            { return (_list != rhs._list); }
        List& operator++()
            { if (_list) _list = mpr_list_get_next(_list); RETURN_SELF }
        List operator++(int)
            { List tmp(*this); operator++(); return tmp; }
        List begin()
            { RETURN_SELF; }
        List end()
            { return List(0); }

        /*! Return the number ot items in a List
         *  \return             The number of items in this list. */
        int size()
            { return mpr_list_get_size(_list); }

        /* Combination functions */
        /*! Add items found in List rhs to this List (without duplication).
         *  \param rhs          A second List.
         *  \return             Self. */
        List& join(const List& rhs)
            { _list = mpr_list_get_union(_list, mpr_list_get_cpy(rhs._list)); RETURN_SELF; }

        /*! Remove items NOT found in List rhs from this List
         *  \param rhs          A second List.
         *  \return             Self. */
        List& intersect(const List& rhs)
            { _list = mpr_list_get_isect(_list, mpr_list_get_cpy(rhs._list)); RETURN_SELF; }

        /*! Filter items from this List based on property matching
         *  \param prop         The name or id of the property to match.
         *  \param value        The property value.
         *  \param op           The comparison operator.
         *  \return             Self. */
        template <typename P, typename V>
        List& filter(P&& prop, V&& value, Operator op);

        /*! Remove items found in List rhs from this List
         *  \param rhs          A second list.
         *  \return             Self. */
        List& subtract(const List& rhs)
            { _list = mpr_list_get_diff(_list, mpr_list_get_cpy(rhs._list)); RETURN_SELF; }

        /*! Add items found in List rhs to this List (without duplication).
         *  \param rhs          A second List.
         *  \return             A new List containing the results. */
        List operator+(const List& rhs) const
        {
            return List(mpr_list_get_union(mpr_list_get_cpy(_list), mpr_list_get_cpy(rhs._list)));
        }

        /*! Remove items NOT found in List rhs from this List
         *  \param rhs          A second List.
         *  \return             A new List containing the results. */
        List operator*(const List& rhs) const
        {
            return List(mpr_list_get_isect(mpr_list_get_cpy(_list), mpr_list_get_cpy(rhs._list)));
        }

        /*! Remove items found in List rhs from this List
         *  \param rhs          A second List.
         *  \return             A new List containing the results. */
        List operator-(const List& rhs) const
        {
            return List(mpr_list_get_diff(mpr_list_get_cpy(_list), mpr_list_get_cpy(rhs._list)));
        }

        /*! Add items found in List rhs to this List (without duplication).
         *  \param rhs          A second List.
         *  \return             Self. */
        List& operator+=(const List& rhs)
            { _list = mpr_list_get_union(_list, mpr_list_get_cpy(rhs._list)); RETURN_SELF; }

        /*! Remove items NOT found in List rhs from this List
         *  \param rhs          A second List.
         *  \return             Self. */
        List& operator*=(const List& rhs)
            { _list = mpr_list_get_isect(_list, mpr_list_get_cpy(rhs._list)); RETURN_SELF; }

        /*! Remove items found in List rhs from this List
         *  \param rhs          A second List.
         *  \return             Self. */
        List& operator-=(const List& rhs)
            { _list = mpr_list_get_diff(_list, mpr_list_get_cpy(rhs._list)); RETURN_SELF; }

        /*! Set properties for each Object in the List.
         *  \param vals     The Properties to add of modify.
         *  \return         Self. */
        template <typename... Values>
        List& set_property(const Values... vals);

        T operator*()
            { return _list ? T(*_list) : T(NULL); }
        operator T()
            { return _list ? T(*_list) : T(NULL); }

        /*! Retrieve an indexed item in the List.
         *  \param idx           The index of the element to retrieve.
         *  \return              The retrieved Object. */
        T operator [] (int idx)
            { return T(mpr_list_get_idx(_list, idx)); }

        /*! Convert this List to a std::vector of CLASS_NAME.
         *  \return              The converted List results. */
        virtual operator std::vector<T>() const
        {
            std::vector<T> vec;
            mpr_list cpy = mpr_list_get_cpy(_list);
            while (cpy) {
                vec.push_back(T(*cpy));
                cpy = mpr_list_get_next(cpy);
            }
            return vec;
        }

    protected:
        mpr_list _list;
    };

    /*! Objects provide a generic representation of Devices, Signals, and Maps. */
    class Object
    {
    protected:
        int* _refcount_ptr;
        int incr_refcount()
            { return _refcount_ptr ? ++(*_refcount_ptr) : 0; }
        int decr_refcount()
            { return _refcount_ptr ? --(*_refcount_ptr) : 0; }
        bool _owned;

        Object(mpr_obj obj) { _obj = obj; _owned = 0; _refcount_ptr = 0; }

        friend class List<Device>;
        friend class List<Signal>;
        friend class List<Map>;
        friend class PropVal;

        mpr_obj _obj;
    public:
        /*! The set of possible statuses for an Object. */
        enum class Status
        {
            UNDEFINED   = MPR_STATUS_UNDEFINED, /*!< Object status is undefined. */
            EXPIRED     = MPR_STATUS_EXPIRED,   /*!< Object record has expired. */
            STAGED      = MPR_STATUS_STAGED,    /*!< Object has been staged. */
            READY       = MPR_STATUS_READY,     /*!< Object is ready. */
        };

        Object() { _obj = NULL; _owned = 0; _refcount_ptr = 0; }
        virtual ~Object() {}
        operator mpr_obj() const
            { return _obj; }
        bool operator == (Object o) const
            { return _obj == o._obj; }

        /*! Cast to a boolean value based on whether the underlying C object exists.
         *  \return         True if object exists, otherwise false. */
        operator bool() const
            { return _obj ? true : false; }

        /*! Get the specific type of an Object.
         *  \return         Object type. */
        Type type() const
            { return Type(mpr_obj_get_type(_obj)); }

        /*! Get the underlying Graph.
         *  \return         Graph. */
        inline Graph graph() const;

        /*! Set arbitrary properties for an Object.
         *  \param vals     The Properties to add or modify.
         *  \return         Self. */
        template <typename... Values>
        Object& set_property(const Values... vals);

        /*! Remove a Property from an Object by symbolic identifier.
         *  \param prop    The Property to remove.
         *  \return        Self. */
        virtual Object& remove_property(Property prop)
            { mpr_obj_remove_prop(_obj, static_cast<mpr_prop>(prop), NULL); RETURN_SELF }

        /*! Remove a named Property from an Object.
         *  \param key     Name of the Property to remove.
         *  \return        Self. */
        virtual Object& remove_property(const str_type &key)
        {
            if (key)
                mpr_obj_remove_prop(_obj, MPR_PROP_UNKNOWN, key);
            RETURN_SELF;
        }

        /*! Push "staged" property changes out to the distributed graph.
         *  \return         Self. */
        virtual const Object& push() const
            { mpr_obj_push(_obj); RETURN_SELF }

        const Object& print(int staged=0) const
            { mpr_obj_print(_obj, staged); RETURN_SELF }

        /*! Retrieve the number of Properties owned by an Object.
         *  \param staged   Set to true to count properties that have been
         *                  staged but not synchronized with the graph.
         *  \return         The number of Properties. */
        int num_props(bool staged = false) const
            { return mpr_obj_get_num_props(_obj, staged); }

        /*! Retrieve a Property by name.
         *  \param key      The name of the Property to retrieve.
         *  \return         The retrieved PropVal. */
        PropVal property(const str_type &key=NULL) const;

        /*! Retrieve a Property by Property enum value.
         *  \param prop     The symbolic identifier of the Property to retrieve.
         *  \return         The retrieved Property. */
        PropVal property(Property prop) const;

        /*! Retrieve a Property by index.
         *  \param idx      The index of the Property to retrieve.
         *  \return         The retrieved Property. */
        PropVal property(int idx) const;

        template <typename T>
        PropVal operator [] (T prop) const;

        friend std::ostream& operator<<(std::ostream& os, const mapper::Object& o);
    };

    class signal_type {
    public:
        signal_type(mpr_sig sig)
            { _sig = sig; }
        inline signal_type(const Signal& sig); // defined later
        operator mpr_sig() const
            { return _sig; }
        mpr_sig _sig;
    };

    /*! Maps define dataflow connections between sets of Signals. A Map consists of one or more
     *  source Signals, one or more destination Signal (currently), restricted to one) and
     *  properties which determine how the source data is processed. */
    class Map : public Object
    {
    public:
        /*! Describes the possible endpoints of a map. */
        enum class Location
        {
            SRC = MPR_LOC_SRC,              /*!< Source signal(s) for this map. */
            DST = MPR_LOC_DST,              /*!< Destination signal(s) for this map. */
            ANY = MPR_LOC_ANY               /*!< Either source or destination signals. */
        };

        /*! Describes the possible network protocols for map communication. */
        enum class Protocol
        {
            UDP         = MPR_PROTO_UDP,    /*!< Map updates are sent using UDP. */
            TCP         = MPR_PROTO_TCP     /*!< Map updates are sent using TCP. */
        };
    private:
        /* This constructor accepts a between 2 and 10 signal object arguments inclusive. It is
         * delagated to by the variadic template constructor and in turn it calls the vararg
         * function mpr_map_new_from_str() from the libmapper C API. At least two valid signal
         * arguments are required to create map, but the remaining 8 are set to default to NULL to
         * support calling the constructor with fewer than 10 signal arguments. When calling into
         * the C API we add an extra NULL argument to ensure that it will fail if the format string
         * contains more than 10 format specifiers. */
        Map(int dummy, const str_type &expression, signal_type sig0, signal_type sig1,
            signal_type sig2 = NULL, signal_type sig3 = NULL, signal_type sig4 = NULL,
            signal_type sig5 = NULL, signal_type sig6 = NULL, signal_type sig7 = NULL,
            signal_type sig8 = NULL, signal_type sig9 = NULL) : Object(NULL)
        {
            _obj = mpr_map_new_from_str(expression, (mpr_sig)sig0, (mpr_sig)sig1,
                                        sig2 ? (mpr_sig)sig2 : NULL, sig3 ? (mpr_sig)sig3 : NULL,
                                        sig4 ? (mpr_sig)sig4 : NULL, sig5 ? (mpr_sig)sig5 : NULL,
                                        sig6 ? (mpr_sig)sig6 : NULL, sig7 ? (mpr_sig)sig7 : NULL,
                                        sig8 ? (mpr_sig)sig8 : NULL, sig9 ? (mpr_sig)sig9 : NULL,
                                        NULL);
        }
    public:
        Map(const Map& orig) : Object(orig._obj) {}
        Map(mpr_map map) : Object(map) {}

        /*! Create a map between a pair of Signals.
         *  \param src  Source Signal.
         *  \param dst  Destination Signal object.
         *  \return     A new Map object – either loaded from the Graph (if the Map already exists)
         *              or newly created. In the latter case the Map will not take effect until it
         *              has been added to the distributed graph using push(). */
        Map(signal_type src, signal_type dst) : Object(NULL)
        {
            mpr_sig cast_src = src, cast_dst = dst;
            _obj = mpr_map_new(1, &cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of signals using an expression string containing embedded
         *  format specifiers that are replaced by mpr_sig values specified in subsequent additional
         *  arguments.
         *  \param expression   A string specifying the map expression to use when mapping source to
         *                      destination signals. The format specifier "%x" is used to specify
         *                      source signals and the "%y" is used to specify the destination
         *                      signal.
         *  \param args         A sequence of additional Signal arguments, one for each format
         *                      specifier in the format string
         *  \return             A map data structure – either loaded from the graph and modified
         *                      with the new expression (if the map already existed) or newly
         *                      created. Changes to the map will not take effect until it has been
         *                      added to the distributed graph using mpr_obj_push(). */
        template<typename... Args>
        Map(const str_type &expression, Args ...args) : Map(1, expression, args...) {}

        /*! Create a map between a set of Signals.
         *  \param num_srcs The number of source signals in this map.
         *  \param srcs     Array of source Signal objects.
         *  \param num_dsts The number of destination signals in this map, currently restricted to 1.
         *  \param dsts     Array of destination Signal objects.
         *  \return         A new Map object – either loaded from the Graph (if the Map already
         *                  exists) or newly created. In the latter case the Map will not take
         *                  effect until it has been added to the graph using push(). */
        Map(int num_srcs, signal_type srcs[], int num_dsts, signal_type dsts[]) : Object(NULL)
        {
            mpr_sig *cast_src = (mpr_sig*)malloc(sizeof(mpr_sig) * num_srcs), cast_dst = dsts[0];
            for (int i = 0; i < num_srcs; i++)
                cast_src[i] = srcs[i];
            _obj = mpr_map_new(num_srcs, cast_src, 1, &cast_dst);
            free(cast_src);
        }

        /*! Create a map between a set of Signals.
         *  \param srcs std::array of source Signal objects.
         *  \param dsts std::array of destination Signal objects.
         *  \return     A new Map object – either loaded from the Graph (if the Map already exists)
         *              or newly created. In the latter case the Map will not take effect until it
         *              has been added to the distributed graph using push(). */
        template <size_t N, size_t M>
        Map(std::array<signal_type, N>& srcs,
            std::array<signal_type, M>& dsts) : Object(NULL)
        {
            if (srcs.empty() || dsts.empty() || M != 1) {
                _obj = 0;
                return;
            }
            mpr_sig *cast_src = (mpr_sig*)malloc(sizeof(mpr_sig) * N), cast_dst = dsts.data()[0];
            for (int i = 0; i < N; i++)
                cast_src[i] = srcs.data()[i];
            _obj = mpr_map_new(N, cast_src, 1, &cast_dst);
            free(cast_src);
        }

        /*! Create a map between a set of Signals.
         *  \param srcs std::vector of source Signal objects.
         *  \param dsts std::vector of destination Signal objects.
         *  \return     A new Map object – either loaded from the Graph (if the Map already exists)
         *              or newly created. In the latter case the Map will not take effect until it
         *              has been added to the distributed graph using push(). */
        Map(std::vector<signal_type>& srcs, std::vector<signal_type>& dsts) : Object(NULL)
        {
            if (!srcs.size() || (dsts.size() != 1)) {
                _obj = 0;
                return;
            }
            int num_srcs = srcs.size();
            mpr_sig *cast_src = (mpr_sig*)malloc(sizeof(mpr_sig) * num_srcs);
            mpr_sig cast_dst = dsts.data()[0];
            for (int i = 0; i < num_srcs; i++)
                cast_src[i] = srcs.data()[i];
            _obj = mpr_map_new(num_srcs, cast_src, 1, &cast_dst);
            free(cast_src);
        }

        /*! Return C data structure mpr_map corresponding to this object.
         *  \return         C mpr_map data structure. */
        operator mpr_map() const
            { return _obj; }

        /*! Re-create stale map if necessary.
         *  \return         Self. */
        const Map& refresh() const
            { mpr_map_refresh(_obj); RETURN_SELF }

        /*! Release the Map between a set of Signals. */
        // this function can be const since it only sends the unmap msg
        void release() const
            { mpr_map_release(_obj); }

        /*! Detect whether a Map is completely initialized.
         *  \return         True if map is completely initialized. */
        bool ready() const
            { return mpr_map_get_is_ready(_obj); }

        /*! Add a scope to this Map. Map scopes configure the propagation of Signal updates across
         *  the Map. Changes will not take effect until synchronized with the distributed graph
         *  using push().
         *  \param dev      Device to add as a scope for this Map. After taking effect, this
         *                  setting will cause instance updates originating from the specified
         *                  Device to be propagated across the Map.
         *  \return         Self. */
        inline Map& add_scope(const Device& dev);

        /*! Remove a scope from this Map. Map scopes configure the propagation of Signal updates
         *  across the Map. Changes will not take effect until synchronized with the distributed
         *  graph using push().
         *  \param dev      Device to remove as a scope for this Map. After taking effect, this
         *                  setting will cause instance updates originating from the specified
         *                  Device to be blocked from propagating across the Map.
         *  \return         Self. */
        inline Map& remove_scope(const Device& dev);

        /*! Get the index of the Map endpoint matching a specific Signal.
         *  \param sig      The Signal to look for.
         *  \return         Index of the signal in this map, or -1 if not found. */
        int signal_idx(signal_type sig) const
            { return mpr_map_get_sig_idx(_obj, (mpr_sig)sig); }

        /*! Retrieve a list of connected Signals for this Map.
         *  \param loc      MPR_LOC_SRC for source signals for this Map,
         *                  MPR_LOC_DST for destinations, or MPR_LOC_ANY for both.
         *  \return         A List of Signals. */
        List<Signal> signals(Location loc = Location::ANY) const
            { return List<Signal>(mpr_map_get_sigs(_obj, static_cast<mpr_loc>(loc))); }

        OBJ_METHODS(Map);

        friend std::ostream& operator<<(std::ostream& os, const mapper::Map& map);

    protected:
        friend class Graph;
    };

    /*! Signals define inputs or outputs for Devices.  A Signal consists of a scalar or vector
     *  value of some integer or floating-point type.  A Signal is created by adding an input or
     *  output to a Device.  It can optionally be provided with some metadata such as a range,
     *  unit, or other properties.  Signals can be mapped by creating Maps using remote requests
     *  on the network, usually generated by a standalone GUI. */
    class Signal : public Object
    {
    protected:
        friend class Device;
        Signal(mpr_dev dev, mpr_dir dir, const str_type &name, int len, mpr_type type,
               const str_type &unit=0, void *min=0, void *max=0, int *num_inst=0)
        {
            _obj = mpr_sig_new(dev, dir, name, len, type, unit, min, max, num_inst, NULL, 0);
        }

    public:
        /*! The set of possible signal events, used to register and inform callbacks. */
        enum class Event
        {
            NONE        = 0,
            INST_NEW    = MPR_SIG_INST_NEW,     /*!< New instance has been created. */
            REL_UPSTRM  = MPR_SIG_REL_UPSTRM,   /*!< Instance was released upstream. */
            REL_DNSTRM  = MPR_SIG_REL_DNSTRM,   /*!< Instance was released downstream. */
            INST_OFLW   = MPR_SIG_INST_OFLW,    /*!< No local instances left. */
            UPDATE      = MPR_SIG_UPDATE,       /*!< Instance value has been updated. */
            ALL         = MPR_SIG_ALL
        };

        /*! The set of possible voice-stealing modes for instances. */
        enum class Stealing
        {
            NONE    = MPR_STEAL_NONE,       /*!< No stealing will take place. */
            OLDEST  = MPR_STEAL_OLDEST,     /*!< Steal the oldest instance. */
            NEWEST  = MPR_STEAL_NEWEST      /*!< Steal the newest instance. */
        };

        Signal() : Object() {}
        Signal(mpr_sig sig) : Object(sig) {}
        operator mpr_sig() const
            { return _obj; }

        /*! Get the parent Device for this Signal.
         *  \return         The parent Device. */
        inline Device device() const;

        /*! Get a list of Maps involving this Signal.
         *  \param dir      The directionality of the Maps (with reference to the Signal) to
         *                  include in the list. Use Direction::ANY to include all Maps.
         *  \return         A List of Maps. */
        List<Map> maps(Direction dir = Direction::ANY) const
            { return List<Map>(mpr_sig_get_maps(_obj, static_cast<mpr_dir>(dir))); }

    private:
        /* Value update functions */
        Signal& _set_value(const int *val, int len)
            { mpr_sig_set_value(_obj, 0, len, MPR_INT32, val); RETURN_SELF }
        Signal& _set_value(const float *val, int len)
            { mpr_sig_set_value(_obj, 0, len, MPR_FLT, val); RETURN_SELF }
        Signal& _set_value(const double *val, int len)
            { mpr_sig_set_value(_obj, 0, len, MPR_DBL, val); RETURN_SELF }

        template <typename T>
        Signal& _set_value(T val)
            { return set_value(&val, 1); }
        template <typename T>
        Signal& _set_value(const T* val)
            { return set_value(val, 1); }
        template <typename T, int len>
        Signal& _set_value(const T* val)
            { return set_value(val, len); }
        template <typename T, size_t N>
        Signal& _set_value(std::array<T,N> val)
            { return set_value(&val[0], N); }
        template <typename T>
        Signal& _set_value(std::vector<T> val)
            { return set_value(&val[0], (int)val.size()); }
    public:
        /*! Set the current value for this Signal.
         *  \param vals     The value to set. Can be scalar, array, std::array, or std::vector of
         *                  int, float, or double
         *  \return         Self. */
        template <typename... Values>
        Signal& set_value(Values... vals)
            { return _set_value(vals...); }

        const void *value() const
            { return mpr_sig_get_value(_obj, 0, 0); }
        const void *value(Time time) const
            { return mpr_sig_get_value(_obj, 0, (mpr_time*)time); }

        /*! Signal Instances can be used to describe the multiplicity and/or ephemerality of
         *  phenomena associated with Signals. A signal describes the phenomena, e.g. the position
         *  of a 'blob' in computer vision, and the signal's instances will describe the positions
         *  of actual detected blobs. */
        class Instance {
        public:
            /*! The set of possible statuses for a Signal Instance. */
            enum class Status
            {
                ACTIVE      = MPR_STATUS_ACTIVE,    /*!< Instance is active. */
                RESERVED    = MPR_STATUS_RESERVED   /*!< Instance is reserved by inactive. */
            };

            Instance(mpr_sig sig, Id id)
                { _sig = sig; _id = id; }
            bool operator == (Instance i) const
                { return (_id == i._id); }

            /*! get the Id for this Instance.
             *  \return         The Id. */
            operator Id() const
                { return _id; }

            /*! Return whether this Instance is currently active.
             *  \return         True or False. */
            bool is_active() const
                { return mpr_sig_get_inst_is_active(_sig, _id); }

        private:
            Instance& _set_value(const int *val, int len)
                { mpr_sig_set_value(_sig, _id, len, MPR_INT32, val); RETURN_SELF; }
            Instance& _set_value(const float *val, int len)
                { mpr_sig_set_value(_sig, _id, len, MPR_FLT, val); RETURN_SELF; }
            Instance& _set_value(const double *val, int len)
                { mpr_sig_set_value(_sig, _id, len, MPR_DBL, val); RETURN_SELF; }

            template <typename T>
            Instance& _set_value(T val)
                { return set_value(&val, 1); }
            template <typename T>
            Instance& _set_value(const T* val)
                { return set_value(val, 1); }
            template <typename T, int len>
            Instance& _set_value(const T* val)
                { return set_value(val, len); }
            template <typename T, size_t N>
            Instance& _set_value(std::array<T,N> val)
                { return set_value(&val[0], N); }
            template <typename T>
            Instance& _set_value(std::vector<T> val)
                { return set_value(&val[0], val.size()); }
        public:
            /*! Set the current value for this Instance.
             *  \param vals     The value to set. Can be scalar, array, std::array, or std::vector
             *                  of int, float, or double
             *  \return         Self. */
            template <typename... Values>
            Instance& set_value(Values... vals)
                { return _set_value(vals...); }

            /*! Release this Instance.
             *  \return         Self. */
            Instance& release()
                { mpr_sig_release_inst(_sig, _id); RETURN_SELF }

            /*! Get the Id associated with this Instance.
             *  \return         The Instance Id. */
            Id id() const
                { return _id; }

            /*! Set the user_data pointer associated with this Instance.
             *  \param data     A user_data pointer to store.
             *  \return         Self. */
            Instance& set_data(void *data)
                { mpr_sig_set_inst_data(_sig, _id, data); RETURN_SELF }

            /*! Get the user_data pointer associated with this Instance.
             *  \return         The user_data pointer. */
            void *data() const
                { return mpr_sig_get_inst_data(_sig, _id); }

            /*! Get the current value of this Instance.
             *  \return         A pointer to the current value. */
            const void *value() const
                { return mpr_sig_get_value(_sig, _id, 0); }

            /*! Get the current value of this Instance.
             *  \param time     A Time object to retrieve the time assicated with the current value.
             *  \return         A pointer to the current value. */
            const void *value(Time time) const
                { mpr_time *_time = time; return mpr_sig_get_value(_sig, _id, _time); }

            /*! Retrieve the parent Signal.
             *  \return         The Signal parent of this Instance. */
            Signal signal() const
                { return Signal(_sig); }
        protected:
            friend class Signal;
        private:
            mpr_id _id;
            mpr_sig _sig;
        };
        friend std::ostream& operator<<(std::ostream& os, const mapper::Signal& sig);
    private:
        enum handler_type {
            NONE = -1,
            STANDARD,
            SIMPLE,
            INST,
            SIG_INT,
            SIG_FLT,
            SIG_DBL,
            INST_INT,
            INST_FLT,
            INST_DBL,
            INST_EVT
        };
        typedef struct _handler_data {
            union {
                void (*standard)(Signal&&, Signal::Event, Id, int, Type, const void*, Time&&);
                void (*simple)(Signal&&, int, Type, const void*, Time&&);
                void (*inst)(Signal::Instance&&, Signal::Event, int, Type, const void*, Time&&);
                void (*sig_int)(Signal&&, int, Time&&);
                void (*sig_flt)(Signal&&, float, Time&&);
                void (*sig_dbl)(Signal&&, double, Time&&);
                void (*inst_int)(Signal::Instance&&, Signal::Event, int, Time&&);
                void (*inst_flt)(Signal::Instance&&, Signal::Event, float, Time&&);
                void (*inst_dbl)(Signal::Instance&&, Signal::Event, double, Time&&);
                void (*inst_evt)(Signal::Instance&&, Signal::Event, Time&&);
            } handler;
            enum handler_type type;
        } *handler_data;
        static void _generic_handler(mpr_sig sig, mpr_sig_evt evt, mpr_id inst, int len,
                                     mpr_type type, const void *val, mpr_time time)
        {
            // recover signal user_data
            handler_data data = (handler_data)mpr_obj_get_prop_as_ptr(sig, MPR_PROP_DATA, NULL);
            if (!data)
                return;
            switch (data->type) {
                case STANDARD: {
                    data->handler.standard(Signal(sig), Signal::Event(evt), inst, len, Type(type),
                                           val, Time(time));
                    break;
                }
                case SIMPLE: {
                    data->handler.simple(Signal(sig), len, Type(type), val, Time(time));
                    break;
                }
                case INST:
                    data->handler.inst(Signal::Instance(sig, inst), Signal::Event(evt), len,
                                       Type(type), val, Time(time));
                    break;
                case SIG_INT:
                    if (val)
                        data->handler.sig_int(Signal(sig), *(int*)val, Time(time));
                    break;
                case SIG_FLT:
                    if (val)
                        data->handler.sig_flt(Signal(sig), *(float*)val, Time(time));
                    break;
                case SIG_DBL:
                    if (val)
                        data->handler.sig_dbl(Signal(sig), *(double*)val, Time(time));
                    break;
                case INST_INT:
                    data->handler.inst_int(Signal::Instance(sig, inst), Signal::Event(evt),
                                           val ? *(int*)val : 0, Time(time));
                    break;
                case INST_FLT:
                    data->handler.inst_flt(Signal::Instance(sig, inst), Signal::Event(evt),
                                           val ? *(float*)val : 0, Time(time));
                    break;
                case INST_DBL:
                    data->handler.inst_dbl(Signal::Instance(sig, inst), Signal::Event(evt),
                                           val ? *(double*)val : 0, Time(time));
                case INST_EVT:
                    data->handler.inst_evt(Signal::Instance(sig, inst), Signal::Event(evt),
                                           Time(time));
                    break;
                default:
                    return;
            }
        }
        void _set_callback(handler_data data,
                           void (*h)(Signal&&, Signal::Event, Id, int,
                                     Type, const void*, Time&&))
        {
            data->type = STANDARD;
            data->handler.standard = h;
        }
        void _set_callback(handler_data data, void (*h)(Signal&&, int, Type, const void*, Time&&))
        {
            data->type = SIMPLE;
            data->handler.simple = h;
        }
        void _set_callback(handler_data data,
                           void (*h)(Signal::Instance&&, Signal::Event, int,
                                     Type, const void*, Time&&))
        {
            data->type = INST;
            data->handler.inst = h;
        }
        void _set_callback(handler_data data, void (*h)(Signal&&, int, Time&&))
        {
            if (mpr_obj_get_prop_as_int32(_obj, MPR_PROP_TYPE, NULL) != MPR_INT32
                || mpr_obj_get_prop_as_int32(_obj, MPR_PROP_LEN, NULL) != 1) {
                printf("wrong type 'i' in handler definition\n");
                data->type = NONE;
                return;
            }
            data->type = SIG_INT;
            data->handler.sig_int = h;
        }
        void _set_callback(handler_data data, void (*h)(Signal&&, float, Time&&))
        {
            if (mpr_obj_get_prop_as_int32(_obj, MPR_PROP_TYPE, NULL) != MPR_FLT
                || mpr_obj_get_prop_as_int32(_obj, MPR_PROP_LEN, NULL) != 1) {
                printf("wrong type 'q' in handler definition\n");
                data->type = NONE;
                return;
            }
            data->type = SIG_FLT;
            data->handler.sig_flt = h;
        }
        void _set_callback(handler_data data, void (*h)(Signal&&, double, Time&&))
        {
            if (mpr_obj_get_prop_as_int32(_obj, MPR_PROP_TYPE, NULL) != MPR_DBL
                || mpr_obj_get_prop_as_int32(_obj, MPR_PROP_LEN, NULL) != 1) {
                printf("wrong type 'd' in handler definition\n");
                data->type = NONE;
                return;
            }
            data->type = SIG_DBL;
            data->handler.sig_dbl = h;
        }
        void _set_callback(handler_data data, void (*h)(Signal::Instance&&, Signal::Event, int, Time&&))
        {
            if (mpr_obj_get_prop_as_int32(_obj, MPR_PROP_TYPE, NULL) != MPR_INT32
                || mpr_obj_get_prop_as_int32(_obj, MPR_PROP_LEN, NULL) != 1) {
                printf("wrong type 'i' in handler definition\n");
                data->type = NONE;
                return;
            }
            data->type = INST_INT;
            data->handler.inst_int = h;
        }
        void _set_callback(handler_data data, void (*h)(Signal::Instance&&, Signal::Event, float, Time&&))
        {
            if (mpr_obj_get_prop_as_int32(_obj, MPR_PROP_TYPE, NULL) != MPR_FLT
                || mpr_obj_get_prop_as_int32(_obj, MPR_PROP_LEN, NULL) != 1) {
                printf("wrong type 'q' in handler definition\n");
                data->type = NONE;
                return;
            }
            data->type = INST_FLT;
            data->handler.inst_flt = h;
        }
        void _set_callback(handler_data data, void (*h)(Signal::Instance&&, Signal::Event, double, Time&&))
        {
            if (mpr_obj_get_prop_as_int32(_obj, MPR_PROP_TYPE, NULL) != MPR_DBL
                || mpr_obj_get_prop_as_int32(_obj, MPR_PROP_LEN, NULL) != 1) {
                printf("wrong type 'd' in handler definition\n");
                data->type = NONE;
                return;
            }
            data->type = INST_DBL;
            data->handler.inst_dbl = h;
        }
        void _set_callback(handler_data data, void (*h)(Signal::Instance&&, Signal::Event, Time&&))
        {
            data->type = INST_EVT;
            data->handler.inst_evt = h;
        }
    public:
        /*! Add an Event Callback to a signal.
         *  \param h    Callback function to call on Signal Events.
         *              Function signature can be any of the following:
         *              * void (Signal&& sig, Signal::Event evt, Id id, int len, Type type, const void* val, Time&& time);
         *              * void (Signal&& sig, int len, Type type, const void* val, Time&& time);
         *              * void (Signal::Instance&& inst, Signal::Event evt, int len, Type type, const void* val, Time&& time);
         *              * void (Signal&& sig, int val, Time&& time);
         *              * void (Signal&& sig, float val, Time&& time);
         *              * void (Signal&& sig, double val, Time&& time);
         *              * void (Signal::Instance&& inst, Signal::Event evt, int val, Time&& time);
         *              * void (Signal::Instance&& inst, Signal::Event evt, float val, Time&& time);
         *              * void (Signal::Instance&& inst, Signal::Event evt, double val, Time&& time);
         *              * void (Signal::Instance&& inst, Signal::Event evt, Time&& time);
         *  \param events   Types of Events to listen for.
         *  \return         Self. */
        template <typename H>
        Signal& set_callback(H& h, Signal::Event events = Signal::Event::UPDATE)
        {
            handler_data data = (handler_data)mpr_obj_get_prop_as_ptr(_obj, MPR_PROP_DATA, NULL);
            if (data)
                free(data);
            if (events > Signal::Event::NONE) {
                data = (handler_data)malloc(sizeof(struct _handler_data));
                _set_callback(data, h);
                mpr_sig_set_cb(_obj, _generic_handler, static_cast<int>(events));
                mpr_obj_set_prop(_obj, MPR_PROP_DATA, NULL, 1, MPR_PTR, data, 0);
            }
            else {
                mpr_sig_set_cb(_obj, NULL, 0);
                mpr_obj_remove_prop(_obj, MPR_PROP_DATA, NULL);
            }
            RETURN_SELF
        }

        /*! Remove the callback from a Signal.
         *  \return         Self. */
        Signal& remove_callback()
        {
            handler_data data = (handler_data)mpr_obj_get_prop_as_ptr(_obj, MPR_PROP_DATA, NULL);
            if (data) {
                mpr_sig_set_cb(_obj, NULL, 0);
                mpr_obj_remove_prop(_obj, MPR_PROP_DATA, NULL);
                free(data);
            }
            RETURN_SELF
        }

        /*! Activate and return a new Instance with an automatically-generated id.
         *  \return         The new Instance. */
        Instance instance()
        {
            mpr_id id = mpr_dev_generate_unique_id(mpr_sig_get_dev(_obj));
            return Instance(_obj, id);
        }

        /*! Retrieve an Instance with the provided id, activating a reserved instance if necessary.
         *  \param id       Id of the Instance to retrieve or activate.
         *  \return         An Instance. */
        Instance instance(Id id)
            { return Instance(_obj, id); }

        /*! Reserve a number of Instances.
         *  \param num      The number of Instances to reserve.
         *  \return         Self. */
        Signal& reserve_instances(int num)
            { mpr_sig_reserve_inst(_obj, num, NULL, NULL); RETURN_SELF }

        /*! Reserve a number of Instances with provided ids.
         *  \param num      The number of Instances to reserve.
         *  \param ids      An array of ids to associate with the reserved Instances.
         *                  Must be NULL or have size num.
         *  \return         Self. */
        Signal& reserve_instances(int num, mpr_id *ids)
            { mpr_sig_reserve_inst(_obj, num, ids, NULL); RETURN_SELF }

        /*! Reserve a number of Instances with provided ids and user_data.
         *  \param num      The number of Instances to reserve.
         *  \param ids      An array of num ids to associate with the reserved Instances.
         *                  Must be NULL or have size num.
         *  \param data     An array of num data pointers to associate with the reserved Instances.
         *                  Must be NULL or have size num.
         *  \return         Self. */
        Signal& reserve_instances(int num, Id *ids, void **data)
            { mpr_sig_reserve_inst(_obj, num, ids, data); RETURN_SELF }

        /*! Reserve a single Instance.
         *  \return         Self. */
        Signal& reserve_instance()
            { mpr_sig_reserve_inst(_obj, 1, NULL, NULL); RETURN_SELF }

        /*! Reserve a single Instance with the provided id.
         *  \param id       The id to associate with the Instance.
         *  \return         Self. */
        Signal& reserve_instance(mpr_id id)
            { mpr_sig_reserve_inst(_obj, 1, &id, NULL); RETURN_SELF }

        /*! Reserve a single Instance with the provided id and user_data.
         *  \param id       The id to associate with the Instance.
         *  \param data     User data to associate with the Instance.
         *  \return         Self. */
        Signal& reserve_instance(mpr_id id, void* data)
            { mpr_sig_reserve_inst(_obj, 1, &id, &data); RETURN_SELF }

        /*! Retrieve an Instance from the pool using an index and status.
         *  \param idx      The index of the Instance to retrieve.
         *  \param status   The status pool to query: ACTIVE, RESERVED, or ALL.
         *  \return         An Instance. */
        Instance instance(int idx, Instance::Status status) const
            { return Instance(_obj, mpr_sig_get_inst_id(_obj, idx, static_cast<mpr_status>(status))); }

        /*! Remove an Instance and free its resources.
         *  \param instance The Instance to remove.
         *  \return         Self. */
        Signal& remove_instance(Instance instance)
            { mpr_sig_remove_inst(_obj, instance._id); RETURN_SELF }

        /*! Retrieve the oldest active Instance.
         *  \return         An Instance. */
        Instance oldest_instance()
            { return Instance(_obj, mpr_sig_get_oldest_inst_id(_obj)); }

        /*! Retrieve the newest active Instance.
         *  \return         An Instance. */
        Instance newest_instance()
            { return Instance(_obj, mpr_sig_get_newest_inst_id(_obj)); }

        /*! Retrieve the number of Instances belonging to this Signal.
         *  \param status   The status of the Instance to count: Instance::Status::ACTIVE, RESERVED, or ALL.
         *  \return         The number of Instances with the given status. */
        int num_instances(Instance::Status status = Instance::Status::ACTIVE) const
            { return mpr_sig_get_num_inst(_obj, static_cast<mpr_status>(status)); }

        OBJ_METHODS(Signal);
    };

    /*! A Device is an entity on the network which has input and/or output Signals.  The Device is
     *  the primary interface through which a program uses libmapper.  A Device must have a name,
     *  to which a unique ordinal is subsequently appended.  It can also be given other
     *  user-specified metadata.  Signals can be mapped using local code or messages over the
     *  network, usually sent from an external GUI. */
    class Device : public Object
    {
    private:
        void maybe_free() {
            if (_owned && _obj && decr_refcount() <= 0) {
                mpr_list sigs = mpr_dev_get_sigs(_obj, MPR_DIR_ANY);
                while (sigs) {
                    const void *data = mpr_obj_get_prop_as_ptr((mpr_sig)*sigs, MPR_PROP_DATA, NULL);
                    if (data)
                        free((void*)data);
                    sigs = mpr_list_get_next(sigs);
                }
                mpr_dev_free(_obj);
                free(_refcount_ptr);
            }
        }
    public:
        Device() : Object() {}
        /*! Allocate and initialize a Device.
         *  \param name     A short descriptive string to identify the Device.
         *                  Must not contain spaces or the slash character '/'.
         *  \param graph    A previously allocated Graph object to use.
         *  \return         A newly allocated Device. */
        Device(const str_type &name, const Graph& graph);

        /*! Allocate and initialize a Device.
         *  \param name     A short descriptive string to identify the Device.
         *                  Must not contain spaces or the slash character '/'.
         *  \return         A newly allocated Device. */
        Device(const str_type &name) : Object(NULL)
        {
            _obj = mpr_dev_new(name, 0);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        /* Copy constructor */
        Device(const Device& orig) : Object(orig._obj)
        {
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            if (_owned)
                incr_refcount();
        }
        /* Move constructor */
        Device(Device&& orig) noexcept : Object(orig._obj)
        {
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            orig._obj = NULL;
            orig._owned = 0;
            /* do not modify refcount */
        }
        /* Copy assignment operator */
        Device& operator=(const Device& orig) noexcept
        {
            maybe_free();
            _obj = orig._obj;
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            if (_owned)
                incr_refcount();
            return *this;
        }
        /* Move assignment operator */
        Device& operator=(Device&& orig) noexcept
        {
            maybe_free();
            _obj = orig._obj;
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            orig._obj = 0;
            orig._owned = 0;
            return *this;
        }
        Device(mpr_dev dev) : Object(dev)
            { _owned = false; }
        ~Device()
            { maybe_free(); }
        operator mpr_dev() const
            { return _obj; }

        /*! Add a Signal to this Device.
         *  \param dir      Directionality of the signal to create. Must be either
         *                  Direction::INCOMING or Direction::OUTGOING.
         *  \param name     A descriptive name for the signal.
         *  \param len      Vector length for the signal.
         *  \param type     Data type for the signal (INT32, FLOAT, or DOUBLE).
         *  \param unit     Descriptive unit for the signal (optional).
         *  \param min      Minimum value for the signal (optional). Must point to an array of
         *                  length and type specified by the len and type arguments.
         *  \param max      maximum value for the signal (optional). Must point to an array of
         *                  length and type specified by the len and type arguments.
         *  \param num_inst The number of instances to be allocated for the signal. For singleton
         *                  (non-instanced) signals pass NULL for this argument, otherwise provide
         *                  the number of instances to pre-allocate. Additional instances may be
         *                  allocated later using Signal::reserve_instances().
         *  \return         A newly allocated Signal. */
        Signal add_signal(Direction dir, const str_type &name, int len, Type type,
                          const str_type &unit=0, void *min=0, void *max=0, int *num_inst=0)
        {
            return Signal(_obj, static_cast<mpr_dir>(dir), name, len, static_cast<mpr_type>(type),
                          unit, min, max, num_inst);
        }
        /*! Remove and destroy a Signal from this Device.
         *  \param sig      The signal to remove.
         *  \return         Self. */
        Device& remove_signal(Signal& sig)
        {
            sig.remove_callback();
            mpr_sig_free(sig._obj);
            RETURN_SELF
        }

        /*! Get a list of Signals belonging to this Device.
         *  \param dir      The directionality of the Signals to include in the list. Use
         *                  Direction::ANY to include all Signals.
         *  \return         A List of Signals. */
        List<Signal> signals(Direction dir = Direction::ANY) const
            { return List<Signal>(mpr_dev_get_sigs(_obj, static_cast<mpr_dir>(dir))); }

        /*! Get a list of Maps involving this Device.
         *  \param dir      The directionality of the Maps (with reference to the Device) to
         *                  include in the list. Use Direction::ANY to include all Maps.
         *  \return         A List of Maps. */
        List<Map> maps(Direction dir = Direction::ANY) const
            { return List<Map>(mpr_dev_get_maps(_obj, static_cast<mpr_dir>(dir))); }

        /*! Poll this device for new messages.  Note, if you have multiple devices, the right thing
         *  to do is call this function for each of them with block_ms=0, and add your own sleep if
         *  necessary.
         *  \param block_ms     The number of milliseconds to block, or 0 for non-blocking behavior.
         *  \return             The number of handled messages. */
        int poll(int block_ms=0) const
            { return mpr_dev_poll(_obj, block_ms); }

        /*! Start automatically polling this Device for new messages in a separate thread.
         *  \return   Self. */
        Device& start()
            { mpr_dev_start_polling(_obj); RETURN_SELF }

        /*! Stop automatically polling this Device for new messages in a separate thread.
         *  \return   Self. */
        Device& stop()
            { mpr_dev_stop_polling(_obj); RETURN_SELF }

        /*! Detect whether a device is completely initialized.
         *  \return         Non-zero if device is completely initialized, i.e., has an allocated
         *                  receiving port and unique identifier. Zero otherwise. */
        bool ready() const
            { return mpr_dev_get_is_ready(_obj); }

        /*! Get the current time for a device.
         *  \return         The current time. */
        Time get_time()
            { return mpr_dev_get_time(_obj); }

        /*! Set the time for a device. Use only if user code has access to a more accurate
         *  timestamp than the operating system.
         *  \param time     The time to set. This time will be used for tagging signal updates until
         *                  the next occurrence mpr_dev_set_time() or mpr_dev_poll().
         *  \return   Self. */
        Device& set_time(Time time)
            { mpr_dev_set_time(_obj, *time); RETURN_SELF }

        /*! Indicate that all signal values have been updated for a given timestep. This function
         *  can be omitted if poll() is called each sampling timestep, however calling poll() at a
         *  lower rate may be more performant.
         *  \return   Self. */
        Device& update_maps()
            { mpr_dev_update_maps(_obj); RETURN_SELF }

        OBJ_METHODS(Device);

        friend std::ostream& operator<<(std::ostream& os, const mapper::Device& dev);
    };

    class device_type {
    public:
        device_type(mpr_dev dev) { _dev = dev; }
        device_type(const Device& dev) { _dev = (mpr_dev)dev; }
        operator mpr_dev() const { return _dev; }
        mpr_dev _dev;
    };

    /*! Graphs are the primary interface through which a program may observe the network and store
     *  information about Devices and Signals that are present. Each Graph stores records of
     *  Devices, Signals, and Maps, which can be queried. */
    class Graph : public Object
    {
    public:
        /*! The set of possible graph events, used to inform callbacks. */
        enum class Event
        {
            OBJ_NEW = MPR_OBJ_NEW,  /*!< New record has been added to the graph. */
            OBJ_MOD = MPR_OBJ_MOD,  /*!< The existing record has been modified. */
            OBJ_REM = MPR_OBJ_REM,  /*!< The existing record has been removed. */
            OBJ_EXP = MPR_OBJ_EXP   /*!< The graph has lost contact with the remote entity. */
        };
    private:
        enum handler_type {
            OBJECT,
            DEVICE,
            SIGNAL,
            MAP
        };
        typedef struct _handler_data {
            union {
                void (*object)(Graph&&, Object&&, Graph::Event);
                void (*device)(Graph&&, Device&&, Graph::Event);
                void (*signal)(Graph&&, Signal&&, Graph::Event);
                void (*map)(Graph&&, Map&&, Graph::Event);
            } handler;
            enum handler_type type;
        } *handler_data;
        static void _generic_handler(mpr_graph g, mpr_obj o, mpr_graph_evt e, const void *user)
        {
            handler_data data = (handler_data)user;
            switch (data->type) {
                case OBJECT:
                    switch (mpr_obj_get_type(o)) {
                        case MPR_DEV:
                            data->handler.object(Graph(g), Device(o), Graph::Event(e));
                            break;
                        case MPR_SIG:
                            data->handler.object(Graph(g), Signal(o), Graph::Event(e));
                            break;
                        case MPR_MAP:
                            data->handler.object(Graph(g), Map(o), Graph::Event(e));
                            break;
                    }
                    break;
                case DEVICE:
                    data->handler.device(Graph(g), Device(o), Graph::Event(e));
                    break;
                case SIGNAL:
                    data->handler.signal(Graph(g), Signal(o), Graph::Event(e));
                    break;
                case MAP:
                    data->handler.map(Graph(g), Map(o), Graph::Event(e));
                    break;
            }
        }
        int _set_callback(handler_data data, mpr_type types,
                          void (*h)(Graph&&, Object&&, Graph::Event))
        {
            data->type = OBJECT;
            data->handler.object = h;
            return types;
        }
        int _set_callback(handler_data data, mpr_type types,
                          void (*h)(Graph&&, Device&&, Graph::Event))
        {
            data->type = DEVICE;
            data->handler.device = h;
            return MPR_DEV;
        }
        int _set_callback(handler_data data, mpr_type types,
                          void (*h)(Graph&&, Signal&&, Graph::Event))
        {
            data->type = SIGNAL;
            data->handler.signal = h;
            return MPR_SIG;
        }
        int _set_callback(handler_data data, mpr_type types,
                          void (*h)(Graph&&, Map&&, Graph::Event))
        {
            data->type = MAP;
            data->handler.map = h;
            return MPR_MAP;
        }
        void maybe_free() {
            if (_owned && _obj && decr_refcount() <= 0) {
                mpr_graph_free(_obj);
                free(_refcount_ptr);
            }
        }
    public:
        /*! Create a peer in the libmapper distributed graph.
         *  \param types    Sets whether the graph should automatically subscribe to information
         *                  about Signals and Maps when it encounters a previously-unseen Device.
         *  \return         The new Graph. */
        Graph(Type types = Type::OBJECT)
        {
            _obj = mpr_graph_new(static_cast<mpr_type>(types));
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        /* Copy constructor */
        Graph(const Graph& orig) : Object(orig._obj)
        {
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            if (_owned)
                incr_refcount();
        }
        /* Move constructor */
        Graph(Graph&& orig) noexcept : Object(orig._obj)
        {
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            orig._obj = NULL;
            orig._owned = 0;
            /* do not modify refcount */
        }
        /* Copy assignment operator */
        Graph& operator=(const Graph& orig) noexcept
        {
            maybe_free();
            _obj = orig._obj;
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            if (_owned)
                incr_refcount();
            return *this;
        }
        /* Move assignment operator */
        Graph& operator=(Graph&& orig) noexcept
        {
            maybe_free();
            _obj = orig._obj;
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            orig._obj = 0;
            orig._owned = 0;
            return *this;
        }
        Graph(mpr_graph graph) : Object(graph) {}
        ~Graph()
            { maybe_free(); }
        operator mpr_graph() const
            { return _obj; }

        /*! Specify the network interface to use.
         *  \param iface        A string specifying the name of the network interface to use.
         *  \return             Self. */
        Graph& set_iface(const str_type &iface)
            { mpr_graph_set_interface(_obj, iface); RETURN_SELF }

        /*! Return a string indicating the name of the network interface in use.
         *  \return     A string containing the name of the network interface.*/
        std::string iface() const
        {
            const char *iface = mpr_graph_get_interface(_obj);
            return iface ? std::string(iface) : 0;
        }

        /*! Specify the multicast group and port to use.
         *  \param group    A string specifying the multicast group to use.
         *  \param port     The multicast port to use.
         *  \return         Self. */
        Graph& set_address(const str_type &group, int port)
            { mpr_graph_set_address(_obj, group, port); RETURN_SELF }

        /*! Retrieve the multicast url currently in use.
         *  \return     A string specifying the multicast url in use. */
        std::string address() const
            { return std::string(mpr_graph_get_address(_obj)); }

        /*! Synchonize a Graph object with the distributed graph.
         *  \param block_ms     The number of milliseconds to block, or 0 for non-blocking behavior.
         *  \return             The number of handled messages. */
        int poll(int block_ms=0) const
            { return mpr_graph_poll(_obj, block_ms); }

        /*! Start automatically synchonizing a Graph object in a separate thread.
         *  \return   Self. */
        Graph& start()
            { mpr_graph_start_polling(_obj); RETURN_SELF }

        /*! Stop automatically synchonizing a Graph object in a separate thread.
         *  \return   Self. */
        Graph& stop()
            { mpr_graph_stop_polling(_obj); RETURN_SELF }

        // subscriptions
        /*! Subscribe to information about a specific Device.
         *  \param dev      The Device of interest.
         *  \param types    Bitflags setting the type of information of interest. Can be a
         *                  combination of MPR_DEV, MPR_SIG_IN, MPR_SIG_OUT, MPR_SIG, MPR_MAP_IN,
         *                  MPR_MAP_OUT, MPR_MAP, or simply MPR_OBJ for all information.
         *  \param timeout  The desired duration in seconds for this subscription. If set to -1,
         *                  the graph will automatically renew the subscription until it is
         *                  freed or this function is called again.
         *  \return         Self. */
        const Graph& subscribe(const device_type& dev, Type types, int timeout)
            { mpr_graph_subscribe(_obj, dev, static_cast<mpr_type>(types), timeout); RETURN_SELF }

        /*! Subscribe to information about all discovered Devices.
         *  \param types    Bitflags setting the type of information of interest. Can be a
         *                  combination of MPR_DEV, MPR_SIG_IN, MPR_SIG_OUT, MPR_SIG, MPR_MAP_IN,
         *                  MPR_MAP_OUT, MPR_MAP, or simply MPR_OBJ for all information.
         *  \return         Self. */
        const Graph& subscribe(Type types)
            { mpr_graph_subscribe(_obj, 0, static_cast<mpr_type>(types), -1); RETURN_SELF }

        /*! Unsubscribe from information about a specific Device.
         *  \param dev      The Device of interest.
         *  \return         Self. */
        const Graph& unsubscribe(const device_type& dev)
            { mpr_graph_unsubscribe(_obj, dev); RETURN_SELF }

        /*! Cancel all subscriptions.
         *  \return         Self. */
        const Graph& unsubscribe()
            { mpr_graph_unsubscribe(_obj, 0); RETURN_SELF }

        /*! Register a callback for when an Object record is added, updated, or removed.
         *  \param h        Callback function.
         *  \param types    Bitflags setting the type of information of interest.
         *                  Can be a combination of Type values.
         *  \return         Self. */
        template <typename T>
        const Graph& add_callback(void (*h)(Graph&&, T&&, Graph::Event),
                                  Type types = Type::OBJECT)
        {
            handler_data data = (handler_data)malloc(sizeof(struct _handler_data));
            int mtypes = _set_callback(data, static_cast<mpr_type>(types), h);
            mpr_graph_add_cb(_obj, _generic_handler, mtypes, data);
            RETURN_SELF
        }

        /*! Remove an Object record callback from the Graph service.
         *  \param h        Callback function.
         *  \return         Self. */
        const Graph& remove_callback(void (*h)(Graph&&, Object&&, Graph::Event))
        {
            // need to recover and free data
            void *data = mpr_graph_remove_cb(_obj, _generic_handler, reinterpret_cast<void*>(h));
            if (data)
                free(data);
            RETURN_SELF
        }

        /*! Return a List of all Devices.
         *  \return         A List of Devices. */
        List<Device> devices() const
            { return List<Device>(mpr_graph_get_list(_obj, MPR_DEV)); }

        /*! Return a List of all Signals.
         *  \return         A List of Signals. */
        List<Signal> signals() const
            { return List<Signal>(mpr_graph_get_list(_obj, MPR_SIG)); }

        /*! Return a List of all Maps.
         *  \return         A List of Maps. */
        List<Map> maps() const
            { return List<Map>(mpr_graph_get_list(_obj, MPR_MAP)); }

        OBJ_METHODS(Graph);
    };

    class PropVal
    {
    protected:
        template <typename P, typename T>
        PropVal(P&& _prop, T&& _val) : PropVal(_prop)
            { _set(_val); }
        template <typename P, typename T>
        PropVal(P&& _prop, int _len, T&& _val) : PropVal(_prop)
            { _set(_len, _val); }
        template <typename P, typename T, size_t N>
        PropVal(P&& _prop, std::array<T, N> _val) : PropVal(_prop)
            { _set(_val); }
        template <typename P, typename T>
        PropVal(P&& _prop, std::vector<T> _val) : PropVal(_prop)
            { _set(_val); }
        template <typename P, typename T>
        PropVal(P&& _prop, int _len, Type _type, T&& _val) : PropVal(_prop)
            { _set(_len, _type, _val); }
    public:
        ~PropVal()
            { maybe_free(); }

        operator bool() const
        {
            if (!len || !type || !val)
                return false;
            if (len > 1)
                return true;
            switch (type) {
                case MPR_BOOL:
                case MPR_INT32: return *(int*)val != 0;
                case MPR_FLT:   return *(float*)val != 0.f;
                case MPR_DBL:   return *(double*)val != 0.;
                default:        return val != 0;
            }
        }
        template <typename T>
        operator T*() const
            { return (T*)(len > 1 ? val : &val); }
        template <typename T>
        operator T() const
            { return *(T*)val; }
        operator const char*() const
        {
            if (!val || !len || type != MPR_STR)
                return NULL;
            return len > 1 ? ((const char**)val)[0] : (const char*)val;
        }
        operator const char**() const
        {
            if (!val || !len || type != MPR_STR)
                return NULL;
            return (const char**)(len > 1 ? val : &val);
        }
        operator std::string() const
        {
            if (!val || !len || type != MPR_STR)
                return NULL;
            return std::string(len > 1 ? *(const char**)val : (const char*)val);
        }
        operator void*() const
        {
            if (MPR_PTR != type) return 0;
            return (void*)val;
        }
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
                for (size_t i = 0; i < N && i < len; i++)
                    temp_a[i] = tempp[i];
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
                for (size_t i = 0; i < N && i < len; i++)
                    temp_a[i] = std::string(tempp[i]);
            }
            return temp_a;
        }
        template <typename T>
        operator const std::vector<T>() const
        {
            std::vector<T> temp_v;
            for (unsigned int i = 0; i < len; i++)
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
        operator List<Device>() const
            { return (type == MPR_LIST) ? (mpr_list)val : NULL; }
        operator List<Signal>() const
            { return (type == MPR_LIST) ? (mpr_list)val : NULL; }
        operator List<Map>() const
            { return (type == MPR_LIST) ? (mpr_list)val : NULL; }

        template <typename... Values>
        PropVal& operator = (Values... vals)
            { _set(vals...); RETURN_SELF }

        friend std::ostream& operator<<(std::ostream& os, const PropVal& p);

        const char *key;
    protected:
        mpr_prop prop;

        mpr_type type;
        unsigned int len;
        const void *val;
        bool pub;
        friend class Graph;
        friend class Object;
        friend class List<Device>;
        friend class List<Signal>;
        friend class List<Map>;
        mpr_obj parent = NULL;

        PropVal(mpr_prop _prop, const str_type &_key, int _len, mpr_type _type,
                const void *_val, int _pub)
        {
            prop = _prop;
            key = _key;
            _set(_len, _type, _val);
            owned = false;
            pub = _pub;
        }
        PropVal(Property _prop)
        {
            prop = static_cast<mpr_prop>(_prop);
            key = NULL;
            val = 0;
            owned = false;
            pub = true;
        }
        PropVal(const str_type&& _key)
        {
            prop = MPR_PROP_UNKNOWN;
            key = _key;
            val = 0;
            owned = false;
            pub = true;
        }
    private:
        bool owned;

        void maybe_free()
        {
            if (owned && val) {
                if (type == MPR_STR && len > 1) {
                    for (unsigned int i = 0; i < len; i++)
                        free(((char**)val)[i]);
                }
                free((void*)val);
                owned = false;
            }
        }
        void maybe_update()
        {
            if (parent)
                mpr_obj_set_prop(parent, prop, key, len, type, val, pub);
        }
        void _set(int _len, mpr_type _type, const void *_val)
        {
            type = _type;
            val = _val;
            len = _len;
            pub = (MPR_PTR != _type);
            maybe_update();
        }
        void _set(int _len, bool _val[])
        {
            int *ival = (int*)malloc(sizeof(int)*_len);
            if (!ival) {
                val = 0;
                return;
            }
            for (int i = 0; i < _len; i++)
                ival[i] = (int)_val[i];
            val = ival;
            len = _len;
            type = MPR_INT32;
            owned = true;
            maybe_update();
        }
        void _set(int _len, void* _val[])
            { _set(_len, MPR_PTR, (1 == _len) ? (void*)_val[0] : _val); }
        void _set(int _len, int _val[])
            { _set(_len, MPR_INT32, _val); }
        void _set(int _len, float _val[])
            { _set(_len, MPR_FLT, _val); }
        void _set(int _len, double _val[])
            { _set(_len, MPR_DBL, _val); }
        void _set(int _len, mpr_type _val[])
            { _set(_len, MPR_TYPE, _val); }
        void _set(int _len, const char *_val[])
            { _set(_len, MPR_STR, (1 == _len) ? (void*)_val[0] : (void*)_val); }
        template <typename T>
        void _set(const T _val)
            { _set(1, (T*)&_val); }
        template <typename T>
        void _set(const T* _val)
            { _set(1, (const T**)&_val); }
        template <typename T, size_t N>
        void _set(const std::array<T, N>& _val)
        {
            if (!_val.empty())
                _set(N, _val.data());
            else {
                val = 0;
                len = 0;
            }
            maybe_update();
        }
        template <size_t N>
        void _set(const std::array<const char*, N>& _vals)
        {
            len = N;
            type = MPR_STR;
            if (len == 1)
                val = strdup(_vals[0]);
            else if (len > 1) {
                // need to copy string array
                val = (char**)malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++)
                    ((char**)val)[i] = strdup((char*)_vals[i]);
                owned = true;
            }
            else
                val = 0;
            maybe_update();
        }
        template <size_t N>
        void _set(const std::array<std::string, N>& _vals)
        {
            len = N;
            type = MPR_STR;
            if (len == 1)
                val = strdup(_vals[0].c_str());
            else if (len > 1) {
                // need to copy string array
                val = (char**)malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++)
                    ((char**)val)[i] = strdup((char*)_vals[i].c_str());
                owned = true;
            }
            else
                val = 0;
            maybe_update();
        }
        void _set(int _len, const std::string _vals[])
        {
            len = _len;
            type = MPR_STR;
            if (len == 1)
                val = strdup(_vals[0].c_str());
            else if (len > 1) {
                // need to copy string array
                val = malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++)
                    ((char**)val)[i] = strdup((char*)_vals[i].c_str());
                owned = true;
            }
            else
                val = 0;
            maybe_update();
        }
        template <typename T>
        void _set(const std::vector<T> _val)
            { _set((int)_val.size(), _val.data()); }
        void _set(const std::vector<const char*>& _val)
        {
            len = (int)_val.size();
            type = MPR_STR;
            if (len == 1)
                val = strdup(_val[0]);
            else {
                // need to copy string array since std::vector may free it
                val = malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++)
                    ((char**)val)[i] = strdup((char*)_val[i]);
                owned = true;
            }
            maybe_update();
        }
        void _set(const std::vector<std::string>& _val)
        {
            len = (int)_val.size();
            type = MPR_STR;
            if (len == 1)
                val = strdup(_val[0].c_str());
            else if (len > 1) {
                // need to copy string array
                val = malloc(sizeof(char*) * len);
                for (unsigned int i = 0; i < len; i++)
                    ((char**)val)[i] = strdup((char*)_val[i].c_str());
                owned = true;
            }
            else
                val = 0;
            maybe_update();
        }
        void _set(mpr_list _val)
            { _set(1, MPR_LIST, _val); }

        // handle some enum classes
        void _set(Direction dir)
            { _set(static_cast<int>(dir)); }
        void _set(Map::Location loc)
            { _set(static_cast<int>(loc)); }
        void _set(Map::Protocol proto)
            { _set(static_cast<int>(proto)); }
        void _set(Signal::Stealing stl)
            { _set(static_cast<int>(stl)); }
    };

    template <typename... Values>
    inline Object& Object::set_property(const Values... vals)
    {
        PropVal p(vals...);
        if (p.prop != MPR_PROP_DATA && (!p.key || strcmp(p.key, "data")))
            mpr_obj_set_prop(_obj, p.prop, p.key, p.len, p.type, p.val, p.pub);
        RETURN_SELF;
    }

    inline PropVal Object::property(const str_type &key) const
    {
        mpr_prop prop;
        mpr_type type;
        const void *val;
        int len, pub;
        prop = mpr_obj_get_prop_by_key(_obj, key, &len, &type, &val, &pub);
        PropVal p(prop, key, len, type, val, pub);
        p.parent = _obj;
        return p;
    }

    inline PropVal Object::property(int idx) const
    {
        const char *key;
        mpr_type type;
        const void *val;
        int len, pub;
        mpr_prop mprop = mpr_obj_get_prop_by_idx(_obj, idx, &key, &len, &type, &val, &pub);
        PropVal p(mprop, key, len, type, val, pub);
        p.parent = _obj;
        return p;
    }

    inline PropVal Object::property(Property prop) const
        { return property(static_cast<mpr_prop>(prop)); }

    template <typename T>
    inline PropVal Object::operator [] (const T prop) const
        { return property(prop); }

    template <class T>
    template <typename P, typename V>
    inline List<T>& List<T>::filter(P&& property, V&& value, Operator op)
    {
        PropVal p(property, value);
        _list = mpr_list_filter(_list, p.prop, p.key, p.len, p.type, p.val, static_cast<mpr_op>(op));
        RETURN_SELF;
    }

    template <class T>
    template <typename... Values>
    inline List<T>& List<T>::set_property(const Values... vals)
    {
        PropVal p(vals...);
        if (!p || p.prop == MPR_PROP_DATA || (p.key && !strcmp(p.key, "data")))
            RETURN_SELF;
        mpr_list cpy = mpr_list_get_cpy(_list);
        while (cpy) {
            mpr_obj_set_prop(*cpy, p.prop, p.key, p.len, p.type, p.val, p.pub);
            cpy = mpr_list_get_next(cpy);
        }
        RETURN_SELF;
    }

    inline Graph Object::graph() const
        { return Graph(mpr_obj_get_graph(_obj)); }

    inline Device::Device(const str_type &name, const Graph& graph) : Object(NULL)
    {
        _obj = mpr_dev_new(name, graph);
        _owned = true;
        _refcount_ptr = (int*)malloc(sizeof(int));
        *_refcount_ptr = 1;
    }

    inline signal_type::signal_type(const Signal& sig)
        { _sig = (mpr_sig)sig; }

    inline Map& Map::add_scope(const Device& dev)
        { mpr_map_add_scope(_obj, mpr_dev(dev)); RETURN_SELF }

    inline Map& Map::remove_scope(const Device& dev)
        { mpr_map_remove_scope(_obj, mpr_dev(dev)); RETURN_SELF }

    inline Device Signal::device() const
        { return Device(mpr_sig_get_dev(_obj)); }

    inline std::string version()
        { return std::string(mpr_get_version()); }

    inline constexpr Type operator|(Type l, Type r)
    {
        return static_cast<Type>(static_cast<mpr_type>(l) | static_cast<mpr_type>(r));
    }

    inline constexpr Signal::Event operator|(Signal::Event l, Signal::Event r)
    {
        return static_cast<Signal::Event>(static_cast<mpr_sig_evt>(l) | static_cast<mpr_sig_evt>(r));
    }

    inline std::ostream& operator<<(std::ostream &os, const Direction& d)
    {
        switch (d) {
            case Direction::INCOMING: os << "INCOMING"; break;
            case Direction::OUTGOING: os << "OUTGOING"; break;
            case Direction::ANY:      os << "ANY";      break;
            case Direction::BOTH:     os << "BOTH";     break;
        }
        return os;
    }

    inline std::ostream& operator<<(std::ostream &os, const Map::Location& d)
    {
        switch (d) {
            case Map::Location::SRC: os << "SRC"; break;
            case Map::Location::DST: os << "DST"; break;
            case Map::Location::ANY: os << "ANY"; break;
        }
        return os;
    }

    inline std::ostream& operator<<(std::ostream &os, const Map::Protocol& d)
    {
        switch (d) {
            case Map::Protocol::UDP: os << "UDP"; break;
            case Map::Protocol::TCP: os << "TCP"; break;
        }
        return os;
    }

    inline std::ostream& operator<<(std::ostream &os, const Object::Status& s)
    {
        switch (s) {
            case Object::Status::UNDEFINED: os << "UNDEFINED";  break;
            case Object::Status::EXPIRED:   os << "EXPIRED";    break;
            case Object::Status::STAGED:    os << "STAGED";     break;
            case Object::Status::READY:     os << "READY";      break;
        }
        return os;
    }

    inline std::ostream& operator<<(std::ostream &os, const Signal::Stealing& s)
    {
        switch (s) {
            case Signal::Stealing::NONE:    os << "NONE";   break;
            case Signal::Stealing::OLDEST:  os << "OLDEST"; break;
            case Signal::Stealing::NEWEST:  os << "NEWEST"; break;
        }
        return os;
    }

    inline std::ostream& operator<<(std::ostream &os, const Type& s)
    {
        switch (s) {
            case Type::DEVICE:      os << "DEVICE";     break;
            case Type::SIGNAL_IN:   os << "SIGNAL_IN";  break;
            case Type::SIGNAL_OUT:  os << "SIGNAL_OUT"; break;
            case Type::SIGNAL:      os << "SIGNAL";     break;
            case Type::MAP_IN:      os << "MAP_IN";     break;
            case Type::MAP_OUT:     os << "MAP_OUT";    break;
            case Type::MAP:         os << "MAP";        break;
            case Type::OBJECT:      os << "OBJECT";     break;
            case Type::LIST:        os << "LIST";       break;
            case Type::BOOLEAN:     os << "BOOLEAN";    break;
            case Type::TYPE:        os << "TYPE";       break;
            case Type::DOUBLE:      os << "DOUBLE";     break;
            case Type::FLOAT:       os << "FLOAT";      break;
            case Type::INT64:       os << "INT64";      break;
            case Type::INT32:       os << "INT32";      break;
            case Type::STRING:      os << "STRING";     break;
            case Type::TIME:        os << "TIME";       break;
            case Type::POINTER:     os << "POINTER";    break;
        }
        return os;
    }

    #define OSTREAM_TYPE(TYPE)                  \
    if (p.len == 1)                             \
        os << *(TYPE*)p.val;                    \
    else if (p.len > 1) {                       \
        os << "[";                              \
        for (unsigned int i = 0; i < p.len; i++)\
            os << ((TYPE*)p.val)[i] << ", ";    \
        os << "\b\b]";                          \
    }

    inline std::ostream& operator<<(std::ostream& os, const PropVal& p)
    {
        if (p.len <= 0 || p.type == MPR_NULL)
            return os << "NULL";
        switch (p.type) {
            case MPR_INT32:
                // handle some Enums
                switch (p.prop) {
                    case MPR_PROP_DIR:
                        os << Direction(*(int*)p.val);
                        break;
                    case MPR_PROP_PROCESS_LOC:
                        os << Map::Location(*(int*)p.val);
                        break;
                    case MPR_PROP_PROTOCOL:
                        os << Map::Protocol(*(int*)p.val);
                        break;
                    case MPR_PROP_STATUS:
                        os << Object::Status(*(int*)p.val);
                        break;
                    case MPR_PROP_STEAL_MODE:
                        os << Signal::Stealing(*(int*)p.val);
                        break;
                    default:
                        OSTREAM_TYPE(int);
                }
                break;
            case MPR_INT64:     OSTREAM_TYPE(int64_t);  break;
            case MPR_FLT:       OSTREAM_TYPE(float);    break;
            case MPR_DBL:       OSTREAM_TYPE(double);   break;
            case MPR_BOOL:      OSTREAM_TYPE(bool);     break;
            case MPR_STR:
                if (p.len == 1)
                    os << (const char*)p.val;
                else if (p.len > 1) {
                    os << "[";
                    for (unsigned int i = 0; i < p.len; i++)
                        os << ((const char**)p.val)[i] << ", ";
                    os << "\b\b]";
                }
                break;
            case MPR_TIME:
                os << Time(*(int64_t*)p.val);
                break;
            case MPR_TYPE:
                os << Type(*(int*)p.val);
                break;
            case MPR_DEV:
                os << Device((mpr_dev)p.val);
                break;
            case MPR_SIG:
                os << Signal((mpr_sig)p.val);
                break;
            case MPR_MAP:
                os << Map((mpr_map)p.val);
                break;
            case MPR_LIST:
                if (p.val && *(mpr_list)p.val) {
                    switch (mpr_obj_get_type(*(mpr_list)p.val)) {
                        case MPR_DEV:
                            os << "<mapper::List<mapper::Device>>";
                            break;
                        case MPR_SIG:
                            os << "<mapper::List<mapper::Signal>>";
                            break;
                        case MPR_MAP:
                            os << "<mapper::List<mapper::Map>>";
                            break;
                    }
                }
                else
                    os << "<mapper::List<empty>>";
                break;
            default:
                os << "Property type not handled by ostream operator!";
        }
        return os;
    }

    inline std::ostream& operator<<(std::ostream& os, const Device& dev)
    {
        return os << "<mapper::Device '" << dev[Property::NAME] << "'>";
    }

    inline std::ostream& operator<<(std::ostream& os, const Signal& sig)
    {
        os << "<mapper::Signal '" << sig.device()[Property::NAME] << ":" << sig[Property::NAME] << "'>";
        return os;
    }

    inline std::ostream& operator<<(std::ostream& os, const Map& map)
    {
        os << "<mapper::Map ";

        // add sources
        os << "[";
        for (const Signal s : map.signals(Map::Location::SRC))
            os << s << ", ";
        os << "\b\b] -> ";

        // add destinations
        os << "[";
        for (const Signal s : map.signals(Map::Location::DST))
            os << s << ", ";
        os << "\b\b]";

        os << ">";
        return os;
    }

    inline std::ostream& operator<<(std::ostream& os, const Object& o)
    {
        mpr_obj obj = (mpr_obj)o;
        switch (mpr_obj_get_type(obj)) {
            case MPR_DEV: os << Device(obj); break;
            case MPR_SIG: os << Signal(obj); break;
            case MPR_MAP: os << Map(obj);    break;
            default:                                 break;
        }
        return os;
    }

    inline std::ostream& operator<<(std::ostream& os, const Time& t)
    {
        os << "<mapper::Time " << t._time.sec << ":" << t._time.frac << ">";
        return os;
    }
};

#endif // _MPR_CPP_H_
