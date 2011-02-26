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
%typemap(out) boolean {
    PyObject *o = $1 ? Py_True : Py_False;
    Py_INCREF(o);
    return o;
 }
%typemap(out) mapper_db_signal {
    PyObject *o = PyDict_New();
    if (!o)
        o = Py_None;
    else {
        int i=0;
        const char *property;
        lo_type type;
        lo_arg *value;
        while (!mapper_db_signal_property_index($1, i, &property,
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
            else if (type=='f')
                v = Py_BuildValue("f", value->f);
            if (v) {
                PyDict_SetItemString(o, property, v);
                Py_DECREF(v);
            }
            i++;
        }
    }
    $result = o;
 }
%typemap(in) mapper_db_mapping_with_flags_t* %{
    mapper_db_mapping_with_flags_t p;
    $1 = 0;
    if (PyDict_Check($input)) {
        memset(&p, 0, sizeof(mapper_db_mapping_with_flags_t));
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
                            p.flags |= MAPPING_CLIPMAX;
                        }
                    }
                    else if (strcmp(s, "clip_min")==0) {
                        int ecode = SWIG_AsVal_int(v, &k);
                        if (SWIG_IsOK(ecode)) {
                            p.props.clip_min = k;
                            p.flags |= MAPPING_CLIPMIN;
                        }
                    }
                    else if (strcmp(s, "range")==0) {
                        if (PyList_Check(v)) {
                            int len = PyList_Size(v), j, n;
                            float *f;
                            for (j=0; j<len && j<4; j++) {
                                PyObject *r = PyList_GetItem(v, j);
                                switch (j) {
                                case 0: f = &p.props.range.src_min;
                                    k = MAPPING_RANGE_SRC_MIN; break;
                                case 1: f = &p.props.range.src_max;
                                    k = MAPPING_RANGE_SRC_MAX; break;
                                case 2: f = &p.props.range.dest_min;
                                    k = MAPPING_RANGE_DEST_MIN; break;
                                case 3: f = &p.props.range.dest_max;
                                    k = MAPPING_RANGE_DEST_MAX; break;
                                }
                                int ecode = SWIG_AsVal_float(r, f);
                                if (SWIG_IsOK(ecode))
                                    p.props.range.known |= k;
                                else {
                                    ecode = SWIG_AsVal_float(r, &n);
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
                            p.flags |= MAPPING_EXPRESSION;
                        }
                    }
                    else if (strcmp(s, "mode")==0) {
                        int ecode = SWIG_AsVal_int(v, &k);
                        if (SWIG_IsOK(ecode)) {
                            p.props.mode = k;
                            p.flags |= MAPPING_MODE;
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
                            p.flags |= MAPPING_MUTED;
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
                            p.props.dest_type = PyString_AsString(v)[0];
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
        PyObject *o = PyDict_New();
        if (!o)
            o = Py_None;
        else {
            int i=0;
            const char *property;
            lo_type type;
            lo_arg *value;
            while (!mapper_db_device_property_index(*$1, i, &property,
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
                else if (type=='f')
                    v = Py_BuildValue("f", value->f);
                if (v) {
                    PyDict_SetItemString(o, property, v);
                    Py_DECREF(v);
                }
                i++;
            }
            // Return the dict and an opaque pointer.
            // The pointer will be hidden by a Python generator interface.
            o = Py_BuildValue("(Oi)", o, $1);
        }
        $result = o;
    }
    else {
        $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
 }
%typemap(out) mapper_db_signal_t ** {
    if ($1) {
        PyObject *o = PyDict_New();
        if (!o)
            o = Py_None;
        else {
            int i=0;
            const char *property;
            lo_type type;
            lo_arg *value;
            while (!mapper_db_signal_property_index(*$1, i, &property,
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
                else if (type=='f')
                    v = Py_BuildValue("f", value->f);
                if (v) {
                    PyDict_SetItemString(o, property, v);
                    Py_DECREF(v);
                }
                i++;
            }
            // Return the dict and an opaque pointer.
            // The pointer will be hidden by a Python generator interface.
            o = Py_BuildValue("(Oi)", o, $1);
        }
        $result = o;
    }
    else {
        $result = Py_BuildValue("(OO)", Py_None, Py_None);
    }
 }
%{
#include <mapper_internal.h>
typedef struct _device {} device;
typedef struct _signal {} signal__;
typedef struct _monitor {} monitor;
typedef struct _db {} db;

/* Note: We want to call the signal object 'signal', but there is
 * already a function in the C standard library called signal().
 * Solution is to name it something else (signal__) but still tell
 * SWIG that it is called 'signal', and then use the preprocessor to
 * do replacement. Should work as long as signal() doesn't need to be
 * called from the SWIG wrapper code. */
#define signal signal__

/* Wrapper for callback back to python when a mapper_signal handler is
 * called. */
void msig_handler_py(struct _mapper_signal *msig,
                     mapper_signal_value_t *v)
{
    PyObject *arglist=0;
    PyObject *result=0;

    PyObject *py_msig = SWIG_NewPointerObj(SWIG_as_voidptr(msig),
                                          SWIGTYPE_p__signal, 0);

    if (msig->props.type == 'i')
        arglist = Py_BuildValue("(Oi)", py_msig, v->i32);
    else if (msig->props.type == 'f')
        arglist = Py_BuildValue("(Of)", py_msig, v->f);
    if (!arglist) {
        printf("[mapper] Could not build arglist (msig_handler_py).\n");
        return;
    }
    mapper_db_signal props = msig_properties(msig);
    result = PyEval_CallObject((PyObject*)props->user_data, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
}

typedef struct {
    mapper_signal_value_t v;
    char t;
} sigval, *maybeSigVal;

typedef int* maybeInt;
typedef int boolean;

typedef struct {
    mapper_db_mapping_t props;
    int flags;
} mapper_db_mapping_with_flags_t;

void sigval_coerce(mapper_signal_value_t *out, sigval *v, char type)
{
    if (v->t == 'i' && type == 'f')
        out->f = (float)v->v.i32;
    else if (v->t == 'f' && type == 'i')
        out->i32 = (int)v->v.f;
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

/*! Describes the mapping mode.
 *  @ingroup mappingdb */
typedef enum _mapper_mode_type {
    MO_UNDEFINED,    //!< Not yet defined
    MO_BYPASS,       //!< Direct throughput
    MO_LINEAR,       //!< Linear scaling
    MO_EXPRESSION,   //!< Expression
    MO_CALIBRATE,    //!< Calibrate to source signal
    N_MAPPER_MODE_TYPES
} mapper_mode_type;

typedef struct _device {} device;
typedef struct _signal {} signal;
typedef struct _monitor {} monitor;
typedef struct _db {} db;

%extend device {
    device(const char *name, int port=9000) {
        device *d = mdev_new(name, port, 0);
        return d;
    }
    ~device() {
        mdev_free($self);
    }
    int poll(int timeout=0) {
        return mdev_poll($self, timeout);
    }
    int ready() {
        return mdev_ready($self);
    }

    // Note, these functions return memory which is _not_ owned by
    // Python.  Correspondingly, the SWIG default is to set thisown to
    // False, which is correct for this case.
    signal* add_input(const char *name, const char type='f',
                      PyObject *PyFunc=0, char *unit=0,
                      maybeSigVal minimum=0, maybeSigVal maximum=0)
    {
        void *h = 0;
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
        return mdev_add_input($self, name, 1, type, unit,
                              pmn, pmx, h, PyFunc);
    }
    signal* add_output(const char *name, char type='f', const char *unit=0,
                       maybeSigVal minimum=0, maybeSigVal maximum=0)
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
        return mdev_add_output($self, name, 1, type, unit, pmn, pmx);
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
    const char *get_name() { return mdev_name($self); }
    const char *get_ip4() {
        unsigned int *i = (unsigned int*)mdev_ip4($self);
        return i ? inet_ntoa(*i) : 0;
    }
    const char *get_interface() { return mdev_interface($self); }
    unsigned int get_ordinal() { return mdev_ordinal($self); }
    int get_num_inputs() { return mdev_num_inputs($self); }
    int get_num_outputs() { return mdev_num_outputs($self); }
    signal *get_input_by_name(const char *name) {
        return mdev_get_input_by_name($self, name, 0);
    }
    signal *get_output_by_name(const char *name) {
        return mdev_get_output_by_name($self, name, 0);
    }
    signal *get_input_by_index(int index) {
        return mdev_get_input_by_index($self, index);
    }
    signal *get_output_by_index(int index) {
        return mdev_get_output_by_index($self, index);
    }
    %pythoncode {
        port = property(get_port)
        name = property(get_name)
        ip4 = property(get_ip4)
        interface = property(get_interface)
        ordinal = property(get_ordinal)
        num_inputs = property(get_num_inputs)
        num_outputs = property(get_num_outputs)
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
            msig_update_float($self, f);
        else if (sig->props.type == 'i') {
            msig_update_int($self, (int)f);
        }
    }
    void update(int i) {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.type == 'i')
            msig_update_int($self, i);
        else if (sig->props.type == 'f') {
            msig_update_float($self, (float)i);
        }
    }
    void set_minimum(maybeSigVal v) {
        mapper_signal sig = (mapper_signal)$self;
        if (!v)
            msig_set_minimum($self, 0);
        else if (v->t == sig->props.type)
            msig_set_minimum($self, &v->v);
        else {
            mapper_signal_value_t tmp;
            sigval_coerce(&tmp, v, sig->props.type);
            msig_set_minimum($self, &tmp);
        }
    }
    void set_maximum(maybeSigVal v) {
        mapper_signal sig = (mapper_signal)$self;
        if (!v)
            msig_set_maximum($self, 0);
        else if (v->t == sig->props.type)
            msig_set_maximum($self, &v->v);
        else {
            mapper_signal_value_t tmp;
            sigval_coerce(&tmp, v, sig->props.type);
            msig_set_maximum($self, &tmp);
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
    boolean get_is_output() { return ((mapper_signal)$self)->props.is_output; }
    const char* get_device_name() {
        return ((mapper_signal)$self)->props.device_name;
    }
    const char* get_unit() {
        return ((mapper_signal)$self)->props.unit;
    }
    mapper_db_signal get_properties() {
        return msig_properties($self);
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
        properties = property(get_properties)
        def __setattr__(self, name, value):
            try:
                {'minimum': self.set_minimum,
                 'maximum': self.set_maximum}[name](value)
            except KeyError:
                _swig_setattr(self, signal, name, value)
    }
}

%extend monitor {
    monitor() {
        monitor *m = mapper_monitor_new();
        return m;
    }
    ~monitor() {
        mapper_monitor_free($self);
    }
    int poll(int timeout=0) {
        return mapper_monitor_poll($self, timeout);
    }
    db *get_db() {
        return mapper_monitor_get_db($self);
    }
    int request_devices() {
        return mapper_monitor_request_devices($self);
    }
    int request_signals_by_name(const char* name) {
        return mapper_monitor_request_signals_by_name($self, name);
    }
    int request_links_by_name(const char* name) {
        return mapper_monitor_request_links_by_name($self, name);
    }
    int request_mappings_by_name(const char* name) {
        return mapper_monitor_request_mappings_by_name($self, name);
    }
    void link(const char* source_device, const char* dest_device) {
        mapper_monitor_link($self, source_device, dest_device);
    }
    void unlink(const char* source_device, const char* dest_device) {
        mapper_monitor_unlink($self, source_device, dest_device);
    }
    void modify(mapper_db_mapping_with_flags_t *properties) {
        if (properties)
        {
            if (!properties->props.src_name || !properties->props.dest_name)
                SWIG_exception_fail(SWIG_ValueError,
                                    "modify() requires 'src_name' and "
                                    "'dest_name' in properties dict");
            mapper_monitor_mapping_modify($self, &properties->props,
                                          properties->flags);
          fail:
            ;
        }
    }
    void connect(const char* source_signal,
                 const char* dest_signal,
                 mapper_db_mapping_with_flags_t *properties) {
        if (properties) {
            mapper_monitor_connect($self, source_signal, dest_signal,
                                   &properties->props, properties->flags);
        }
        else
            mapper_monitor_connect($self, source_signal, dest_signal, 0, 0);
    }
    void disconnect(const char* source_signal, 
                    const char* dest_signal) {
        mapper_monitor_disconnect($self, source_signal, dest_signal);
    }
    %pythoncode {
        db = property(get_db)
    }
}

%extend db {
    mapper_db_device_t **get_all_devices() {
        return mapper_db_get_all_devices($self);
    }
    mapper_db_device_t **device_next(int iterator) {
        return mapper_db_device_next((mapper_db_device_t**)iterator);
    }
    mapper_db_signal_t **get_all_inputs() {
        return mapper_db_get_all_inputs($self);
    }
    mapper_db_signal_t **get_all_outputs() {
        return mapper_db_get_all_outputs($self);
    }
    mapper_db_signal_t **signal_next(int iterator) {
        return mapper_db_signal_next((mapper_db_signal_t**)iterator);
    }
    %pythoncode {
        def make_iterator(self, first, next, *args):
            def it():
                (d, p) = first(*args)
                while p:
                    yield d
                    (d, p) = next(p)
            return it
        def all_devices(self):
            return self.make_iterator(self.get_all_devices,
                                      self.device_next)()
        def all_inputs(self):
            return self.make_iterator(self.get_all_inputs,
                                      self.signal_next)()
        def all_outputs(self):
            return self.make_iterator(self.get_all_outputs,
                                      self.signal_next)()
    }
}
