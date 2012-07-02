%module mapper
%include "typemaps.i"
%typemap(in) PyObject *PyFunc {
    if ($input!=Py_None && !PyCallable_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "Need a callable object!");
        return NULL;
    }
    $1 = $input;
 }
%typemap(in) maybeSigVal %{
    {
    sigval val;
    if ($input == Py_None)
        $1 = 0;
    else {
        int ecode = SWIG_AsVal_int($input, &val.v.i32);
        if (SWIG_IsOK(ecode))
            val.t = 'i';
        else {
            ecode = SWIG_AsVal_float($input, &val.v.f);
            if (SWIG_IsOK(ecode))
                val.t = 'f';
            else {
                SWIG_exception_fail(SWIG_ArgError(ecode),
                             "argument $argnum of type 'float' or 'int'");
            }
        }
        $1 = &val;
    }
    }
%}
%typemap(out) maybeSigVal {
    if ($1 && $1->t == 'f') {
        $result = Py_BuildValue("f", $1->v.f);
        free($1);
    }
    else if ($1 && $1->t == 'i') {
        $result = Py_BuildValue("i", $1->v.i32);
        free($1);
    }
    else {
        $result = Py_None;
        Py_INCREF($result);
    }
 }
%typemap(out) maybeInt {
    if ($1) {
        $result = Py_BuildValue("i", *$1);
        free($1);
    }
    else {
        $result = Py_None;
        Py_INCREF($result);
    }
 }
%typemap(out) booltype {
    PyObject *o = $1 ? Py_True : Py_False;
    Py_INCREF(o);
    return o;
 }

%typemap(in) mapper_db_connection_with_flags_t* %{
    mapper_db_connection_with_flags_t p;
    $1 = 0;
    if (PyDict_Check($input)) {
        memset(&p, 0, sizeof(mapper_db_connection_with_flags_t));
        PyObject *keys = PyDict_Keys($input);
        if (keys) {
            int i = PyList_GET_SIZE(keys), k;
            for (i=i-1; i>=0; --i) {
                PyObject *o = PyList_GetItem(keys, i);
                if (PyString_Check(o)) {
                    PyObject *v = PyDict_GetItem($input, o);
                    char *s = PyString_AsString(o);
                    if (strcmp(s, "clip_max")==0) {
                        int ecode = SWIG_AsVal_int(v, &k);
                        if (SWIG_IsOK(ecode)) {
                            p.props.clip_max = k;
                            p.flags |= CONNECTION_CLIPMAX;
                        }
                    }
                    else if (strcmp(s, "clip_min")==0) {
                        int ecode = SWIG_AsVal_int(v, &k);
                        if (SWIG_IsOK(ecode)) {
                            p.props.clip_min = k;
                            p.flags |= CONNECTION_CLIPMIN;
                        }
                    }
                    else if (strcmp(s, "range")==0) {
                        if (PySequence_Check(v)) {
                            int len = PySequence_Size(v), j, n;
                            float *f;
                            for (j=0; j<len && j<4; j++) {
                                PyObject *r = PySequence_GetItem(v, j);
                                switch (j) {
                                case 0: f = &p.props.range.src_min;
                                    k = CONNECTION_RANGE_SRC_MIN; break;
                                case 1: f = &p.props.range.src_max;
                                    k = CONNECTION_RANGE_SRC_MAX; break;
                                case 2: f = &p.props.range.dest_min;
                                    k = CONNECTION_RANGE_DEST_MIN; break;
                                case 3: f = &p.props.range.dest_max;
                                    k = CONNECTION_RANGE_DEST_MAX; break;
                                }
                                int ecode = SWIG_AsVal_float(r, f);
                                if (SWIG_IsOK(ecode))
                                    p.props.range.known |= k;
                                else {
                                    ecode = SWIG_AsVal_int(r, &n);
                                    if (SWIG_IsOK(ecode)) {
                                        *f = (float)n;
                                        p.props.range.known |= k;
                                    }
                                }
                            }
                            p.flags |= p.props.range.known;
                        }
                    }
                    else if (strcmp(s, "expression")==0) {
                        if (PyString_Check(v)) {
                            p.props.expression = PyString_AsString(v);
                            p.flags |= CONNECTION_EXPRESSION;
                        }
                    }
                    else if (strcmp(s, "mode")==0) {
                        int ecode = SWIG_AsVal_int(v, &k);
                        if (SWIG_IsOK(ecode)) {
                            p.props.mode = k;
                            p.flags |= CONNECTION_MODE;
                        }
                    }
                    else if (strcmp(s, "muted")==0) {
                        k = -1;
                        if (v == Py_True)
                            k = 1;
                        else if (v == Py_False) {
                            k = 0;
                        }
                        else {
                            int ecode = SWIG_AsVal_int(v, &k);
                            if (SWIG_IsOK(ecode))
                                k = k!=0;
                            else
                                k = -1;
                        }
                        if (k>-1) {
                            p.props.muted = k;
                            p.flags |= CONNECTION_MUTED;
                        }
                    }
                    else if (strcmp(s, "src_name")==0) {
                        if (PyString_Check(v))
                            p.props.src_name = PyString_AsString(v);
                    }
                    else if (strcmp(s, "dest_name")==0) {
                        if (PyString_Check(v))
                            p.props.dest_name = PyString_AsString(v);
                    }
                    else if (strcmp(s, "src_type")==0) {
                        if (PyString_Check(v))
                            p.props.src_type = PyString_AsString(v)[0];
                    }
                    else if (strcmp(s, "dest_type")==0) {
                        if (PyString_Check(v))
                            p.props.dest_type = PyString_AsString(v)[0];
                    }
                    else if (strcmp(s, "src_length")==0) {
                        int ecode = SWIG_AsVal_int(v, &k);
                        if (SWIG_IsOK(ecode))
                            p.props.src_length = k;
                    }
                    else if (strcmp(s, "dest_length")==0) {
                        int ecode = SWIG_AsVal_int(v, &k);
                        if (SWIG_IsOK(ecode))
                            p.props.dest_length = k;
                    }
                }
            }
            Py_DECREF(keys);
            $1 = &p;
        }
    }
    else {
        SWIG_exception_fail(SWIG_TypeError,
                            "argument $argnum must be 'dict'");
    }
 %}

