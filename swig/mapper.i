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
        int ecode = SWIG_AsVal_float($input, &val.v.f);
        if (SWIG_IsOK(ecode))
            val.t = 'f';
        else {
            ecode = SWIG_AsVal_int($input, &val.v.i32);
            if (SWIG_IsOK(ecode))
                val.t = 'i';
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
%{
#include <mapper_internal.h>
typedef struct _device {} device;
typedef struct _signal {} signal__;

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
    result = PyEval_CallObject((PyObject*)msig->user_data, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
}

typedef struct {
    mapper_signal_value_t v;
    char t;
} sigval, *maybeSigVal;

typedef int* maybeInt;
typedef int boolean;

void sigval_coerce(mapper_signal_value_t *out, sigval *v, char type)
{
    if (v->t == 'i' && type == 'f')
        out->f = (float)v->v.i32;
    else if (v->t == 'f' && type == 'i')
        out->i32 = (int)v->v.f;
}

%}

typedef struct _device {} device;
typedef struct _signal {} signal;

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
        float mn, mx, *pmn=0, *pmx=0;
        if (type == 'f')
        {
            if (minimum) {
                if (minimum->t == 'f')
                    pmn = &minimum->v.f;
                else {
                    mn = (float)minimum->v.i32;
                    pmn = &mn;
                }
            }
            if (maximum) {
                if (maximum->t == 'f')
                    pmx = &maximum->v.f;
                else {
                    mx = (float)maximum->v.i32;
                    pmx = &mx;
                }
            }
        }
        else if (type == 'i')
        {
            if (minimum) {
                if (minimum->t == 'i')
                    pmn = &minimum->v.i32;
                else {
                    mn = (int)minimum->v.f;
                    pmn = &mn;
                }
            }
            if (maximum) {
                if (maximum->t == 'i')
                    pmx = &maximum->v.i32;
                else {
                    mx = (int)maximum->v.f;
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
        float mn, mx, *pmn=0, *pmx=0;
        if (type == 'f')
        {
            if (minimum) {
                if (minimum->t == 'f')
                    pmn = &minimum->v.f;
                else {
                    mn = (float)minimum->v.i32;
                    pmn = &mn;
                }
            }
            if (maximum) {
                if (maximum->t == 'f')
                    pmx = &maximum->v.f;
                else {
                    mx = (float)maximum->v.i32;
                    pmx = &mx;
                }
            }
        }
        else if (type == 'i')
        {
            if (minimum) {
                if (minimum->t == 'i')
                    pmn = &minimum->v.i32;
                else {
                    mn = (int)minimum->v.f;
                    pmn = &mn;
                }
            }
            if (maximum) {
                if (maximum->t == 'i')
                    pmx = &maximum->v.i32;
                else {
                    mx = (int)maximum->v.f;
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
    int get_num_inputs() { return mdev_num_inputs($self); }
    int get_num_outputs() { return mdev_num_outputs($self); }
    %pythoncode {
        port = property(get_port)
        name = property(get_name)
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
    void update_scalar(float f) {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.type == 'f')
            msig_update_scalar($self, *(mapper_signal_value_t*)&f);
        else if (sig->props.type == 'i') {
            int i = (int)f;
            msig_update_scalar($self, *(mapper_signal_value_t*)&i);
        }
    }
    void update_scalar(int i) {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.type == 'i')
            msig_update_scalar($self, *(mapper_signal_value_t*)&i);
        else if (sig->props.type == 'f') {
            float f = (float)i;
            msig_update_scalar($self, *(mapper_signal_value_t*)&f);
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
    %pythoncode {
        minimum = property(get_minimum, set_minimum)
        maximum = property(get_maximum, set_maximum)
        name = property(get_name)
        full_name = property(get_full_name)
        length = property(get_length)
        type = property(get_type)
        is_output = property(get_is_output)
        unit = property(get_unit)
        def __setattr__(self, name, value):
            try:
                {'minimum': self.set_minimum,
                 'maximum': self.set_maximum}[name](value)
            except KeyError:
                _swig_setattr(self, signal, name, value)
    }
}
