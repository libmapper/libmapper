
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
    /*! Set arbitrary properties for a CLASS_NAME.                       */ \
    /*!  \param values  The Properties to add or modify.                 */ \
    /*!  \return        Self.                                            */ \
    template <typename... Values>                                           \
    CLASS_NAME& set_property(Values... values)                              \
    {                                                                       \
        Property p(values...);                                              \
        if (p)                                                              \
            set_property(&p);                                               \
        return (*this);                                                     \
    }                                                                       \
    /*! Remove a named Property from a CLASS_NAME.                       */ \
    /*!  \param key     The Property to remove.                          */ \
    /*!  \return        Self.                                            */ \
    CLASS_NAME& remove_property(const string_type &key)                     \
    {                                                                       \
        if (PTR && key)                                                     \
            MAPPER_FUNC(NAME, remove_property)(PTR, key);                   \
        return (*this);                                                     \
    }                                                                       \
    /*! Retrieve the number of properties for a specific CLASS_NAME.     */ \
    /*!  \return        The number of properties.                        */ \
    int num_properties() const                                              \
    {                                                                       \
        return MAPPER_FUNC(NAME, num_properties)(PTR);                      \
    }                                                                       \
    /*! Retrieve a Property by name from a specific CLASS_NAME.          */ \
    /*!  \param key     The name of the Property to retrieve.            */ \
    /*!  \return        The retrieved Property.                          */ \
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
    /*! Retrieve a Property by index from a specific CLASS_NAME.         */ \
    /*!  \param index   The index of the Property to retrieve.           */ \
    /*!  \return        The retrieved Property.                          */ \
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
    /*! Clear all "staged" Properties from a specific CLASS_NAME.        */ \
    /*! Staged Properties are those that have not yet been synchronized  */ \
    /*! with the network by calling push().                              */ \
    /*!  \return        Self.                                            */ \
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
    /*! Add items found in CLASS_NAME Query rhs from this Query (without */ \
    /*! duplication).                                                    */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             Self.                                        */ \
    Query& join(const Query& rhs)                                           \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, union)(_query, rhs_cpy);                  \
        return (*this);                                                     \
    }                                                                       \
    /*! Remove items NOT found in CLASS_NAME Query rhs from this Query   */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             Self.                                        */ \
    Query& intersect(const Query& rhs)                                      \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, intersection)(_query, rhs_cpy);           \
        return (*this);                                                     \
    }                                                                       \
    /*! Remove items found in CLASS_NAME Query rhs from this Query       */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             Self.                                        */ \
    Query& subtract(const Query& rhs)                                       \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, difference)(_query, rhs_cpy);             \
        return (*this);                                                     \
    }                                                                       \
    /*! Add items found in CLASS_NAME Query rhs from this Query (without */ \
    /*! duplication).                                                    */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             A new Query containing the results.          */ \
    Query operator+(const Query& rhs) const                                 \
    {                                                                       \
        /* need to use copies of both queries */                            \
        MAPPER_TYPE(NAME) *lhs_cpy = QUERY_FUNC(NAME, copy)(_query);        \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        return Query(QUERY_FUNC(NAME, union)(lhs_cpy, rhs_cpy));            \
    }                                                                       \
    /*! Remove items NOT found in CLASS_NAME Query rhs from this Query   */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             A new Query containing the results.          */ \
    Query operator*(const Query& rhs) const                                 \
    {                                                                       \
        /* need to use copies of both queries */                            \
        MAPPER_TYPE(NAME) *lhs_cpy = QUERY_FUNC(NAME, copy)(_query);        \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        return Query(QUERY_FUNC(NAME, intersection)(lhs_cpy, rhs_cpy));     \
    }                                                                       \
    /*! Remove items found in CLASS_NAME Query rhs from this Query       */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             A new Query containing the results.          */ \
    Query operator-(const Query& rhs) const                                 \
    {                                                                       \
        /* need to use copies of both queries */                            \
        MAPPER_TYPE(NAME) *lhs_cpy = QUERY_FUNC(NAME, copy)(_query);        \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        return Query(QUERY_FUNC(NAME, difference)(lhs_cpy, rhs_cpy));       \
    }                                                                       \
    /*! Add items found in CLASS_NAME Query rhs from this Query (without */ \
    /*! duplication).                                                    */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             Self.                                        */ \
    Query& operator+=(const Query& rhs)                                     \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, union)(_query, rhs_cpy);                  \
        return (*this);                                                     \
    }                                                                       \
    /*! Remove items NOT found in CLASS_NAME Query rhs from this Query   */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             Self.                                        */ \
    Query& operator*=(const Query& rhs)                                     \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, intersection)(_query, rhs_cpy);           \
        return (*this);                                                     \
    }                                                                       \
    /*! Remove items found in CLASS_NAME Query rhs from this Query       */ \
    /*! \param rhs          A second CLASS_NAME query.                   */ \
    /*! \return             Self.                                        */ \
    Query& operator-=(const Query& rhs)                                     \
    {                                                                       \
        /* need to use copy of rhs query */                                 \
        MAPPER_TYPE(NAME) *rhs_cpy = QUERY_FUNC(NAME, copy)(rhs._query);    \
        _query = QUERY_FUNC(NAME, difference)(_query, rhs_cpy);             \
        return (*this);                                                     \
    }                                                                       \
                                                                            \
    /*! Retrieve an indexed CLASS_NAME item in the query.                */ \
    /*! \param idx           The index of the list element to retrieve.  */ \
    /*! \return              The retrieved CLASS_NAME.                   */ \
    CLASS_NAME operator [] (int idx)                                        \
        { return CLASS_NAME(QUERY_FUNC(NAME, index)(_query, idx)); }        \
                                                                            \
    /*! Convert this Query to a std::vector.                             */ \
    /*! \return              The converted Query results.                */ \
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
    /*! Set arbitrary properties for each CLASS_NAME in the Query.*/        \
    /*!  \param values  The Properties to add or modify.                 */ \
    /*!  \return        Self.                                            */ \
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
    /*! Remove a named Property from each CLASS_NAME in the Query.       */ \
    /*!  \param key     The Property to remove.                          */ \
    /*!  \return        Self.                                            */ \
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
    /*! Associate each CLASS_NAME ## s in the Query with an arbitrary    */ \
    /*! pointer.                                                         */ \
    /*! \param user_data    A pointer to user data to be associated.     */ \
    /*! \return             Self.                                        */ \
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
    /*! Push "staged" property changes for each CLASS_NAME in the Query  */ \
    /*! out to the network.                                              */ \
    /*! \return     Self.                                                */ \
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