%typemap(out) mapper_db_device_t ** {
    if ($1) {
        // Return the dict and an opaque pointer.
        // The pointer will be hidden by a Python generator interface.
        PyObject *o = device_to_py(*$1);
        if (o!=Py_None)
            $result = Py_BuildValue("(Ol)", o, $1);
        else
            $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
    else {
        $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
 }

%typemap(out) mapper_db_device {
    return device_to_py($1);
 }

%typemap(out) mapper_db_signal_t ** {
    if ($1) {
        // Return the dict and an opaque pointer.
        // The pointer will be hidden by a Python generator interface.
        PyObject *o = signal_to_py(*$1);
        if (o!=Py_None)
            $result = Py_BuildValue("(Ol)", o, $1);
        else
            $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
    else {
        $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
 }

%typemap(out) mapper_db_signal {
    return signal_to_py($1);
 }

%typemap(out) mapper_db_connection_t ** {
    if ($1) {
        // Return the dict and an opaque pointer.
        // The pointer will be hidden by a Python generator interface.
        PyObject *o = connection_to_py(*$1);
        if (o!=Py_None)
            $result = Py_BuildValue("(Ol)", o, $1);
        else
            $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
    else {
        $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
 }

%typemap(out) mapper_db_connection {
    return connection_to_py($1);
 }

%typemap(out) mapper_db_link_t ** {
    if ($1) {
        // Return the dict and an opaque pointer.
        // The pointer will be hidden by a Python generator interface.
        PyObject *o = link_to_py(*$1);
        if (o!=Py_None)
            $result = Py_BuildValue("(Ol)", o, $1);
        else
            $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
    else {
        $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
 }

%typemap(out) mapper_db_link {
    return link_to_py($1);
 }

%{
#include <mapper_internal.h>

// Note: inet_ntoa() crashes on OS X if this header isn't included!
// On the other hand, doesn't compile on Windows if it is.
#ifdef __APPLE__
#include <arpa/inet.h>
#endif

typedef struct _device {} device;
typedef struct _signal {} signal__;
typedef struct _monitor {} monitor;
typedef struct _db {} db;
typedef struct _admin {} admin;

PyThreadState *_save;

static PyObject *device_to_py(mapper_db_device_t *dev)
{
    if (!dev) return Py_None;
    PyObject *o = PyDict_New();
    if (!o)
        return Py_None;
    else {
        int i=0;
        const char *property;
        lo_type type;
        const lo_arg *value;
        while (!mapper_db_device_property_index(dev, i, &property,
                                                &type, &value))
        {
            if (strcmp(property, "user_data")==0) {
                i++;
                continue;
            }
            PyObject *v = 0;
            if (type=='s' || type=='S')
                v = PyString_FromString(&value->s);
            else if (type=='c')
                v = Py_BuildValue("c", value->c);
            else if (type=='i')
                v = Py_BuildValue("i", value->i32);
            else if (type=='h')
                v = Py_BuildValue("l", value->i64);
            else if (type=='f')
                v = Py_BuildValue("f", value->f);
            else if (type=='d')
                v = Py_BuildValue("d", value->d);
            if (v) {
                PyDict_SetItemString(o, property, v);
                Py_DECREF(v);
            }
            i++;
        }
    }
    return o;
}

static PyObject *signal_to_py(mapper_db_signal_t *sig)
{
    if (!sig) return Py_None;
    PyObject *o = PyDict_New();
    if (!o)
        return Py_None;
    else {
        int i=0;
        const char *property;
        lo_type type;
        const lo_arg *value;
        while (!mapper_db_signal_property_index(sig, i, &property,
                                                &type, &value))
        {
            if (strcmp(property, "user_data")==0) {
                i++;
                continue;
            }
            PyObject *v = 0;
            if (type=='s' || type=='S')
                v = PyString_FromString(&value->s);
            else if (type=='c')
                v = Py_BuildValue("c", value->c);
            else if (type=='i')
                v = Py_BuildValue("i", value->i32);
            else if (type=='h')
                v = Py_BuildValue("l", value->i64);
            else if (type=='f')
                v = Py_BuildValue("f", value->f);
            else if (type=='d')
                v = Py_BuildValue("d", value->d);
            if (v) {
                PyDict_SetItemString(o, property, v);
                Py_DECREF(v);
            }
            i++;
        }
    }
    return o;
}

static PyObject *connection_to_py(mapper_db_connection_t *con)
{
    if (!con) return Py_None;
    PyObject *o;
    o = Py_BuildValue("{s:s,s:s,s:c,s:c,s:i,s:i,s:i,s:i,s:(OOOO),s:s,s:i,s:i}",
                      "src_name", con->src_name,
                      "dest_name", con->dest_name,
                      "src_type", con->src_type,
                      "dest_type", con->dest_type,
                      "src_length", con->src_length,
                      "dest_length", con->dest_length,
                      "clip_max", con->clip_max,
                      "clip_min", con->clip_min,
                      "range",
                      (con->range.known & CONNECTION_RANGE_SRC_MIN
                       ? Py_BuildValue("f", con->range.src_min) : Py_None),
                      (con->range.known & CONNECTION_RANGE_SRC_MAX
                       ? Py_BuildValue("f", con->range.src_max) : Py_None),
                      (con->range.known & CONNECTION_RANGE_DEST_MIN
                       ? Py_BuildValue("f", con->range.dest_min) : Py_None),
                      (con->range.known & CONNECTION_RANGE_DEST_MAX
                       ? Py_BuildValue("f", con->range.dest_max) : Py_None),
                      "expression", con->expression,
                      "mode", con->mode,
                      "muted", con->muted);
    return o;
}

static PyObject *link_to_py(mapper_db_link_t *link)
{
    if (!link) return Py_None;
    PyObject *o =
        Py_BuildValue("{s:s,s:s}",
                      "src_name", link->src_name, 
                      "dest_name", link->dest_name);
    return o;
}

/* Note: We want to call the signal object 'signal', but there is
 * already a function in the C standard library called signal().
 * Solution is to name it something else (signal__) but still tell
 * SWIG that it is called 'signal', and then use the preprocessor to
 * do replacement. Should work as long as signal() doesn't need to be
 * called from the SWIG wrapper code. */
#define signal signal__

/* Wrapper for callback back to python when a mapper_signal handler is
 * called. */
static void msig_handler_py(struct _mapper_signal *msig,
                            mapper_db_signal props,
                            mapper_timetag_t *tt,
                            void *v)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist=0;
    PyObject *result=0;

    PyObject *py_msig = SWIG_NewPointerObj(SWIG_as_voidptr(msig),
                                          SWIGTYPE_p__signal, 0);

    if (v) {
        if (props->type == 'i')
            arglist = Py_BuildValue("(Oi)", py_msig, *(int*)v);
        else if (props->type == 'f')
            arglist = Py_BuildValue("(Of)", py_msig, *(float*)v);
    }
    else {
        arglist = Py_BuildValue("(Os)", py_msig, 0);
    }
    if (!arglist) {
        printf("[mapper] Could not build arglist (msig_handler_py).\n");
        return;
    }
    result = PyEval_CallObject((PyObject*)props->user_data, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

typedef struct {
    mapper_signal_value_t v;
    char t;
} sigval, *maybeSigVal;

typedef int* maybeInt;
typedef int booltype;

typedef struct {
    mapper_db_connection_t props;
    int flags;
} mapper_db_connection_with_flags_t;

static void sigval_coerce(mapper_signal_value_t *out, sigval *v, char type)
{
    if (v->t == 'i' && type == 'f')
        out->f = (float)v->v.i32;
    else if (v->t == 'f' && type == 'i')
        out->i32 = (int)v->v.f;
}


/* Wrapper for callback back to python when a mapper_db_device handler
 * is called. */
static void device_db_handler_py(mapper_db_device record,
                                 mapper_db_action_t action,
                                 void *user)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist = Py_BuildValue("(Oi)", device_to_py(record), action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (device_db_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_db_signal handler
 * is called. */
static void signal_db_handler_py(mapper_db_signal record,
                                 mapper_db_action_t action,
                                 void *user)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist = Py_BuildValue("(Oi)", signal_to_py(record), action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (signal_db_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_db_connection handler
 * is called. */
static void connection_db_handler_py(mapper_db_connection record,
                                     mapper_db_action_t action,
                                     void *user)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist = Py_BuildValue("(Oi)", connection_to_py(record),
                                      action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (connection_db_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_db_link handler
 * is called. */
static void link_db_handler_py(mapper_db_link record,
                               mapper_db_action_t action,
                               void *user)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist = Py_BuildValue("(Oi)", link_to_py(record), action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (link_db_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

%}

typedef enum _mapper_clipping_type {
    CT_NONE,    /*!< Value is passed through unchanged. This is the
                 *   default. */
    CT_MUTE,    //!< Value is muted.
    CT_CLAMP,   //!< Value is limited to the boundary.
    CT_FOLD,    //!< Value continues in opposite direction.
    CT_WRAP,    /*!< Value appears as modulus offset at the opposite
                 *   boundary. */
    N_MAPPER_CLIPPING_TYPES
} mapper_clipping_type;

/*! Describes the connection mode.
 *  @ingroup connectiondb */
typedef enum _mapper_mode_type {
    MO_UNDEFINED,    //!< Not yet defined
    MO_BYPASS,       //!< Direct throughput
    MO_LINEAR,       //!< Linear scaling
    MO_EXPRESSION,   //!< Expression
    MO_CALIBRATE,    //!< Calibrate to source signal
    N_MAPPER_MODE_TYPES
} mapper_mode_type;

/*! The set of possible actions on a database record, used
 *  to inform callbacks of what is happening to a record. */
typedef enum {
    MDB_MODIFY,
    MDB_NEW,
    MDB_REMOVE,
} mapper_db_action_t;

typedef struct _device {} device;
typedef struct _signal {} signal;
typedef struct _monitor {} monitor;
typedef struct _db {} db;
typedef struct _admin {} admin;

%extend device {
    device(const char *name, int port=9000, admin *DISOWN=0) {
        device *d = (device *)mdev_new(name, port, (mapper_admin) DISOWN);
        return d;
    }
    ~device() {
        mdev_free((mapper_device)$self);
    }
    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mdev_poll((mapper_device)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }
    int ready() {
        return mdev_ready((mapper_device)$self);
    }

    // Note, these functions return memory which is _not_ owned by
    // Python.  Correspondingly, the SWIG default is to set thisown to
    // False, which is correct for this case.
    signal* add_input(const char *name, int length=1, const char type='f',
                      const char *unit=0, maybeSigVal minimum=0,
                      maybeSigVal maximum=0, PyObject *PyFunc=0)
    {
        mapper_signal_handler *h = 0;
        if (PyFunc) {
            h = msig_handler_py;
            Py_XINCREF(PyFunc);
        }
        mapper_signal_value_t mn, mx, *pmn=0, *pmx=0;
        if (type == 'f')
        {
            if (minimum) {
                if (minimum->t == 'f')
                    pmn = &minimum->v;
                else {
                    mn.f = (float)minimum->v.i32;
                    pmn = &mn;
                }
            }
            if (maximum) {
                if (maximum->t == 'f')
                    pmx = &maximum->v;
                else {
                    mx.f = (float)maximum->v.i32;
                    pmx = &mx;
                }
            }
        }
        else if (type == 'i')
        {
            if (minimum) {
                if (minimum->t == 'i')
                    pmn = &minimum->v;
                else {
                    mn.i32 = (int)minimum->v.f;
                    pmn = &mn;
                }
            }
            if (maximum) {
                if (maximum->t == 'i')
                    pmx = &maximum->v;
                else {
                    mx.i32 = (int)maximum->v.f;
                    pmx = &mx;
                }
            }
        }
        return (signal *)mdev_add_input((mapper_device)$self, name, length,
                                        type, unit, pmn, pmx, h, PyFunc);
    }
    signal* add_output(const char *name, int length=1, const char type='f',
                       const char *unit=0, maybeSigVal minimum=0,
                       maybeSigVal maximum=0)
    {
        mapper_signal_value_t mn, mx, *pmn=0, *pmx=0;
        if (type == 'f')
        {
            if (minimum) {
                if (minimum->t == 'f')
                    pmn = &minimum->v;
                else {
                    mn.f = (float)minimum->v.i32;
                    pmn = &mn;
                }
            }
            if (maximum) {
                if (maximum->t == 'f')
                    pmx = &maximum->v;
                else {
                    mx.f = (float)maximum->v.i32;
                    pmx = &mx;
                }
            }
        }
        else if (type == 'i')
        {
            if (minimum) {
                if (minimum->t == 'i')
                    pmn = &minimum->v;
                else {
                    mn.i32 = (int)minimum->v.f;
                    pmn = &mn;
                }
            }
            if (maximum) {
                if (maximum->t == 'i')
                    pmx = &maximum->v;
                else {
                    mx.i32 = (int)maximum->v.f;
                    pmx = &mx;
                }
            }
        }
        return (signal*)mdev_add_output((mapper_device)$self, name,
                                      length, type, unit, pmn, pmx);
    }
    void remove_input(signal *sig) {
        mapper_signal msig = (mapper_signal)sig;
        if (msig->props.user_data) {
            Py_XDECREF((PyObject*)msig->props.user_data);
        }
        return mdev_remove_input((mapper_device)$self, (mapper_signal)sig);
    }
    void remove_output(signal *sig) {
        mapper_signal msig = (mapper_signal)sig;
        if (msig->props.user_data) {
            Py_XDECREF((PyObject*)msig->props.user_data);
        }
        return mdev_remove_output((mapper_device)$self, msig);
    }
    maybeInt get_port() {
        mapper_device md = (mapper_device)$self;
        int port = mdev_port(md);
        if (port) {
            int *pi = malloc(sizeof(int));
            *pi = port;
            return pi;
        }
        return 0;
    }
    const char *get_name() { return mdev_name((mapper_device)$self); }
    const char *get_ip4() {
        const struct in_addr *a = mdev_ip4((mapper_device)$self);
        return a ? inet_ntoa(*a) : 0;
    }
    const char *get_interface() { return mdev_interface((mapper_device)$self); }
    unsigned int get_ordinal() { return mdev_ordinal((mapper_device)$self); }
    int get_num_inputs() { return mdev_num_inputs((mapper_device)$self); }
    int get_num_outputs() { return mdev_num_outputs((mapper_device)$self); }
    signal *get_input_by_name(const char *name) {
        return (signal *)mdev_get_input_by_name((mapper_device)$self, name, 0);
    }
    signal *get_output_by_name(const char *name) {
        return (signal *)mdev_get_output_by_name((mapper_device)$self, name, 0);
    }
    signal *get_input_by_index(int index) {
        return (signal *)mdev_get_input_by_index((mapper_device)$self, index);
    }
    signal *get_output_by_index(int index) {
        return (signal *)mdev_get_output_by_index((mapper_device)$self, index);
    }
    void set_property(const char *key, void* val=0) {
        if (!val)
            mdev_remove_property((mapper_device)$self, key);
    }
    void set_property(const char *key, int val) {
        mdev_set_property((mapper_device)$self, key, 'i', (lo_arg*)&val);
    }
    void set_property(const char *key, int64_t val) {
        mdev_set_property((mapper_device)$self, key, 'h', (lo_arg*)&val);
    }
    void set_property(const char *key, double val) {
        mdev_set_property((mapper_device)$self, key, 'd', (lo_arg*)&val);
    }
    void set_property(const char *key, float val) {
        mdev_set_property((mapper_device)$self, key, 'f', (lo_arg*)&val);
    }
    void set_property(const char *key, const char *val) {
        mdev_set_property((mapper_device)$self, key, 's', (lo_arg*)val);
    }
    void remove_property(const char *key) {
        mdev_remove_property((mapper_device)$self, key);
    }
    %pythoncode {
        port = property(get_port)
        name = property(get_name)
        ip4 = property(get_ip4)
        interface = property(get_interface)
        ordinal = property(get_ordinal)
        num_inputs = property(get_num_inputs)
        num_outputs = property(get_num_outputs)
        def __propgetter(self):
            signal = self
            props = {}
            class propsetter(dict):
                __getitem__ = props.__getitem__
                def __setitem__(self, key, value):
                    props[key] = value
                    signal.set_property(key, value)
            return propsetter({})
        properties = property(__propgetter)
        def set_properties(self, props):
            [self.set_property(k, props[k]) for k in props]
    }
}

%extend signal {
    const char *get_name() {
        return ((mapper_signal)$self)->props.name;
    }
    const char *get_full_name() {
        mapper_signal sig = (mapper_signal)$self;
        char s[1024];
        int len = msig_full_name(sig, s, 1024);
        if (len) {
            // TODO: memory leak
            char *str = (char*)malloc(len+1);
            strncpy(str, s, len+1);
            return str;
        }
        return 0;
    }
    void update(float f) {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.type == 'f')
            msig_update_float((mapper_signal)$self, f);
        else if (sig->props.type == 'i') {
            msig_update_int((mapper_signal)$self, (int)f);
        }
    }
    void update(int i) {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.type == 'i')
            msig_update_int((mapper_signal)$self, i);
        else if (sig->props.type == 'f') {
            msig_update_float((mapper_signal)$self, (float)i);
        }
    }
    void set_query_callback(PyObject *PyFunc=0) {
        mapper_signal_handler *h = 0;
        mapper_signal msig = (mapper_signal)$self;
        if (PyFunc && !msig->props.user_data) {
            h = msig_handler_py;
            Py_XINCREF(PyFunc);
        }
        else if (!PyFunc && msig->props.user_data) {
            if (msig->props.user_data) {
                Py_XDECREF((PyObject*)msig->props.user_data);
            }
        }
        return msig_set_query_callback((mapper_signal)$self, h, PyFunc);
    }
    int query_remote() {
        return msig_query_remote((mapper_signal)$self);
    }
    void set_minimum(maybeSigVal v) {
        mapper_signal sig = (mapper_signal)$self;
        if (!v)
            msig_set_minimum((mapper_signal)$self, 0);
        else if (v->t == sig->props.type)
            msig_set_minimum((mapper_signal)$self, &v->v);
        else {
            mapper_signal_value_t tmp;
            sigval_coerce(&tmp, v, sig->props.type);
            msig_set_minimum((mapper_signal)$self, &tmp);
        }
    }
    void set_maximum(maybeSigVal v) {
        mapper_signal sig = (mapper_signal)$self;
        if (!v)
            msig_set_maximum((mapper_signal)$self, 0);
        else if (v->t == sig->props.type)
            msig_set_maximum((mapper_signal)$self, &v->v);
        else {
            mapper_signal_value_t tmp;
            sigval_coerce(&tmp, v, sig->props.type);
            msig_set_maximum((mapper_signal)$self, &tmp);
        }
    }
    maybeSigVal get_minimum() {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.minimum) {
            sigval *pv = malloc(sizeof(sigval));
            pv->t = sig->props.type;
            pv->v = *sig->props.minimum;
            return pv;
        }
        return 0;
    }
    maybeSigVal get_maximum() {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.maximum) {
            sigval *pv = malloc(sizeof(sigval));
            pv->t = sig->props.type;
            pv->v = *sig->props.maximum;
            return pv;
        }
        return 0;
    }
    int get_length() { return ((mapper_signal)$self)->props.length; }
    char get_type() { return ((mapper_signal)$self)->props.type; }
    booltype get_is_output() { return ((mapper_signal)$self)->props.is_output; }
    const char* get_device_name() {
        return ((mapper_signal)$self)->props.device_name;
    }
    const char* get_unit() {
        return ((mapper_signal)$self)->props.unit;
    }
    mapper_db_signal get_properties() {
        return msig_properties((mapper_signal)$self);
    }
    void set_property(const char *key, void* val=0) {
        if (!val)
            msig_remove_property((mapper_signal)$self, key);
    }
    void set_property(const char *key, int val) {
        msig_set_property((mapper_signal)$self, key, 'i', (lo_arg*)&val);
    }
    void set_property(const char *key, int64_t val) {
        msig_set_property((mapper_signal)$self, key, 'h', (lo_arg*)&val);
    }
    void set_property(const char *key, double val) {
        msig_set_property((mapper_signal)$self, key, 'd', (lo_arg*)&val);
    }
    void set_property(const char *key, float val) {
        msig_set_property((mapper_signal)$self, key, 'f', (lo_arg*)&val);
    }
    void set_property(const char *key, const char *val) {
        msig_set_property((mapper_signal)$self, key, 's', (lo_arg*)val);
    }
    void remove_property(const char *key) {
        msig_remove_property((mapper_signal)$self, key);
    }
    %pythoncode {
        minimum = property(get_minimum, set_minimum)
        maximum = property(get_maximum, set_maximum)
        name = property(get_name)
        full_name = property(get_full_name)
        length = property(get_length)
        type = property(get_type)
        is_output = property(get_is_output)
        unit = property(get_unit)
        def __propgetter(self):
            signal = self
            props = self.get_properties()
            class propsetter(dict):
                __getitem__ = props.__getitem__
                def __setitem__(self, key, value):
                    props[key] = value
                    signal.set_property(key, value)
            return propsetter(self.get_properties())
        properties = property(__propgetter)
        def set_properties(self, props):
            [self.set_property(k, props[k]) for k in props]
        def __setattr__(self, name, value):
            try:
                {'minimum': self.set_minimum,
                 'maximum': self.set_maximum}[name](value)
            except KeyError:
                _swig_setattr(self, signal, name, value)
    }
}

%extend monitor {
    monitor(admin *DISOWN=0, int enable_autorequest=1) {
        return (monitor *)mapper_monitor_new((mapper_admin) DISOWN,
                                             enable_autorequest);
    }
    ~monitor() {
        mapper_monitor_free((mapper_monitor)$self);
    }
    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mapper_monitor_poll((mapper_monitor)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }
    db *get_db() {
        return (db *)mapper_monitor_get_db((mapper_monitor)$self);
    }
    void autorequest(const int enable) {
        mapper_monitor_autorequest((mapper_monitor)$self, enable);
    }
    int request_devices() {
        return mapper_monitor_request_devices((mapper_monitor)$self);
    }
    int request_signals_by_name(const char* name) {
        return mapper_monitor_request_signals_by_name((mapper_monitor)$self, name);
    }
    int request_links_by_name(const char* name) {
        return mapper_monitor_request_links_by_name((mapper_monitor)$self, name);
    }
    int request_connections_by_name(const char* name) {
        return mapper_monitor_request_connections_by_name((mapper_monitor)$self, name);
    }
    void link(const char* source_device, const char* dest_device) {
        mapper_monitor_link((mapper_monitor)$self, source_device, dest_device);
    }
    void unlink(const char* source_device, const char* dest_device) {
        mapper_monitor_unlink((mapper_monitor)$self, source_device, dest_device);
    }
    void modify(mapper_db_connection_with_flags_t *properties) {
        if (properties)
        {
            if (!properties->props.src_name || !properties->props.dest_name)
                SWIG_exception_fail(SWIG_ValueError,
                                    "modify() requires 'src_name' and "
                                    "'dest_name' in properties dict");
            mapper_monitor_connection_modify((mapper_monitor)$self, &properties->props,
                                             properties->flags);
          fail:
            ;
        }
    }
    void connect(const char* source_signal,
                 const char* dest_signal,
                 mapper_db_connection_with_flags_t *properties=0) {
        if (properties) {
            mapper_monitor_connect((mapper_monitor)$self, source_signal, dest_signal,
                                   &properties->props, properties->flags);
        }
        else
            mapper_monitor_connect((mapper_monitor)$self, source_signal, dest_signal, 0, 0);
    }
    void disconnect(const char* source_signal, 
                    const char* dest_signal) {
        mapper_monitor_disconnect((mapper_monitor)$self, source_signal, dest_signal);
    }
    %pythoncode {
        db = property(get_db)
    }
}

%extend db {
    void add_device_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_db_add_device_callback((mapper_db)$self, device_db_handler_py, PyFunc);
    }
    void remove_device_callback(PyObject *PyFunc) {
        mapper_db_remove_device_callback((mapper_db)$self, device_db_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
    }
    void add_signal_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_db_add_signal_callback((mapper_db)$self, signal_db_handler_py, PyFunc);
    }
    void remove_signal_callback(PyObject *PyFunc) {
        mapper_db_remove_signal_callback((mapper_db)$self, signal_db_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
    }
    void add_connection_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_db_add_connection_callback((mapper_db)$self, connection_db_handler_py,
                                          PyFunc);
    }
    void remove_connection_callback(PyObject *PyFunc) {
        mapper_db_remove_connection_callback((mapper_db)$self, connection_db_handler_py,
                                             PyFunc);
        Py_XDECREF(PyFunc);
    }
    void add_link_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_db_add_link_callback((mapper_db)$self, link_db_handler_py, PyFunc);
    }
    void remove_link_callback(PyObject *PyFunc) {
        mapper_db_remove_link_callback((mapper_db)$self, link_db_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
    }
    mapper_db_device_t **get_all_devices() {
        return mapper_db_get_all_devices((mapper_db)$self);
    }
    mapper_db_device_t **__match_devices_by_name(const char *device) {
        return mapper_db_match_devices_by_name((mapper_db)$self, device);
    }
    mapper_db_device_t **device_next(long iterator) {
        return mapper_db_device_next((mapper_db_device_t**)iterator);
    }
    mapper_db_signal_t **get_all_inputs() {
        return mapper_db_get_all_inputs((mapper_db)$self);
    }
    mapper_db_signal_t **get_all_outputs() {
        return mapper_db_get_all_outputs((mapper_db)$self);
    }
    mapper_db_signal_t **get_inputs_by_device_name(const char *device_name) {
        return mapper_db_get_inputs_by_device_name((mapper_db)$self,
                                                   device_name);
    }
    mapper_db_signal_t **get_outputs_by_device_name(const char *device_name) {
        return mapper_db_get_outputs_by_device_name((mapper_db)$self,
                                                    device_name);
    }
    mapper_db_signal_t **__match_inputs_by_device_name(
        const char *device_name, const char *input_pattern) {
        return mapper_db_match_inputs_by_device_name((mapper_db)$self,
                                                     device_name,
                                                     input_pattern);
    }
    mapper_db_signal_t **__match_outputs_by_device_name(
        const char *device_name, char const *output_pattern) {
        return mapper_db_match_outputs_by_device_name((mapper_db)$self,
                                                      device_name,
                                                      output_pattern);
    }
    mapper_db_signal_t **signal_next(long iterator) {
        return mapper_db_signal_next((mapper_db_signal_t**)iterator);
    }
    mapper_db_connection_t **get_all_connections() {
        return mapper_db_get_all_connections((mapper_db)$self);
    }
    mapper_db_connection_t **get_connections_by_device_name(
        const char *device_name) {
        return mapper_db_get_connections_by_device_name((mapper_db)$self,
                                                        device_name);
    }
    mapper_db_connection_t **get_connections_by_input_name(
        const char *input_name) {
        return mapper_db_get_connections_by_input_name((mapper_db)$self,
                                                       input_name);
    }
    mapper_db_connection_t **get_connections_by_device_and_input_name(
        const char *device_name, const char *input_name) {
        return mapper_db_get_connections_by_device_and_input_name(
            (mapper_db)$self, device_name, input_name);
    }
    mapper_db_connection_t **get_connections_by_output_name(
        const char *output_name) {
        return mapper_db_get_connections_by_output_name((mapper_db)$self,
                                                        output_name);
    }
    mapper_db_connection_t **get_connections_by_device_and_output_name(
        const char *device_name, const char *output_name) {
        return mapper_db_get_connections_by_device_and_output_name(
            (mapper_db)$self, device_name, output_name);
    }
    mapper_db_connection_t **get_connections_by_device_and_signal_names(
        const char *input_device_name,  const char *input_name,
        const char *output_device_name, const char *output_name) {
        return mapper_db_get_connections_by_device_and_signal_names(
            (mapper_db)$self, input_device_name, input_name,
            output_device_name, output_name);
    }
    mapper_db_connection connection_by_signal_full_names(
        const char *src_name, const char *dest_name) {
        return mapper_db_get_connection_by_signal_full_names(
            (mapper_db)$self, src_name, dest_name);
    }
    mapper_db_connection_t **get_connections_by_src_dest_device_names(
        const char *src_device_name, const char *dest_device_name) {
        return mapper_db_get_connections_by_src_dest_device_names(
            (mapper_db)$self, src_device_name, dest_device_name);
    }
    mapper_db_connection_t **connection_next(long iterator) {
        return mapper_db_connection_next((mapper_db_connection_t**)iterator);
    }
    mapper_db_link_t **get_all_links() {
        return mapper_db_get_all_links((mapper_db)$self);
    }
    mapper_db_link_t **get_links_by_device_name(const char *dev_name) {
        return mapper_db_get_links_by_device_name((mapper_db)$self, dev_name);
    }
    mapper_db_link_t **get_links_by_src_device_name(
        const char *src_device_name) {
        return mapper_db_get_links_by_src_device_name((mapper_db)$self,
                                                      src_device_name);
    }
    mapper_db_link_t **get_links_by_dest_device_name(
        const char *dest_device_name) {
        return mapper_db_get_links_by_dest_device_name((mapper_db)$self,
                                                       dest_device_name);
    }
    mapper_db_link link_by_src_dest_names(const char *src_device_name,
                                          const char *dest_device_name) {
        return mapper_db_get_link_by_src_dest_names((mapper_db)$self,
                                                    src_device_name,
                                                    dest_device_name);
    }
    mapper_db_link_t **link_next(long iterator) {
        return mapper_db_link_next((mapper_db_link_t**)iterator);
    }
    %pythoncode {
        def make_iterator(first, next):
            def it(self, *args):
                (d, p) = first(self, *args)
                while p:
                    yield d
                    (d, p) = next(self, p)
            return it
        all_devices = make_iterator(get_all_devices, device_next)
        match_devices_by_name = make_iterator(__match_devices_by_name,
                                              device_next)
        all_inputs = make_iterator(get_all_inputs, signal_next)
        all_outputs = make_iterator(get_all_outputs, signal_next)
        inputs_by_device_name = make_iterator(get_inputs_by_device_name,
                                              signal_next)
        outputs_by_device_name = make_iterator(get_outputs_by_device_name,
                                               signal_next)
        match_inputs_by_device_name = make_iterator(
            __match_inputs_by_device_name, signal_next)
        match_outputs_by_device_name = make_iterator(
            __match_outputs_by_device_name, signal_next)
        all_connections = make_iterator(get_all_connections, connection_next)
        connections_by_device_name = make_iterator(get_connections_by_device_name,
                                                connection_next)
        connections_by_input_name = make_iterator(get_connections_by_input_name,
                                               connection_next)
        connections_by_device_and_input_name = make_iterator(
            get_connections_by_device_and_input_name, connection_next)
        connections_by_output_name = make_iterator(get_connections_by_output_name,
                                                connection_next)
        connections_by_device_and_output_name = make_iterator(
            get_connections_by_device_and_output_name, connection_next)
        connections_by_device_and_signal_names = make_iterator(
            get_connections_by_device_and_signal_names, connection_next)
        connections_by_src_dest_device_names = make_iterator(
            get_connections_by_src_dest_device_names, connection_next)
        all_links = make_iterator(get_all_links, link_next)
        links_by_device_name = make_iterator(get_links_by_device_name,
                                             link_next)
        links_by_src_device_name = make_iterator(
            get_links_by_src_device_name, link_next)
        links_by_dest_device_name = make_iterator(
            get_links_by_dest_device_name, link_next)
    }
}

%extend admin {
    admin(const char *iface=0, const char *ip=0, int port=7570) {
        return (admin *)mapper_admin_new(iface, ip, port);
    }
    ~admin() {
        mapper_admin_free((mapper_admin)$self);
    }
}
