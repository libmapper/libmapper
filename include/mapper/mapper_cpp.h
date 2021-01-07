
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

#ifdef interface
#undef interface
#endif

#define RETURN_SELF(FUNC)   \
{ FUNC; return (*this); }

#define MPR_TYPE(NAME) mpr_ ## NAME
#define MPR_FUNC(OBJ, FUNC) mpr_ ## OBJ ## _ ## FUNC

#define OBJ_METHODS(CLASS_NAME)                                             \
public:                                                                     \
    void set_property(const Property& p)                                    \
        { Object::set_property(p); }                                        \
    template <typename... Values>                                           \
    CLASS_NAME& set_property(const Values... vals)                          \
        { RETURN_SELF(Object::set_property(vals...)); }                     \
    template <typename T>                                                   \
    CLASS_NAME& remove_property(const T prop)                               \
        { RETURN_SELF(Object::remove_property(prop)); }                     \
    const CLASS_NAME& push() const                                          \
        { RETURN_SELF(Object::push()); }                                    \

namespace mapper {

    class Object;
    class Device;
    class Signal;
    class Map;
    class Property;
    class Graph;

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
        Time(mpr_time time)
            { _time.sec = time.sec; _time.frac = time.frac; }
        Time(unsigned long int sec, unsigned long int frac)
            { _time.sec = sec; _time.frac = frac; }
        Time(double seconds)
            { mpr_time_set_dbl(&_time, seconds); }
        Time()
            { mpr_time_set(&_time, MPR_NOW); }
        uint32_t sec()
            { return _time.sec; }
        Time& set_sec(uint32_t sec)
            { RETURN_SELF(_time.sec = sec); }
        uint32_t frac()
            { return _time.frac; }
        Time& set_frac (uint32_t frac)
            { RETURN_SELF(_time.frac = frac); }
        Time& now()
            { RETURN_SELF(mpr_time_set(&_time, MPR_NOW)); }
        operator mpr_time*()
            { return &_time; }
        operator double() const
            { return mpr_time_as_dbl(_time); }
        Time& operator=(Time& time)
            { RETURN_SELF(mpr_time_set(&_time, time._time)); }
        Time& operator=(double d)
            { RETURN_SELF(mpr_time_set_dbl(&_time, d)); }
        Time operator+(Time& addend)
        {
            mpr_time temp;
            mpr_time_set(&temp, _time);
            mpr_time_add(&temp, *(mpr_time*)addend);
            return temp;
        }
        Time operator-(Time& subtrahend)
        {
            mpr_time temp;
            mpr_time_set(&temp, _time);
            mpr_time_sub(&temp, *(mpr_time*)subtrahend);
            return temp;
        }
        Time& operator+=(Time& addend)
            { RETURN_SELF(mpr_time_add(&_time, *(mpr_time*)addend)); }
        Time& operator+=(double addend)
            { RETURN_SELF(mpr_time_add_dbl(&_time, addend)); }
        Time& operator-=(Time& subtrahend)
            { RETURN_SELF(mpr_time_sub(&_time, *(mpr_time*)subtrahend)); }
        Time& operator-=(double subtrahend)
            { RETURN_SELF(mpr_time_add_dbl(&_time, -subtrahend)); }
        Time& operator*=(double multiplicand)
            { RETURN_SELF(mpr_time_mul(&_time, multiplicand)); }
        bool operator<(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) < 0; }
        bool operator<=(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) <= 0; }
        bool operator==(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) == 0; }
        bool operator>=(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) >= 0; }
        bool operator>(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) > 0; }
        bool operator!=(Time& rhs)
            { return mpr_time_cmp(_time, rhs._time) != 0; }
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
        /* override copy constructor */
        List(const List& orig)
            { _list = mpr_list_get_cpy(orig._list); }
        ~List()
            { mpr_list_free(_list); }

        virtual operator std::vector<Object>() const;
        operator mpr_list() { return _list; }

        bool operator==(const List& rhs)
            { return (_list == rhs._list); }
        bool operator!=(const List& rhs)
            { return (_list != rhs._list); }
        List& operator++()
            { RETURN_SELF(if (_list) _list = mpr_list_get_next(_list)); }
        List operator++(int)
            { List tmp(*this); operator++(); return tmp; }
        List begin()
            { return (*this); }
        List end()
            { return List(0); }

        int size()
            { return mpr_list_get_size(_list); }

        /* Combination functions */
        /*! Add items found in List rhs to this List (without duplication).
         *  \param rhs          A second List.
         *  \return             Self. */
        List& join(const List& rhs)
        {
            _list = mpr_list_get_union(_list, mpr_list_get_cpy(rhs._list));
            return (*this);
        }

        /*! Remove items NOT found in List rhs from this List
         *  \param rhs          A second List.
         *  \return             Self. */
        List& intersect(const List& rhs)
        {
            _list = mpr_list_get_isect(_list, mpr_list_get_cpy(rhs._list));
            return (*this);
        }

        /*! Filter items from this List based on property matching
         *  \param p            Property to match.
         *  \param op           The comparison operator.
         *  \return             Self. */
        List& filter(const Property& p, mpr_op op);

        /*! Remove items found in List rhs from this List
         *  \param rhs          A second list.
         *  \return             Self. */
        List& subtract(const List& rhs)
        {
            _list = mpr_list_get_diff(_list, mpr_list_get_cpy(rhs._list));
            return (*this);
        }

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
        {
            _list = mpr_list_get_union(_list, mpr_list_get_cpy(rhs._list));
            return (*this);
        }

        /*! Remove items NOT found in List rhs from this List
         *  \param rhs          A second List.
         *  \return             Self. */
        List& operator*=(const List& rhs)
        {
            _list = mpr_list_get_isect(_list, mpr_list_get_cpy(rhs._list));
            return (*this);
        }

        /*! Remove items found in List rhs from this List
         *  \param rhs          A second List.
         *  \return             Self. */
        List& operator-=(const List& rhs)
        {
            _list = mpr_list_get_diff(_list, mpr_list_get_cpy(rhs._list));
            return (*this);
        }

        /*! Set properties for each Object in the List.
         *  \param vals     The Properties to add of modify.
         *  \return         Self. */
        template <typename... Values>
        List& set_property(const Values... vals);

        T operator*()
            { return T(*_list); }

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
        friend class Property;
        void set_property(const Property& p);

        mpr_obj _obj;
    public:
        Object() { _obj = NULL; _owned = 0; _refcount_ptr = 0; }
        virtual ~Object() {}
        operator mpr_obj() const
            { return _obj; }

        /*! Get the specific type of an Object.
         *  \return         Object type. */
        mpr_type type() const
            { return mpr_obj_get_type(_obj); }

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
        virtual Object& remove_property(mpr_prop prop)
            { RETURN_SELF(mpr_obj_remove_prop(_obj, prop, NULL)); }

        /*! Remove a named Property from an Object.
         *  \param key     Name of the Property to remove.
         *  \return        Self. */
        virtual Object& remove_property(const str_type &key)
        {
            if (key)
                mpr_obj_remove_prop(_obj, MPR_PROP_UNKNOWN, key);
            return (*this);
        }

        /*! Push "staged" property changes out to the distributed graph.
         *  \return         Self. */
        virtual const Object& push() const
            { RETURN_SELF(mpr_obj_push(_obj)); }

        /*! Retrieve the number of Properties owned by an Object.
         *  \param staged   Set to true to count properties that have been
         *                  staged but not synchronized with the graph.
         *  \return         The number of Properties. */
        int num_props(bool staged = false) const
            { return mpr_obj_get_num_props(_obj, staged); }

        /*! Retrieve a Property by name.
         *  \param key      The name of the Property to retrieve.
         *  \return         The retrieved Property. */
        Property property(const str_type &key=NULL) const;

        /*! Retrieve a Property by index.
         *  \param prop     The index of or symbolic identifier of the Property to retrieve.
         *  \return         The retrieved Property. */
        Property property(mpr_prop prop) const;

        template <typename T>
        Property operator [] (T prop) const;
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

    /*! Maps define dataflow connections between sets of Signals. A Map consists
     *  of one or more source Signals, one or more destination Signal (currently),
     *  restricted to one) and properties which determine how the source data is
     *  processed.*/
    class Map : public Object
    {
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
         *  \return     A new Map object – either loaded from the Graph (if the
         *              Map already exists) or newly created. In the latter case
         *              the Map will not take effect until it has been added to
         *              the distributed graph using push(). */
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
         *  \param ...          A sequence of additional Signal arguments, one for each format
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
         *  \param num_dsts The number of destination signals in this map.
         *                  Currently restricted to 1.
         *  \param dsts     Array of destination Signal objects.
         *  \return         A new Map object – either loaded from the Graph (if
         *                  the Map already exists) or newly created. In the
         *                  latter case the Map will not take effect until it
         *                  has been added to the graph using push(). */
        Map(int num_srcs, signal_type srcs[], int num_dsts, signal_type dsts[]) : Object(NULL)
        {
            mpr_sig cast_src[num_srcs], cast_dst = dsts[0];
            for (int i = 0; i < num_srcs; i++)
                cast_src[i] = srcs[i];
            _obj = mpr_map_new(num_srcs, cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of Signals.
         *  \param srcs std::array of source Signal objects.
         *  \param dsts std::array of destination Signal objects.
         *  \return     A new Map object – either loaded from the Graph (if the
         *              Map already exists) or newly created. In the latter case
         *              the Map will not take effect until it has been added to
         *              the distributed graph using push(). */
        template <size_t N, size_t M>
        Map(std::array<signal_type, N>& srcs,
            std::array<signal_type, M>& dsts) : Object(NULL)
        {
            if (srcs.empty() || dsts.empty() || M != 1) {
                _obj = 0;
                return;
            }
            mpr_sig cast_src[N], cast_dst = dsts.data()[0];
            for (int i = 0; i < N; i++)
                cast_src[i] = srcs.data()[i];
            _obj = mpr_map_new(N, cast_src, 1, &cast_dst);
        }

        /*! Create a map between a set of Signals.
         *  \param srcs std::vector of source Signal objects.
         *  \param dsts std::vector of destination Signal objects.
         *  \return     A new Map object – either loaded from the Graph (if the
         *              Map already exists) or newly created. In the latter case
         *              the Map will not take effect until it has been added to
         *              the distributed graph using push(). */
        Map(std::vector<signal_type>& srcs, std::vector<signal_type>& dsts) : Object(NULL)
        {
            if (!srcs.size() || (dsts.size() != 1)) {
                _obj = 0;
                return;
            }
            int num_srcs = srcs.size();
            mpr_sig cast_src[num_srcs], cast_dst = dsts.data()[0];
            for (int i = 0; i < num_srcs; i++)
                cast_src[i] = srcs.data()[i];
            _obj = mpr_map_new(num_srcs, cast_src, 1, &cast_dst);
        }

        /*! Return C data structure mpr_map corresponding to this object.
         *  \return         C mpr_map data structure. */
        operator mpr_map() const
            { return _obj; }

        /*! Cast to a boolean value based on whether the underlying C map exists.
         *  \return         True if mpr_map exists, otherwise false. */
        operator bool() const
            { return _obj; }

        /*! Re-create stale map if necessary.
         *  \return         Self. */
        const Map& refresh() const
            { RETURN_SELF(mpr_map_refresh(_obj)); }

        /*! Release the Map between a set of Signals. */
        // this function can be const since it only sends the unmap msg
        void release() const
            { mpr_map_release(_obj); }

        /*! Detect whether a Map is completely initialized.
         *  \return         True if map is completely initialized. */
        bool ready() const
            { return mpr_map_get_is_ready(_obj); }

//        /*! Get the scopes property for a this map.
//         *  \return       A List containing the list of results.  Use
//         *                List::next() to iterate. */
//        List<Device> scopes() const
//            { return List<Device>((void**)mpr_map_scopes(_obj)); }

        /*! Add a scope to this Map. Map scopes configure the propagation of
         *  Signal updates across the Map. Changes will not take effect until
         *  synchronized with the distributed graph using push().
         *  \param dev      Device to add as a scope for this Map. After taking
         *                  effect, this setting will cause instance updates
         *                  originating from the specified Device to be
         *                  propagated across the Map.
         *  \return         Self. */
        inline Map& add_scope(const Device& dev);

        /*! Remove a scope from this Map. Map scopes configure the propagation
         *  of Signal updates across the Map. Changes will not take effect until
         *  synchronized with the distributed graph using push().
         *  \param dev      Device to remove as a scope for this Map. After
         *                  taking effect, this setting will cause instance
         *                  updates originating from the specified Device to be
         *                  blocked from propagating across the Map.
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
        List<Signal> signals(mpr_loc loc=MPR_LOC_ANY) const
            { return List<Signal>(mpr_map_get_sigs(_obj, loc)); }

        OBJ_METHODS(Map);

    protected:
        friend class Graph;
    };

    /*! Signals define inputs or outputs for Devices.  A Signal consists of a
     *  scalar or vector value of some integer or floating-point type.  A Signal
     *  is created by adding an input or output to a Device.  It can optionally
     *  be provided with some metadata such as a range, unit, or other
     *  properties.  Signals can be mapped by creating Maps using remote
     *  requests on the network, usually generated by a standalone GUI. */
    class Signal : public Object
    {
    public:
        Signal() : Object() {}
        Signal(mpr_sig sig) : Object(sig) {}
        operator mpr_sig() const
            { return _obj; }
        operator bool() const
            { return _obj ? true : false; }
        inline Device device() const;
        List<Map> maps(mpr_dir dir=MPR_DIR_ANY) const
            { return List<Map>(mpr_sig_get_maps(_obj, dir)); }

        /* Value update functions*/
        Signal& set_value(const int *val, int len)
            { RETURN_SELF(mpr_sig_set_value(_obj, 0, len, MPR_INT32, val)); }
        Signal& set_value(const float *val, int len)
            { RETURN_SELF(mpr_sig_set_value(_obj, 0, len, MPR_FLT, val)); }
        Signal& set_value(const double *val, int len)
            { RETURN_SELF(mpr_sig_set_value(_obj, 0, len, MPR_DBL, val)); }
        template <typename T>
        Signal& set_value(T val)
            { return set_value(&val, 1); }
        template <typename T>
        Signal& set_value(const T* val)
            { return set_value(val, 1); }
        template <typename T, int len>
        Signal& set_value(const T* val)
            { return set_value(val, len); }
        template <typename T, size_t N>
        Signal& set_value(std::array<T,N> val)
            { return set_value(&val[0], N); }
        template <typename T>
        Signal& set_value(std::vector<T> val)
            { return set_value(&val[0], (int)val.size()); }
        const void *value() const
            { return mpr_sig_get_value(_obj, 0, 0); }
        const void *value(Time time) const
            { return mpr_sig_get_value(_obj, 0, (mpr_time*)time); }
        Signal& set_callback(mpr_sig_handler *h, int events=MPR_SIG_UPDATE)
            { RETURN_SELF(mpr_sig_set_cb(_obj, h, events)); }

        /*! Signal Instances can be used to describe the multiplicity and/or ephemerality
        of phenomena associated with Signals. A signal describes the phenomena, e.g.
        the position of a 'blob' in computer vision, and the signal's instances will
        describe the positions of actual detected blobs. */
        class Instance {
        public:
            Instance(mpr_sig sig, mpr_id id)
                { _sig = sig; _id = id; }
            bool operator == (Instance i)
                { return (_id == i._id); }
            operator mpr_id() const
                { return _id; }
            int is_active() const
                { return mpr_sig_get_inst_is_active(_sig, _id); }
            Instance& set_value(const int *val, int len)
            {
                mpr_sig_set_value(_sig, _id, len, MPR_INT32, val);
                return (*this);
            }
            Instance& set_value(const float *val, int len)
            {
                mpr_sig_set_value(_sig, _id, len, MPR_FLT, val);
                return (*this);
            }
            Instance& set_value(const double *val, int len)
            {
                mpr_sig_set_value(_sig, _id, len, MPR_DBL, val);
                return (*this);
            }

            void release()
                { mpr_sig_release_inst(_sig, _id); }

            template <typename T>
            Instance& set_value(T val)
                { return set_value(&val, 1); }
            template <typename T>
            Instance& set_value(const T* val)
                { return set_value(val, 1); }
            template <typename T, int len>
            Instance& set_value(const T* val)
                { return set_value(val, len); }
            template <typename T, size_t N>
            Instance& set_value(std::array<T,N> val)
                { return set_value(&val[0], N); }
            template <typename T>
            Instance& set_value(std::vector<T> val)
                { return set_value(&val[0], val.size()); }

            mpr_id id() const
                { return _id; }

            Instance& set_data(void *data)
                { RETURN_SELF(mpr_sig_set_inst_data(_sig, _id, data)); }
            void *data() const
                { return mpr_sig_get_inst_data(_sig, _id); }

            const void *value() const
                { return mpr_sig_get_value(_sig, _id, 0); }
            const void *value(Time time) const
            {
                mpr_time *_time = time;
                return mpr_sig_get_value(_sig, _id, _time);
            }
        protected:
            friend class Signal;
        private:
            mpr_id _id;
            mpr_sig _sig;
        };
        Instance instance()
        {
            mpr_id id = mpr_dev_generate_unique_id(mpr_sig_get_dev(_obj));
            return Instance(_obj, id);
        }
        Instance instance(mpr_id id)
            { return Instance(_obj, id); }
        Signal& reserve_instances(int num, mpr_id *ids = 0)
            { RETURN_SELF(mpr_sig_reserve_inst(_obj, num, ids, 0)); }
        Signal& reserve_instances(int num, mpr_id *ids, void **data)
            { RETURN_SELF(mpr_sig_reserve_inst(_obj, num, ids, data)); }
        Instance instance(int idx, mpr_status status) const
            { return Instance(_obj, mpr_sig_get_inst_id(_obj, idx, status)); }
        Signal& remove_instance(Instance instance)
            { RETURN_SELF(mpr_sig_remove_inst(_obj, instance._id)); }
        Instance oldest_instance()
            { return Instance(_obj, mpr_sig_get_oldest_inst_id(_obj)); }
        Instance newest_instance()
            { return Instance(_obj, mpr_sig_get_newest_inst_id(_obj)); }
        int num_instances(mpr_status status = MPR_STATUS_ACTIVE) const
            { return mpr_sig_get_num_inst(_obj, status); }

        OBJ_METHODS(Signal);
    };

    /*! A Device is an entity on the network which has input and/or output
     *  Signals.  The Device is the primary interface through which a program
     *  uses libmapper.  A Device must have a name, to which a unique ordinal is
     *  subsequently appended.  It can also be given other user-specified
     *  metadata.  Signals can be mapped using local code or messages over the
     *  network, usually sent from an external GUI. */
    class Device : public Object
    {
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
        Device(const Device& orig) : Object(orig)
        {
            _owned = orig._owned;
            _refcount_ptr = orig._refcount_ptr;
            if (_owned)
                incr_refcount();
        }
        Device(mpr_dev dev) : Object(dev)
        {
            _owned = false;
        }
        ~Device()
        {
            if (_owned && _obj && decr_refcount() <= 0) {
                mpr_dev_free(_obj);
                free(_refcount_ptr);
            }
        }
        operator mpr_dev() const
            { return _obj; }

        Signal add_signal(mpr_dir dir, const str_type &name, int len, mpr_type type,
                          const str_type &unit=0, void *min=0, void *max=0,
                          int *num_inst=0, mpr_sig_handler h=0,
                          int events=MPR_SIG_UPDATE)
        {
            return Signal(mpr_sig_new(_obj, dir, name, len, type,
                                      unit, min, max, num_inst, h, events));
        }
        Device& remove_signal(Signal& sig)
            { RETURN_SELF(mpr_sig_free(sig)); }

        List<Signal> signals(mpr_dir dir=MPR_DIR_ANY) const
            { return List<Signal>(mpr_dev_get_sigs(_obj, dir)); }

        List<Map> maps(mpr_dir dir=MPR_DIR_ANY) const
            { return List<Map>(mpr_dev_get_maps(_obj, dir)); }

        int poll(int block_ms=0) const
            { return mpr_dev_poll(_obj, block_ms); }

        bool ready() const
            { return mpr_dev_get_is_ready(_obj); }
        Time get_time()
            { return mpr_dev_get_time(_obj); }
        Device& set_time(Time time)
            { RETURN_SELF(mpr_dev_set_time(_obj, *time)); }
        Device& process_outputs()
            { RETURN_SELF(mpr_dev_process_outputs(_obj)); }

        OBJ_METHODS(Device);
    };

    class device_type {
    public:
        device_type(mpr_dev dev) { _dev = dev; }
        device_type(const Device& dev) { _dev = (mpr_dev)dev; }
        operator mpr_dev() const { return _dev; }
        mpr_dev _dev;
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
        Graph(int flags = MPR_OBJ)
        {
            _graph = mpr_graph_new(flags);
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
        Graph(mpr_graph graph)
        {
            _graph = graph;
            _owned = false;
            _refcount_ptr = 0;
        }
        ~Graph()
        {
            if (_owned && _graph && decr_refcount() <= 0) {
                mpr_graph_free(_graph);
                free(_refcount_ptr);
            }
        }
        operator mpr_graph() const
            { return _graph; }

        /*! Specify the network interface to use.
         *  \param iface        A string specifying the name of the network
         *                      interface to use.
         *  \return             Self. */
        Graph& set_iface(const str_type &iface)
            { RETURN_SELF(mpr_graph_set_interface(_graph, iface)); }

        /*! Return a string indicating the name of the network interface in use.
         *  \return     A string containing the name of the network interface.*/
        std::string iface() const
        {
            const char *iface = mpr_graph_get_interface(_graph);
            return iface ? std::string(iface) : 0;
        }

        /*! Specify the multicast group and port to use.
         *  \param group    A string specifying the multicast group to use.
         *  \param port     The multicast port to use.
         *  \return         Self. */
        Graph& set_address(const str_type &group, int port)
            { RETURN_SELF(mpr_graph_set_address(_graph, group, port)); }

        /*! Retrieve the multicast url currently in use.
         *  \return     A string specifying the multicast url in use. */
        std::string address() const
            { return std::string(mpr_graph_get_address(_graph)); }

        /*! Update a Graph.
         *  \param block_ms     The number of milliseconds to block, or 0 for
         *                      non-blocking behaviour.
         *  \return             The number of handled messages. */
        int poll(int block_ms=0) const
            { return mpr_graph_poll(_graph, block_ms); }

        // subscriptions
        /*! Subscribe to information about a specific Device.
         *  \param dev      The Device of interest.
         *  \param flags    Bitflags setting the type of information of interest.
         *                  Can be a combination of MPR_DEV, MPR_SIG_IN,
         *                  MPR_SIG_OUT, MPR_SIG, MPR_MAP_IN, MPR_MAP_OUT,
         *                  MPR_MAP, or simply MPR_OBJ for all information.
         *  \param timeout  The desired duration in seconds for this
         *                  subscription. If set to -1, the graph will
         *                  automatically renew the subscription until it is
         *                  freed or this function is called again.
         *  \return         Self. */
        const Graph& subscribe(const device_type& dev, int flags, int timeout)
            { RETURN_SELF(mpr_graph_subscribe(_graph, dev, flags, timeout)); }

        /*! Subscribe to information about all discovered Devices.
         *  \param flags    Bitflags setting the type of information of interest.
         *                  Can be a combination of MPR_DEV, MPR_SIG_IN,
         *                  MPR_SIG_OUT, MPR_SIG, MPR_MAP_IN, MPR_MAP_OUT,
         *                  MPR_MAP, or simply MPR_OBJ for all information.
         *  \return         Self. */
        const Graph& subscribe(int flags)
            { RETURN_SELF(mpr_graph_subscribe(_graph, 0, flags, -1)); }

        /*! Unsubscribe from information about a specific Device.
         *  \param dev      The Device of interest.
         *  \return         Self. */
        const Graph& unsubscribe(const device_type& dev)
            { RETURN_SELF(mpr_graph_unsubscribe(_graph, dev)); }

        /*! Cancel all subscriptions.
         *  \return         Self. */
        const Graph& unsubscribe()
            { RETURN_SELF(mpr_graph_unsubscribe(_graph, 0)); }

        // graph signals
        /*! Register a callback for when an Object record is added, updated, or
         *  removed.
         *  \param h        Callback function.
         *  \param types    Bitflags setting the type of information of interest.
         *                  Can be a combination of mpr_type values.
         *  \param data     A user-defined pointer to be passed to the
         *                  callback for context.
         *  \return         Self. */
        const Graph& add_callback(mpr_graph_handler *h, int types, void *data) const
            { RETURN_SELF(mpr_graph_add_cb(_graph, h, types, data)); }

        /*! Remove an Object record callback from the Graph service.
         *  \param h        Callback function.
         *  \param data     The user context pointer that was originally
         *                  specified when adding the callback
         *  \return         Self. */
        const Graph& remove_callback(mpr_graph_handler *h, void *data) const
            { RETURN_SELF(mpr_graph_remove_cb(_graph, h, data)); }

        const Graph& print() const
            { RETURN_SELF(mpr_graph_print(_graph)); }

        // graph devices
        List<Device> devices() const
            { return List<Device>(mpr_graph_get_objs(_graph, MPR_DEV)); }

        // graph signals
        List<Signal> signals() const
            { return List<Signal>(mpr_graph_get_objs(_graph, MPR_SIG)); }

        // graph maps
        List<Map> maps() const
            { return List<Map>(mpr_graph_get_objs(_graph, MPR_MAP)); }

    private:
        mpr_graph _graph;
        int* _refcount_ptr;
        int incr_refcount()
            { return _refcount_ptr ? ++(*_refcount_ptr) : 0; }
        int decr_refcount()
            { return _refcount_ptr ? --(*_refcount_ptr) : 0; }
        bool _owned;
    };

    class Property
    {
    public:
        template <typename T>
        Property(mpr_prop _prop, T _val) : Property(_prop)
            { _set(_val); }
        template <typename T>
        Property(const str_type &_key, T _val) : Property(_key)
            { _set(_val); }
        template <typename T>
        Property(mpr_prop _prop, int _len, T& _val) : Property(_prop)
            { _set(_len, _val); }
        template <typename T>
        Property(const str_type &_key, int _len, T& _val) : Property(_key)
            { _set(_len, _val); }
        template <typename T, size_t N>
        Property(mpr_prop _prop, std::array<T, N> _val) : Property(_prop)
            { _set(_val); }
        template <typename T, size_t N>
        Property(const str_type &_key, std::array<T, N> _val) : Property(_key)
            { _set(_val); }
        template <typename T>
        Property(mpr_prop _prop, std::vector<T> _val) : Property(_prop)
            { _set(_val); }
        template <typename T>
        Property(const str_type &_key, std::vector<T> _val) : Property(_key)
            { _set(_val); }
        template <typename T>
        Property(mpr_prop _prop, int _len, mpr_type _type, T& _val) : Property(_prop)
            { _set(_len, _type, _val); }
        template <typename T>
        Property(const str_type &_key, int _len, mpr_type _type, T& _val) : Property(_key)
            { _set(_len, _type, _val); }

        ~Property()
            { maybe_free(); }

        template <typename T>
        operator const T() const
            { return *(const T*)val; }
        operator const bool() const
        {
            if (!len || !type || !val)
                return false;
            switch (type) {
                case MPR_INT32: return *(int*)val != 0;
                case MPR_FLT:   return *(float*)val != 0.f;
                case MPR_DBL:   return *(double*)val != 0.;
                default:        return val != 0;
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
        operator List<Device>() const
            { return (type == MPR_LIST) ? (mpr_list)val : NULL; }
        operator List<Signal>() const
            { return (type == MPR_LIST) ? (mpr_list)val : NULL; }
        operator List<Map>() const
            { return (type == MPR_LIST) ? (mpr_list)val : NULL; }

        template <typename... Values>
        Property& operator = (Values... vals)
            { RETURN_SELF(_set(vals...)); }

        mpr_prop prop;
        const char *key;
        mpr_type type;
        unsigned int len;
        const void *val;
        bool pub;
    protected:
        friend class Graph;
        friend class Object;
        mpr_obj parent = NULL;

        Property(mpr_prop _prop, const str_type &_key, int _len, mpr_type _type,
                 const void *_val, int _pub)
        {
            prop = _prop;
            key = _key;
            _set(_len, _type, _val);
            owned = false;
            pub = _pub;
        }
        Property(mpr_prop _prop)
        {
            prop = _prop;
            key = NULL;
            val = 0;
            owned = false;
            pub = true;
        }
        Property(const str_type &_key)
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
    };

    void Object::set_property(const Property& p)
    {
        mpr_obj_set_prop(_obj, p.prop, p.key, p.len, p.type, p.val, p.pub);
    }

    template <typename... Values>
    Object& Object::set_property(const Values... vals)
    {
        Property p(vals...);
        set_property(p);
        return (*this);
    }

    Property Object::property(const str_type &key) const
    {
        mpr_prop prop;
        mpr_type type;
        const void *val;
        int len, pub;
        prop = mpr_obj_get_prop_by_key(_obj, key, &len, &type, &val, &pub);
        Property p(prop, key, len, type, val, pub);
        p.parent = _obj;
        return p;
    }

    /*! Retrieve a Property by index.
     *  \param prop     The index or symbolic identifier of the Property to retrieve.
     *  \return         The retrieved Property. */
    Property Object::property(mpr_prop prop) const
    {
        const char *key;
        mpr_type type;
        const void *val;
        int len, pub;
        prop = mpr_obj_get_prop_by_idx(_obj, prop, &key, &len, &type, &val, &pub);
        Property p(prop, key, len, type, val, pub);
        p.parent = _obj;
        return p;
    }

    template <typename T>
    Property Object::operator [] (const T prop) const
        { return property(prop); }

    template <class T>
    List<T>::operator std::vector<Object>() const
    {
        std::vector<Object> vec;
        mpr_list cpy = mpr_list_get_cpy(_list);
        while (cpy) {
            vec.push_back(Object(*cpy));
            cpy = mpr_list_get_next(cpy);
        }
        return vec;
    }

    template <class T>
    List<T>& List<T>::filter(const Property& p, mpr_op op)
    {
        _list = mpr_list_filter(_list, p.prop, p.key, p.len, p.type, p.val, op);
        return (*this);
    }

    template <class T>
    template <typename... Values>
    List<T>& List<T>::set_property(const Values... vals)
    {
        Property p(vals...);
        if (!p)
            return (*this);
        mpr_list cpy = mpr_list_get_cpy(_list);
        while (cpy) {
            mpr_obj_set_prop(*cpy, p.prop, p.key, p.len, p.type, p.val, p.pub);
            cpy = mpr_list_get_next(cpy);
        }
        return (*this);
    }

    Graph Object::graph() const
        { return Graph(mpr_obj_get_graph(_obj)); }

    Device::Device(const str_type &name, const Graph& graph) : Object(NULL)
    {
        _obj = mpr_dev_new(name, graph);
        _owned = true;
        _refcount_ptr = (int*)malloc(sizeof(int));
        *_refcount_ptr = 1;
    }

    signal_type::signal_type(const Signal& sig)
        { _sig = (mpr_sig)sig; }

    Map& Map::add_scope(const Device& dev)
        { RETURN_SELF(mpr_map_add_scope(_obj, mpr_dev(dev))); }

    Map& Map::remove_scope(const Device& dev)
        { RETURN_SELF(mpr_map_remove_scope(_obj, mpr_dev(dev))); }

    Device Signal::device() const
        { return Device(mpr_sig_get_dev(_obj)); }

    inline std::string version()
        { return std::string(mpr_get_version()); }
};

#define OSTREAM_TYPE(TYPE)                  \
if (p.len == 1)                             \
    os << *(TYPE*)p.val;                    \
else if (p.len > 1) {                       \
    os << "[";                              \
    for (unsigned int i = 0; i < p.len; i++)\
        os << ((TYPE*)p.val)[i] << ", ";    \
    os << "\b\b]";                          \
}

std::ostream& operator<<(std::ostream& os, const mapper::Property& p)
{
    if (p.len <= 0 || p.type == MPR_NULL)
        return os << "NULL";
    switch (p.type) {
        case MPR_INT32:     OSTREAM_TYPE(int);      break;
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
        default:
            os << "Property type not handled by ostream operator!";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const mapper::Device& dev)
{
    return os << "<mapper::Device '" << dev[MPR_PROP_NAME] << "'>";
}

std::ostream& operator<<(std::ostream& os, const mapper::Signal& sig)
{
    os << "<mapper::Signal '" << sig.device()[MPR_PROP_NAME] << ":" << sig[MPR_PROP_NAME] << "'>";
    return os;
}

std::ostream& operator<<(std::ostream& os, const mapper::Map& map)
{
    os << "<mapper::Map ";

    // add sources
    os << "[";
    for (const mapper::Signal s : map.signals(MPR_LOC_SRC))
        os << s << ", ";
    os << "\b\b] -> ";

    // add destinations
    os << "[";
    for (const mapper::Signal s : map.signals(MPR_LOC_DST))
        os << s << ", ";
    os << "\b\b]";

    os << ">";
    return os;
}

std::ostream& operator<<(std::ostream& os, const mapper::Object& o)
{
    mpr_obj obj = (mpr_obj)o;
    switch (mpr_obj_get_type(obj)) {
        case MPR_DEV: os << mapper::Device(obj); break;
        case MPR_SIG: os << mapper::Signal(obj); break;
        case MPR_MAP: os << mapper::Map(obj);    break;
        default:                                 break;
    }
    return os;
}

#endif // _MPR_CPP_H_