#define DATABASE_METHODS(CLASS_NAME, NAME, QNAME)                           \
    /*! Register a callback for when a CLASS_NAME record is added or     */ \
    /*! updated in the Database.                                         */ \
    /*! \param h        Callback function.                               */ \
    /*! \param user     A user-defined pointer to be passed to the       */ \
    /*!                 callback for context.                            */ \
    /*! \return         Self.                                            */ \
    const Database&                                                         \
    add_ ## NAME ## _callback(mapper_database_ ## NAME ## _handler *h,      \
                              void *user) const                             \
    {                                                                       \
        mapper_database_add_ ## NAME ## _callback(_db, h, user);            \
        return (*this);                                                     \
    }                                                                       \
    /*! Remove a CLASS_NAME record callback from the Database service.   */ \
    /*! \param h        Callback function.                               */ \
    /*! \param user     The user context pointer that was originally     */ \
    /*!                 specified when adding the callback.              */ \
    /*! \return         Self.                                            */ \
    const Database&                                                         \
    remove_ ## NAME ## _callback(mapper_database_ ## NAME ## _handler *h,   \
                                 void *user) const                          \
    {                                                                       \
        mapper_database_remove_ ## NAME ## _callback(_db, h, user);         \
        return (*this);                                                     \
    }                                                                       \
    /*! Return the number of CLASS_NAME ## s stored in the database.     */ \
    /*! \return         The number of CLASS_NAME ## s.                   */ \
    int num_ ## NAME ## s() const                                           \
    {                                                                       \
        return mapper_database_num_ ## NAME ## s(_db);                      \
    }                                                                       \
    /*! Retrieve a record for a registered CLASS_NAME using its unique   */ \
    /*! id.                                                              */ \
    /*! \param id       Unique id identifying the CLASS_NAME to find in  */ \
    /*!                 the database.                                    */ \
    /*! \return         The CLASS_NAME record.                           */ \
    CLASS_NAME NAME(mapper_id id) const                                     \
    {                                                                       \
        return CLASS_NAME(mapper_database_ ## NAME ## _by_id(_db, id));     \
    }                                                                       \
    /*! Construct a Query from all CLASS_NAME ## s.                      */ \
    /*! \return         A CLASS_NAME ## ::Query containing records of    */ \
    /*!                 all known CLASS_NAME ## s.                       */ \
    QNAME NAME ## s() const                                                 \
    {                                                                       \
        return QNAME(mapper_database_ ## NAME ## s(_db));                   \
    }                                                                       \
    /*! Construct a Query from all CLASS_NAME ## s matching a Property.  */ \
    /*! \param p        Property to match.                               */ \
    /*! \param op       The comparison operator.                         */ \
    /*! \return         A CLASS_NAME ## ::Query containing records of    */ \
    /*!                 CLASS_NAME ## s matching the criteria.           */ \
    QNAME NAME ## s(const Property& p, mapper_op op) const                  \
    {                                                                       \
        return QNAME(                                                       \
            mapper_database_ ## NAME ## s_by_property(_db, p.name, p.length,\
                                                      p.type, p.value, op));\
    }                                                                       \
    /*! Construct a Query from all CLASS_NAME ## s possessing a certain  */ \
    /*! Property.                                                        */ \
    /*! \param p        Property to match.                               */ \
    /*! \return         A CLASS_NAME ## ::Query containing records of    */ \
    /*!                 CLASS_NAME ## s matching the criteria.           */ \
    inline QNAME NAME ## s(const Property& p) const                         \
    {                                                                       \
        return NAME ## s(p, MAPPER_OP_EXISTS);                              \
    }                                                                       \

namespace mapper {

    class Query;
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

    /*! Networks handle multicast and peer-to-peer networking between libmapper
     Devices and Databases.  In general, you do not need to worry about this
     interface, as a Network structure will be created automatically when
     allocating a Device.  A Network structure only needs to be explicitly
     created if you plan to override the default settings. */
    class Network
    {
    public:
        /*! Create a Network with custom parameters.  Creating a Network object manually is only required if you wish to specify custom network
         *  parameters.  Creating a Device without specifying a Network will
         *  give you an object working on the "standard" configuration.
         * \param interface If non-zero, a string identifying a preferred network
         *                  interface.  This string can be enumerated e.g. using
         *                  if_nameindex(). If zero, an interface will be selected
         *                  automatically.
         * \param group     If non-zero, specify a multicast group address to use.
         *                  Zero indicates that the standard group 224.0.1.3 should
         *                  be used.
         * \param port      If non-zero, specify a multicast port to use.  Zero
         *                  indicates that the standard port 7570 should be used.
         * \return          A newly allocated Network object. */
        Network(const string_type &interface=0, const string_type &group=0,
                int port=0)
            { _net = mapper_network_new(interface, group, port); _owned = true; }

        /*! Destructor. */
        ~Network()
            { if (_owned && _net) mapper_network_free(_net); }

        operator mapper_network() const
            { return _net; }

        /*! Return a string indicating the name of the network interface in use.
         *  \return     A string containing the name of the network interface. */
        std::string interface() const
        {
            const char *iface = mapper_network_interface(_net);
            return iface ? std::string(iface) : 0;
        }

        /*! Return the IPv4 address used by a Network.
         *  \return     A pointer to an in_addr struct indicating the network's
         *              IP address, or zero if it is not available.  In general
         *              this will be the IPv4 address associated with the
         *              selected local network interface. */
        const struct in_addr *ip4() const
            { return mapper_network_ip4(_net); }

        /*! Retrieve the name of the multicast group currently in use.
         *  \return     A string specifying the multicast group used by this
         *              Network for bus communication. */
        std::string group() const
            { return std::string(mapper_network_group(_net)); }

        /*! Retrieve the name of the multicast port currently in use.
         *  \return     The port number used by this Network. */
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

    /*! libmapper primarily uses NTP timetags for communication and
     *  synchronization. */
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

    class Query
    {
    public:
        Query(mapper_object_type type, void** query)
            { _type = type; _query = query; }
        mapper_object_type type() const
            { return _type; }
    private:
        mapper_object_type _type;
        void** _query;
    };

    class Object
    {
    protected:
        friend class Property;
        virtual Object& set_property(Property *p) = 0;
    public:
        virtual ~Object() {};
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

    /*! Maps define dataflow connections between sets of Signals. A Map consists
     *  of one or more source Slots, one or more destination Slot (currently),
     *  restricted to one) and properties which determine how the source data is
     *  processed.*/
    class Map : public Object
    {
    public:
        Map(const Map& orig)
            { _map = orig._map; }
        Map(mapper_map map)
            { _map = map; }

        /*! Create a map between a pair of Signals.
         *  \param source       Source Signal.
         *  \param destination  Destination Signal object.
         *  \return             A new Map object – either loaded from the
         *                      Database (if the Map already existed) or newly
         *                      created. In the latter case the Map will not
         *                      take effect until it has been added to the
         *                      network using push(). */
        Map(signal_type source, signal_type destination)
        {
            mapper_signal cast_src = source, cast_dst = destination;
            _map = mapper_map_new(1, &cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of Signals.
         *  \param num_sources  The number of source signals in this map.
         *  \param sources      Array of source Signal objects.
         *  \param num_destinations  The number of destination signals in this map.
         *                      Currently restricted to 1.
         *  \param destinations Array of destination Signal objects.
         *  \return             A new Map object – either loaded from the
         *                      Database (if the Map already existed) or newly
         *                      created. In the latter case the Map will not
         *                      take effect until it has been added to the
         *                      network using push(). */
        Map(int num_sources, signal_type sources[],
            int num_destinations, signal_type destinations[])
        {
            mapper_signal cast_src[num_sources], cast_dst = destinations[0];
            for (int i = 0; i < num_sources; i++) {
                cast_src[i] = sources[i];
            }
            _map = mapper_map_new(num_sources, cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of Signals.
         *  \param sources      std::array of source Signal objects.
         *  \param destinations std::array of destination Signal objects.
         *  \return             A new Map object – either loaded from the
         *                      Database (if the Map already existed) or newly
         *                      created. In the latter case the Map will not
         *                      take effect until it has been added to the
         *                      network using push(). */
        template <size_t N, size_t M>
        Map(std::array<signal_type, N>& sources,
            std::array<signal_type, M>& destinations)
        {
            if (sources.empty() || destinations.empty() || M != 1) {
                _map = 0;
                return;
            }
            mapper_signal cast_src[N], cast_dst = destinations.data()[0];
            for (int i = 0; i < N; i++) {
                cast_src[i] = sources.data()[i];
            }
            _map = mapper_map_new(N, cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of Signals.
         *  \param sources      std::vector of source Signal objects.
         *  \param destinations std::vector of destination Signal objects.
         *  \return             A new Map object – either loaded from the
         *                      Database (if the Map already existed) or newly
         *                      created. In the latter case the Map will not
         *                      take effect until it has been added to the
         *                      network using push(). */
        template <typename T>
        Map(std::vector<T>& sources, std::vector<T>& destinations)
        {
            if (!sources.size() || (destinations.size() != 1)) {
                _map = 0;
                return;
            }
            int num_sources = sources.size();
            mapper_signal cast_src[num_sources], cast_dst = destinations.data()[0];
            for (int i = 0; i < num_sources; i++) {
                cast_src[i] = sources.data()[i];
            }
            _map = mapper_map_new(num_sources, cast_src, 1, &cast_dst);
        }

        /*! Return C data structure mapper_map corresponding to this object.
         *  \return     C mapper_map data structure. */
        operator mapper_map() const
            { return _map; }

        /*! Cast to a boolean value based on whether the underlying C map
         *  exists.
         *  \return     True if mapper_map exists, otherwise false. */
        operator bool() const
            { return _map; }

        /*! Return the unique id assigned to this Map.
         *  \return     The unique id assigned to this Map. */
        operator mapper_id() const
            { return mapper_map_id(_map); }

        /*! Push "staged" property changes out to the network.
         *  \return     Self. */
        const Map& push() const
            { mapper_map_push(_map); return (*this); }

        /*! Re-create stale map if necessary.
         *  \return     Self. */
        const Map& refresh() const
            { mapper_map_refresh(_map); return (*this); }

        /*! Release the Map between a set of Signals. */
        // this function can be const since it only sends the unmap msg
        void release() const
            { mapper_map_release(_map); }

        /*! Get the description property for this Map.
         *  \return     The map description property. */
        std::string description()
            { return std::string(mapper_map_description(_map)); }

        /*! Set the description property for this Map. Changes to remote Maps
         *  will not take effect until synchronized with the network using
         *  push().
         *  \param description  The description value to set.
         *  \return             Self. */
        Map& set_description(string_type &description)
            { mapper_map_set_description(_map, description); return (*this); }

        /*! Get the number of Signals/Slots for this Map.
         *  \param loc      MAPPER_LOC_SOURCE for source slots,
         *                  MAPPER_LOC_DESTINATION for destination slots, or
         *                  MAPPER_LOC_ANY (default) for all slots.
         *  \return     The number of slots. */
        int num_slots(mapper_location loc=MAPPER_LOC_ANY) const
            { return mapper_map_num_slots(_map, loc); }

        /*! Detect whether a Map is completely initialized.
         *  \return     True if map is completely initialized, false otherwise. */
        bool ready() const
            { return mapper_map_ready(_map); }

        /*! Get the mode property for this Map.
         *  \return     The mode property for this Map. */
        mapper_mode mode() const
            { return mapper_map_mode(_map); }

        /*! Set the mode property for this Map. Changes to remote maps will not
         *  take effect until synchronized with the network using push().
         *  \param mode The mode value to set, can be MAPPER_MODE_EXPRESSION,
         *              or MAPPER_MODE_LINEAR.
         *  \return     Self. */
        Map& set_mode(mapper_mode mode)
            { mapper_map_set_mode(_map, mode); return (*this); }

        /*! Get the expression property for a this Map.
         *  \return     The expression property for this Map. */
        const char* expression() const
            { return mapper_map_expression(_map); }

        /*! Set the expression property for this Map. Changes to remote maps
         *  will not take effect until synchronized with the network using
         *  push().
         *  \param expression   A string specifying an expression to be
         *                      evaluated by this Map.
         *  \return             Self. */
        Map& set_expression(const string_type &expression)
            { mapper_map_set_expression(_map, expression); return (*this); }

        /*! Get the muted property for this Map.
         *  \return     True if this Map is muted, false otherwise. */
        bool muted() const
            { return mapper_map_muted(_map); }

        /*! Set the muted property for this Map. Changes to remote maps will not
         *  take effect until synchronized with the network using push().
         *  \param muted    True to mute this map, or false unmute.
         *  \return         Self. */
        Map& set_muted(bool muted)
            { mapper_map_set_muted(_map, (int)muted); return (*this); }

        /*! Get the process location for a this Map.
         *  \return     MAPPER_LOC_SOURCE if processing is evaluated at the
         *              source device, MAPPER_LOC_DESTINATION otherwise. */
        mapper_location process_location() const
            { return mapper_map_process_location(_map); }

        /*! Set the process location property for this Map. Depending on the Map
         *  topology and expression specified it may not be possible to set the
         *  process location to MAPPER_LOC_SOURCE for all maps.  E.g. for
         *  "convergent" topologies or for processing expressions that use
         *  historical values of the destination signal, libmapper will force
         *  processing to take place at the destination device.  Changes to
         *  remote maps will not take effect until synchronized with the network
         *  using push().
         *  \param location MAPPER_LOC_SOURCE to indicate processing should be
         *                  handled by the source Device, or
         *                  MAPPER_LOC_DESTINATION for the destination.
         *  \return         Self. */
        Map& set_process_location(mapper_location location)
            { mapper_map_set_process_location(_map, location); return (*this); }

        /*! Return the unique id assigned to this Map.
         *  \return     The unique id assigned to this Map. */
        mapper_id id() const
            { return mapper_map_id(_map); }

        /*! Indicate whether this is a local Map.
         *  \return     True if the Map is local, false otherwise. */
        bool is_local()
            { return mapper_map_is_local(_map); }

        /*! Get the scopes property for a this map.
         *  \return     A Device::Query containing the list of results.  Use
         *              Device::Query::next() to iterate. */
        mapper::Query scopes() const
            { return mapper::Query(MAPPER_OBJ_DEVICES, (void**)mapper_map_scopes(_map)); }

        /*! Add a scope to this map. Map scopes configure the propagation of
         *  Signal instance updates across the Map. Changes to remote Maps will
         *  not take effect until synchronized with the network using push().
         *  \param dev      Device to add as a scope for this Map. After taking
         *                  effect, this setting will cause instance updates
         *                  originating from the specified Device to be
         *                  propagated across the Map.
         *  \return         Self. */
        inline Map& add_scope(Device dev);

        /*! Remove a scope from this Map. Map scopes configure the propagation
         *  of Signal instance updates across the Map. Changes to remote Maps
         *  will not take effect until synchronized with the network using
         *  push().
         *  \param dev      Device to remove as a scope for this Map. After
         *                  taking effect, this setting will cause instance
         *                  updates originating from the specified Device to be
         *                  blocked from propagating across the Map.
         *  \return         Self. */
        inline Map& remove_scope(Device dev);

        /*! Retrieve the arbitrary pointer associated with this Map.
         *  \return             A pointer associated with this Map. */
        void *user_data() const
            { return mapper_map_user_data(_map); }

        /*! Associate this Map with an arbitrary pointer.
         *  \param user_data    A pointer to user data to be associated.
         *  \return             Self. */
        Map& set_user_data(void *user_data)
            { mapper_map_set_user_data(_map, user_data); return (*this); }

        /*! Query objects provide a lazily-computed iterable list of results
         *  from running queries against Databases, Devices, or Signals. */
        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            QUERY_METHODS(Map, map);

            // also enable some Map methods
            /*! Release each Map in the Query.
             *  \return             Self. */
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
            /*! Set the expression property for each Map in the Query. Changes
             *  to remote maps will not take effect until synchronized with the
             *  network using push().
             *  \param expression   A string specifying an expression to be
             *                      evaluated by this Map.
             *  \return             Self. */
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
            /*! Set the mode property for each Map in the Query. Changes to
             *  remote maps will not take effect until synchronized with the
             *  network using push().
             *  \param mode The mode value to set, can be MAPPER_MODE_EXPRESSION,
             *              or MAPPER_MODE_LINEAR.
             *  \return     Self. */
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
        /*! Slots define the endpoints of a Map.  Each Slot links to a Signal
         *  object and handles properties of the Map that are specific to an
         *  endpoint such as range extrema. */
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

            /*! Retrieve the parent signal for this Slot.
             *  \return     This Slot's parent signal. */
            inline Signal signal() const;

            /*! Get the boundary maximum property for this Slot. This property
             *  controls behaviour when a value exceeds the specified maximum
             *  value.
             *  \return     The boundary maximum setting. */
            mapper_boundary_action bound_max() const
                { return mapper_slot_bound_max(_slot); }

            /*! Set the boundary maximum property for this Slot. This property
             *  controls behaviour when a value exceeds the specified maximum
             *  value.  Changes to remote maps will not take effect until
             *  synchronized with the network using push().
             *  \param action   The boundary maximum setting.
             *  \return         Self. */
            Slot& set_bound_max(mapper_boundary_action action)
                { mapper_slot_set_bound_max(_slot, action); return (*this); }

            /*! Get the boundary minimum property for this Slot. This property
             *  controls behaviour when a value is less than the specified
             *  minimum value.
             *  \return     The boundary minimum setting. */
            mapper_boundary_action bound_min() const
                { return mapper_slot_bound_min(_slot); }

            /*! Set the boundary minimum property for this Slot. This property
             *  controls behaviour when a value is less than the specified
             *  minimum value.  Changes to remote maps will not take effect
             *  until synchronized with the network using push().
             *  \param action   The boundary minimum setting.
             *  \return         Self. */
            Slot& set_bound_min(mapper_boundary_action action)
                { mapper_slot_set_bound_min(_slot, action); return (*this); }

            /*! Retrieve the calibrating property for this Slot. When enabled,
             *  the Slot minimum and maximum values will be updated based on
             *  incoming data.
             *  \return     True if calibration is enabled, false otherwise. */
            bool calibrating() const
                { return mapper_slot_calibrating(_slot); }

            /*! Set the calibrating property for this Slot. When enabled, the
             *  Slot minimum and maximum values will be updated based on
             *  incoming data.  Changes to remote Maps will not take effect
             *  until synchronized with the network using push().
             *  \param calibrating  True to enable calibration, false otherwise.
             *  \return             Self. */
            Slot& set_calibrating(bool calibrating)
            {
                mapper_slot_set_calibrating(_slot, (int)calibrating);
                return (*this);
            }

            /*! Get the "causes update" property for this Slot. When enabled,
             *  updates will cause computation of a new Map output.
             *  \return     True if causes map update, false otherwise. */
            bool causes_update() const
                { return mapper_slot_causes_update(_slot); }

            /*! Set the "causes update" property for this Slot. When enabled,
             *  updates will cause computation of a new Map output.
             *  Changes to remote Maps will not take effect until synchronized
             *  with the network using push().
             *  \param causes_update    True to enable "causes update", false
             *                          otherwise.
             *  \return                 Self. */
            Slot& set_causes_update(bool causes_update)
            {
                mapper_slot_set_causes_update(_slot, (int)causes_update);
                return (*this);
            }

            /*! Retrieve the index for this Slot.
             *  \return     The index of this Slot in its parent Map. */
            int index() const
                { return mapper_slot_index(_slot); }

            /*! Retrieve the value of the "maximum" property for this Slot.
             *  \return     A Property object specifying the maximum. */
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

            /*! Set the "maximum" property for this Slot.  Changes to remote
             *  Maps will not take effect until synchronized with the network
             *  using push().
             *  \param value        A Property object specifying the maximum.
             *  \return             Self. */
            Slot& set_maximum(const Property &value)
            {
                mapper_slot_set_maximum(_slot, value.length, value.type,
                                        (void*)(const void*)value);
                return (*this);
            }

            /*! retrieve the value of the "minimum" property for this Slot.
             *  \return     A Property object specifying the minimum. */
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

            /*! Set the "minimum" property for this Slot.  Changes to remote
             *  Maps will not take effect until synchronized with the network
             *  using push().
             *  \param value        A Property object specifying the minimum.
             *  \return             Self. */
            Slot& set_minimum(const Property &value)
            {
                mapper_slot_set_minimum(_slot, value.length, value.type,
                                        (void*)(const void*)value);
                return (*this);
            }

            /*! Retrieve the value of the "use instances" property for this
             *  Slot. If enabled, updates to this Slot will be treated as
             *  updates to a specific instance.
             *  \return     True if using instance, false otherwise. */
            bool use_instances() const
                { return mapper_slot_use_instances(_slot); }

            /*! Set the "use instances" property for this Slot.  If enabled,
             *  updates to this slot will be treated as updates to a specific
             *  instance.  Changes to remote Maps will not take effect until
             *  synchronized with the network using push().
             *  \param use_instances    True to use instance updates, false
             *                          otherwise.
             *  \return                 Self. */
            Slot& set_use_instances(bool use_instances)
            {
                mapper_slot_set_use_instances(_slot, (int)use_instances);
                return (*this);
            }
            PROPERTY_METHODS(Slot, slot, _slot);
        protected:
            friend class Map;
        private:
            mapper_slot _slot;
        };

        /*! Get the Map Slot matching a specific Signal.
         *  \param sig          The Signal corresponding to the desired slot.
         *  \return         	The Slot. */
        Slot slot(signal_type sig)
            { return Slot(mapper_map_slot_by_signal(_map, (mapper_signal)sig)); }

        /*! Retrieve a destination Slot from this Map.
         *  \param index        The slot index.
         *  \return             The Slot object. */
        Slot destination(int index = 0) const
            { return Slot(mapper_map_slot(_map, MAPPER_LOC_DESTINATION, index)); }

        /*! Retrieve a source Slot from this Map.
         *  \param index        The slot index.
         *  \return             The Slot object. */
        Slot source(int index=0) const
            { return Slot(mapper_map_slot(_map, MAPPER_LOC_SOURCE, index)); }

        PROPERTY_METHODS(Map, map, _map);
    protected:
        friend class Database;
    private:
        mapper_map _map;
    };

    /*! Links define network connections between pairs of devices. */
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
        /*! Query objects provide a lazily-computed iterable list of results
         *  from running queries against Databases or Devices. */
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

    /*! Signals define inputs or outputs for Devices.  A Signal consists of a
     *  scalar or vector value of some integer or floating-point type.  A
     *  Signal is created by adding an input or output to a Device.  It
     *  can optionally be provided with some metadata such as a range, unit, or
     *  other properties.  Signals can be mapped by creating Maps using remote
     *  requests on the network, usually generated by a standalone GUI. */
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

        /*! Indicate whether this is a local Signal.
         *  \return     True if the Signal is local, false otherwise. */
        bool is_local()
            { return mapper_signal_is_local(_sig); }

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
        Signal& update(T value, Timetag tt=0)
            { return update(&value, 1, tt); }
        template <typename T>
        Signal& update(T* value, Timetag tt=0)
            { return update(value, 1, tt); }
        template <typename T, int count>
        Signal& update(T* value, Timetag tt=0)
            { return update(value, count, tt); }
        template <typename T, size_t N>
        Signal& update(std::array<T,N> value, Timetag tt=0)
            { return update(&value[0], N / mapper_signal_length(_sig), tt); }
        template <typename T>
        Signal& update(std::vector<T> value, Timetag tt=0)
        {
            return update(&value[0],
                          (int)value.size() / mapper_signal_length(_sig), tt);
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
            Instance& update(T value, Timetag tt=0)
                { return update(&value, 1, tt); }
            template <typename T>
            Instance& update(T* value, int count=0, Timetag tt=0)
                { return update(value, count, tt); }
            template <typename T>
            Instance& update(T* value, Timetag tt=0)
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

        /*! Query objects provide a lazily-computed iterable list of results
         *  from running queries against Databases or Devices. */
        class Query : public std::iterator<std::input_iterator_tag, int>
        {
        public:
            QUERY_METHODS(Signal, signal);
        private:
            mapper_signal *_query;
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
         *  \param net          A previously allocated Network object to use.
         *  \return             A newly allocated Device. */
        Device(const string_type &name_prefix, int port, const Network& net)
        {
            _dev = mapper_device_new(name_prefix, port, net);
            _db = mapper_device_database(_dev);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }

        /*! Allocate and initialize a Device.
         *  \param name_prefix  A short descriptive string to identify the Device.
         *                      Must not contain spaces or the slash character '/'.
         *  \return             A newly allocated Device. */
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
        /*! Indicate whether this is a local Device.
         *  \return     True if the Device is local, false otherwise. */
        bool is_local()
            { return mapper_device_is_local(_dev); }
        int port() const
            { return mapper_device_port(_dev); }
        int ordinal() const
            { return mapper_device_ordinal(_dev); }
        Timetag start_queue(Timetag tt)
            { mapper_device_start_queue(_dev, *tt); return tt; }
        Timetag start_queue()
        {
            mapper_timetag_t tt;
            mapper_timetag_now(&tt);
            mapper_device_start_queue(_dev, tt);
            return tt;
        }
        Device& send_queue(Timetag tt)
            { mapper_device_send_queue(_dev, *tt); return (*this); }
//        lo::Server lo_server()
//            { return lo::Server(mapper_device_lo_server(_dev)); }

        /*! Query objects provide a lazily-computed iterable list of results
         *  from running queries against a mapper::Database. */
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

    /*! Databases are the primary interface through which a program may observe
     *  the network and store information about Devices and Signals that are
     *  present.  Each Database stores records of Devices, Signals, Links, and
     *  Maps, which can be queried. */
    class Database
    {
    public:
        /*! Create a peer in the libmapper distributed database.
         *  \param net      A previously allocated Network object to use.
         *  \param flags    Sets whether the database should automatically
         *                  subscribe to information about Links, Signals
         *                  and Maps when it encounters a previously-unseen
         *                  Device.
         *  \return         The new Database. */
        Database(Network& net, int flags = MAPPER_OBJ_ALL)
        {
            _db = mapper_database_new(net, flags);
            _owned = true;
            _refcount_ptr = (int*)malloc(sizeof(int));
            *_refcount_ptr = 1;
        }
        /*! Create a peer in the libmapper distributed database.
         *  \param flags    Sets whether the database should automatically
         *                  subscribe to information about Links, Signals
         *                  and Maps when it encounters a previously-unseen
         *                  Device.
         *  \return         The new Database. */
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

        /*! Retrieve the Network object from a Database.
         *  \return         	The database Network object. */
        Network network() const
            { return Network(mapper_database_network(_db)); }

        /*! Update a Database.
         *  \param block_ms     The number of milliseconds to block, or 0 for
         *                      non-blocking behaviour.
         *  \return             The number of handled messages. */
        int poll(int block_ms=0) const
            { return mapper_database_poll(_db, block_ms); }

        /*! Remove unresponsive Devices from the Database.
         *  \return         Self. */
        const Database& flush() const
        {
            mapper_database_flush(_db, mapper_database_timeout(_db), 0);
            return (*this);
        }
        /*! Remove unresponsive Devices from the Database.
         *  \param timeout_sec  The number of second a Device must have been
         *                      unresponsive before removal.
         *  \param quiet        True to disable callbacks during flush(),
         *                      false otherwise.
         *  \return             Self. */
        const Database& flush(int timeout_sec, bool quiet=false) const
        {
            mapper_database_flush(_db, timeout_sec, quiet);
            return (*this);
        }

        // subscriptions
        /*! Send a request to the network for all active Devices to report in.
         *  \return         Self. */
        const Database& request_devices() const
            { mapper_database_request_devices(_db); return (*this); }

        /*! Subscribe to information about a specific Device.
         *  \param dev      The Device of interest.
         *  \param flags    Bitflags setting the type of information of interest.
         *                  Can be a combination of MAPPER_OBJ_DEVICE,
         *                  MAPPER_OBJ_INPUT_SIGNALS, MAPPER_OBJ_OUTPUT_SIGNALS,
         *                  MAPPER_OBJ_SIGNALS, MAPPER_OBJ_INCOMING_MAPS,
         *                  MAPPER_OBJ_OUTGOING_MAPS, MAPPER_OBJ_MAPS, or simply
         *                  MAPPER_OBJ_ALL for all information.
         *  \param timeout  The desired duration in seconds for this 
         *                  subscription. If set to -1, the database will
         *                  automatically renew the subscription until it is
         *                  freed or this function is called again.
         *  \return         Self. */
        const Database& subscribe(const device_type& dev, int flags, int timeout)
        {
            mapper_database_subscribe(_db, dev, flags, timeout);
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
        const Database& subscribe(int flags)
            { mapper_database_subscribe(_db, 0, flags, -1); return (*this); }

        /*! Unsubscribe from information about a specific Device.
         *  \param dev      The Device of interest.
         *  \return         Self. */
        const Database& unsubscribe(const device_type& dev)
        {
            mapper_database_unsubscribe(_db, dev);
            return (*this);
        }

        /*! Cancel all subscriptions.
         *  \return         Self. */
        const Database& unsubscribe()
            { mapper_database_unsubscribe(_db, 0); return (*this); }

        /*! Retrieve the timeout in seconds after which a Database will declare
         *  a Device "unresponsive". Defaults to MAPPER_TIMEOUT_SEC.
         *  \return         The current timeout in seconds. */
        int timeout()
            { return mapper_database_timeout(_db); }

        /*! Set the timeout in seconds after which a Database will declare a
         *  Device "unresponsive".  Defaults to MAPPER_TIMEOUT_SEC.
         *  \param timeout  The timeout in seconds.
         *  \return         Self. */
        Database& set_timeout(int timeout)
            { mapper_database_set_timeout(_db, timeout); return (*this); }

        // database devices
        DATABASE_METHODS(Device, device, Device::Query);

        /*! Retrieve a record for a registered Device with a specific name.
         *  \param name         Name of the Device to find in the database.
         *  \return             The Device record. */
        Device device(const string_type &name) const
            { return Device(mapper_database_device_by_name(_db, name)); }

        /*! Construct a Query from all Devices matching a string pattern.
         *  \param name         Pattern to match against Device names.
         *  \return             A Device::Query containing records of Devices
         *                      matching the criteria. */
        Device::Query devices(const string_type &name) const
            { return Device::Query(mapper_database_devices_by_name(_db, name)); }

        // database signals
        /*! Register a callback for when a Signal record is added or
         *  updated in the Database.
         *  \param h        Callback function.
         *  \param user     A user-defined pointer to be passed to the
         *                  callback for context.
         *  \return         Self. */
        const Database& add_signal_callback(mapper_database_signal_handler *h,
                                            void *user) const
        {
            mapper_database_add_signal_callback(_db, h, user);
            return (*this);
        }

        /*! Remove a Signal record callback from the Database service.
         *  \param h        Callback function.
         *  \param user     The user context pointer that was originally
         *                  specified when adding the callback
         *  \return         Self. */
        const Database& remove_signal_callback(mapper_database_signal_handler *h,
                                               void *user) const
        {
            mapper_database_remove_signal_callback(_db, h, user);
            return (*this);
        }

        /*! Return the number of Signals stored in the database.
         *  \param dir      Signal direction.
         *  \return         The number of Signals. */
        int num_signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return mapper_database_num_signals(_db, dir); }

        /*! Retrieve a record for a registered Signal using its unique id.
         *  \param id       Unique id identifying the CLASS_NAME to find in
         *                  the database.
         *  \return         The CLASS_NAME record. */
        Signal signal(mapper_id id) const
            { return Signal(mapper_database_signal_by_id(_db, id)); }

        /*! Construct a Query from Signal record matching a direction.
         *  \param dir      Signal direction.
         *  \return         A Signal::Query containing the results.*/
        Signal::Query signals(mapper_direction dir=MAPPER_DIR_ANY) const
            { return Signal::Query(mapper_database_signals(_db, dir)); }

        /*! Construct a Query from all Signals with names matching a string
         *  pattern.
         *  \param name         Pattern to match against Signal names.
         *  \return             A Signal::Query containing records of Signals
         *                      matching the criteria. */
        Signal::Query signals(const string_type &name) const
        {
            return Signal::Query(mapper_database_signals_by_name(_db, name));
        }

        /*! Construct a Query from all Signals matching a Property.
         *  \param p        Property to match.
         *  \param op       The comparison operator.
         *  \return         A Signal::Query containing records of
         *                  Signals matching the criteria. */
        Signal::Query signals(const Property& p, mapper_op op) const
        {
            return Signal::Query(
                mapper_database_signals_by_property(_db, p.name, p.length,
                                                    p.type, p.value, op));
        }

        /*! Construct a Query from all Signals possessing a certain
         *  Property.
         *  \param p        Property to match.
         *  \return         A Signal::Query containing records of
         *                  Signals matching the criteria. */
        inline Signal::Query signals(const Property& p) const
            { return signals(p, MAPPER_OP_EXISTS); }

        // database links
        DATABASE_METHODS(Link, link, Link::Query);

        // database maps
        DATABASE_METHODS(Map, map, Map::Query);

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

    Map& Map::add_scope(Device dev)
        { mapper_map_add_scope(_map, mapper_device(dev)); return (*this); }

    Map& Map::remove_scope(Device dev)
        { mapper_map_remove_scope(_map, mapper_device(dev)); return (*this); }

    Signal Map::Slot::signal() const
        { return Signal(mapper_slot_signal(_slot)); }

    Device Signal::device() const
        { return Device(mapper_signal_device(_sig)); }

    inline std::string version()
        { return std::string(mapper_version()); }
};

#endif // _MAPPER_CPP_H_
