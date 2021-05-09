%module mapper
%include "typemaps.i"
%typemap(in) PyObject *PyFunc {
    if ($input!=Py_None && !PyCallable_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "Need a callable object!");
        return NULL;
    }
    $1 = $input;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_INT64) mpr_id {
    $1 = (PyInt_Check($input) || PyLong_Check($input)) ? 1 : 0;
}
%typemap(in) (mpr_id id) {
    if (PyInt_Check($input))
        $1 = PyInt_AsLong($input);
    else if (PyLong_Check($input)) {
        // truncate to 64 bits
        $1 = PyLong_AsUnsignedLongLong($input);
        if ($1 == (unsigned long long)-1) {
            PyErr_SetString(PyExc_ValueError, "Id value must fit into 64 bits.");
            return NULL;
        }
    }
    else {
        PyErr_SetString(PyExc_ValueError, "Id must be int or long type.");
        return NULL;
    }
}
%typemap(out) mpr_id {
    $result = Py_BuildValue("l", (uint64_t)$1);
}
%typemap(in) (int num_ids, mpr_id *argv) {
    int i;
    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_ValueError, "Expecting a list");
        return NULL;
    }
    $1 = PyList_Size($input);
    $2 = (mpr_id*) malloc($1*sizeof(mpr_id));
    for (i = 0; i < $1; i++) {
        PyObject *s = PyList_GetItem($input,i);
        if (PyInt_Check(s))
            $2[i] = (int)PyInt_AsLong(s);
        else if (PyFloat_Check(s))
            $2[i] = (int)PyFloat_AsDouble(s);
        else {
            free($2);
            PyErr_SetString(PyExc_ValueError, "List items must be int or float.");
            return NULL;
        }
    }
}
%typemap(typecheck) (int num_ids, mpr_id *argv) {
    $1 = PyList_Check($input) ? 1 : 0;
}
%typemap(freearg) (int num_ids, mpr_id *argv) {
    if ($2) free($2);
}
%typemap(typecheck) (signal_array) {
    $1 = PyList_Check($input) || ($input && strcmp(($input)->ob_type->tp_name, "signal")==0) ? 1 : 0;
}
%typemap(in) (signal_array) {
    int i;
    signal *s;
    signal_array_t *sa = alloca(sizeof(*sa));
    if (PyList_Check($input)) {
        sa->len = PyList_Size($input);
        sa->sigs = (mpr_sig*) malloc(sa->len * sizeof(mpr_sig));
        for (i = 0; i < sa->len; i++) {
            PyObject *o = PyList_GetItem($input, i);
            if (o && strcmp(o->ob_type->tp_name, "signal")==0) {
                if (!SWIG_IsOK(SWIG_ConvertPtr(o, (void**)&s, SWIGTYPE_p__signal, 0))) {
                    SWIG_exception_fail(SWIG_TypeError, "in method '$symname', expecting type signal");
                    free(sa->sigs);
                    return NULL;
                }
                sa->sigs[i] = (mpr_sig)s;
            }
            else {
                free(sa->sigs);
                PyErr_SetString(PyExc_ValueError, "List items must be signals.");
                return NULL;
            }
        }
    }
    else if ($input && strcmp(($input)->ob_type->tp_name, "signal")==0) {
        sa->len = 1;
        if (!SWIG_IsOK(SWIG_ConvertPtr($input, (void**)&s, SWIGTYPE_p__signal, 0))) {
            SWIG_exception_fail(SWIG_TypeError, "in method '$symname', expecting type signal");
            return NULL;
        }
        sa->sigs = (mpr_sig*) malloc(sizeof(mpr_sig));
        sa->sigs[0] = (mpr_sig)s;
    }
    else {
        SWIG_exception_fail(SWIG_TypeError, "in method '$symname', expecting type signal");
        return NULL;
    }
    $1 = sa;
}
%typemap(freearg) (signal_array) {
    if ($1) {
        signal_array sa = (signal_array)$1;
        if (sa->sigs)
            free (sa->sigs);
//        free(sa);
    }
}
%typemap(in) propval %{
    if (1) {
    propval_t *prop = alloca(sizeof(*prop));
    if ($input == Py_None)
        $1 = 0;
    else {
        prop->type = 0;
        check_type($input, &prop->type, 1, 1);
        if (!prop->type) {
            PyErr_SetString(PyExc_ValueError, "Problem determining value type.");
            return NULL;
        }
        if (PyList_Check($input))
            prop->len = PyList_Size($input);
        else
            prop->len = 1;
        prop->val = malloc(prop->len * mpr_type_get_size(prop->type));
        prop->free_val = 1;
        if (py_to_prop($input, prop, 0)) {
            free(prop->val);
            PyErr_SetString(PyExc_ValueError, "Problem parsing property value (1).");
            return NULL;
        }
        $1 = prop;
    }}
%}
%typemap(out) propval {
    if ($1) {
        $result = prop_to_py($1, 0);
        if ($result)
            free($1);
        else {
            $result = Py_None;
            Py_INCREF($result);
        }
    }
    else {
        $result = Py_None;
        Py_INCREF($result);
    }
 }
%typemap(freearg) propval {
    if ($1) {
        propval prop = (propval)$1;
        if (prop->free_val) {
            if (prop->val)
                free(prop->val);
        }
        else
            free(prop);
    }
}
%typemap(typecheck) propval {
    $1 = 1;
}
%typemap(in) named_prop %{
    named_prop_t *prop = alloca(sizeof(*prop));
    if ($input == Py_None)
        $1 = 0;
    else {
        prop->key = 0;
        prop->val.type = 0;
        check_type($input, &prop->val.type, 1, 1);
        if (!prop->val.type) {
            PyErr_SetString(PyExc_ValueError, "Problem determining value type.");
            return NULL;
        }
        if (PyList_Check($input))
            prop->val.len = PyList_Size($input);
        else
            prop->val.len = 1;
        prop->val.val = malloc(prop->val.len * mpr_type_get_size(prop->val.type));
        prop->val.free_val = 1;
        if (py_to_prop($input, &prop->val, &prop->key)) {
            free(prop->val.val);
            PyErr_SetString(PyExc_ValueError, "Problem parsing property value. (2)");
            return NULL;
        }
        $1 = prop;
    }
%}
%typemap(out) named_prop {
    if ($1) {
        named_prop prop = (named_prop)$1;
        $result = prop_to_py(&prop->val, prop->key);
        if ($result)
            free($1);
        else {
            $result = Py_None;
            Py_INCREF($result);
        }
    }
    else {
        $result = Py_None;
        Py_INCREF($result);
    }
}
%typemap(freearg) named_prop {
    if ($1) {
        named_prop prop = (named_prop)$1;
        if (prop->val.free_val) {
            if (prop->val.val)
                free(prop->val.val);
        }
        else
            free(prop);
    }
}
%typemap(in) booltype {
    $1 = PyObject_IsTrue($input);
}
%typemap(out) booltype {
    PyObject *o = $1 ? Py_True : Py_False;
    Py_INCREF(o);
    return o;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_BOOL) booltype {
    $1 = PyBool_Check($input) ? 1 : 0;
}

%{
#include <assert.h>
#include <mapper_internal.h>
static int my_error = 0;

// Note: inet_ntoa() crashes on OS X if this header isn't included!
// On the other hand, doesn't compile on Windows if it is.
#ifdef __APPLE__
#include <arpa/inet.h>
#endif

typedef struct _device {} device;
typedef struct _signal {} signal__;
typedef struct _map {} map;
typedef struct _graph {} graph;
typedef struct _time {} time__;

typedef struct _device_list {
    mpr_list list;
} device_list;

typedef struct _signal_list {
    mpr_list list;
} signal_list;

typedef struct _map_list {
    mpr_list list;
} map_list;

typedef struct {
    void *val;
    int len;
    mpr_type type;
    char free_val;
    char pub;
} propval_t, *propval;

typedef struct {
    const char *key;
    propval_t val;
    mpr_prop prop;
} named_prop_t, *named_prop;

PyThreadState *_save;

static int py_to_prop(PyObject *from, propval prop, const char **key)
{
    // here we are assuming sufficient memory has already been allocated
    if (!from || !prop->len)
        return 1;

    int i;

    switch (prop->type) {
        case MPR_STR:
        {
            // only strings (unicode in py3) are valid
            if (prop->len > 1) {
                char **str_to = (char**)prop->val;
                for (i = 0; i < prop->len; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
#if PY_MAJOR_VERSION >= 3
                    if (PyUnicode_Check(element)) {
                        Py_ssize_t size;
                        str_to[i] = strdup(PyUnicode_AsUTF8AndSize(element, &size));
                        if (!str_to[i]) {
                            return 1;
                        }
                    }
                    else if (PyBytes_Check(element))
                        str_to[i] = strdup(PyBytes_AsString(element));
                    else
                        return 1;
#else
                    if (!PyString_Check(element))
                        return 1;
                    str_to[i] = strdup(PyString_AsString(element));
#endif
                }
            }
            else {
#if PY_MAJOR_VERSION >= 3
                if (PyUnicode_Check(from)) {
                    char **str_to = (char**)&prop->val;
                    Py_ssize_t size;
                    *str_to = strdup(PyUnicode_AsUTF8AndSize(from, &size));
                    if (!*str_to)
                        return 1;
                }
                else if (PyBytes_Check(from)) {
                    char **str_to = (char**)&prop->val;
                    *str_to = strdup(PyBytes_AsString(from));
                }
                else
                    return 1;
#else
                if (!PyString_Check(from))
                    return 1;
                char **str_to = (char**)&prop->val;
                *str_to = strdup(PyString_AsString(from));
#endif
            }
            break;
        }
        case MPR_TYPE:
        {
            // only mpr_type are valid
            mpr_type *type_to = (mpr_type*)prop->val;
            if (prop->len > 1) {
                for (i = 0; i < prop->len; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
                    if (PyInt_Check(element))
                        type_to[i] = (mpr_type)PyInt_AsLong(element);
                    else
                        return 1;
                }
            }
            else {
                if (PyInt_Check(from))
                    *type_to = PyInt_AsLong(from);
                else
                    return 1;
            }
            break;
        }
        case MPR_INT32:
        {
            int *int_to = (int*)prop->val;
            if (prop->len > 1) {
                for (i = 0; i < prop->len; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
                    if (PyInt_Check(element))
                        int_to[i] = PyInt_AsLong(element);
                    else if (PyFloat_Check(element))
                        int_to[i] = (int)PyFloat_AsDouble(element);
                    else if (PyBool_Check(element)) {
                        if (PyObject_IsTrue(element))
                            int_to[i] = 1;
                        else
                            int_to[i] = 0;
                    }
                    else
                        return 1;
                }
            }
            else {
                if (PyInt_Check(from))
                    *int_to = PyInt_AsLong(from);
                else if (PyFloat_Check(from))
                    *int_to = (int)PyFloat_AsDouble(from);
                else if (PyBool_Check(from)) {
                    if (PyObject_IsTrue(from))
                        *int_to = 1;
                    else
                        *int_to = 0;
                }
                else
                    return 1;
            }
            break;
        }
        case MPR_FLT:
        {
            float *float_to = (float*)prop->val;
            if (prop->len > 1) {
                for (i = 0; i < prop->len; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
                    if (PyFloat_Check(element))
                        float_to[i] = PyFloat_AsDouble(element);
                    else if (PyInt_Check(element))
                        float_to[i] = (float)PyInt_AsLong(element);
                    else if (PyBool_Check(element)) {
                        if (PyObject_IsTrue(element))
                            float_to[i] = 1.;
                        else
                            float_to[i] = 0.;
                    }
                    else
                        return 1;
                }
            }
            else {
                if (PyFloat_Check(from))
                    *float_to = PyFloat_AsDouble(from);
                else if (PyInt_Check(from))
                    *float_to = (float)PyInt_AsLong(from);
                else if (PyBool_Check(from)) {
                    if (PyObject_IsTrue(from))
                        *float_to = 1.;
                    else
                        *float_to = 0.;
                }
                else
                    return 1;
            }
            break;
        }
        case MPR_BOOL:
        {
            int *int_to = (int*)prop->val;
            if (prop->len > 1) {
                for (i = 0; i < prop->len; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
                    if (PyInt_Check(element))
                        int_to[i] = PyInt_AsLong(element);
                    else if (PyFloat_Check(element))
                        int_to[i] = (int)PyFloat_AsDouble(element);
                    else if (PyBool_Check(element)) {
                        if (PyObject_IsTrue(element))
                            int_to[i] = 1;
                        else
                            int_to[i] = 0;
                    }
                    else
                        return 1;
                }
            }
            else {
                if (PyInt_Check(from))
                    *int_to = PyInt_AsLong(from);
                else if (PyFloat_Check(from))
                    *int_to = (int)PyFloat_AsDouble(from);
                else if (PyBool_Check(from)) {
                    if (PyObject_IsTrue(from))
                        *int_to = 1;
                    else
                        *int_to = 0;
                }
                else
                    return 1;
            }
            break;
        }
        default:
            return 1;
            break;
    }
    return 0;
}

static int check_type(PyObject *v, mpr_type *t, int can_promote, int allow_sequence)
{
    if (PyBool_Check(v)) {
        if (*t == 0)
            *t = MPR_BOOL;
        else if (*t == MPR_STR)
            return 1;
    }
    else if (PyInt_Check(v) || PyLong_Check(v)) {
        if (*t == 0)
            *t = MPR_INT32;
        else if (*t == MPR_STR)
            return 1;
    }
    else if (PyFloat_Check(v)) {
        if (*t == 0)
            *t = MPR_FLT;
        else if (*t == MPR_STR)
            return 1;
        else if (*t == MPR_INT32 && can_promote)
            *t = MPR_FLT;
    }
    else if (PyString_Check(v) || PyUnicode_Check(v)) {
        if (*t == 0)
            *t = MPR_STR;
        else if (*t != MPR_STR)
            return 1;
    }
    else if (PySequence_Check(v)) {
        if (allow_sequence) {
            int i;
            for (i=0; i<PySequence_Size(v); i++) {
                if (check_type(PySequence_GetItem(v, i), t, can_promote, 0))
                    return 1;
            }
        }
        else
            return 1;
    }
    return 0;
}

static PyObject *prop_to_py(propval prop, const char *key)
{
    if (!prop->len)
        return 0;

    int i;
    PyObject *v = 0;
    if (prop->len > 1)
        v = PyList_New(prop->len);

    switch (prop->type) {
        case MPR_STR:
        {
            if (prop->len > 1) {
                char **vect = (char**)prop->val;
                for (i=0; i<prop->len; i++)
#if PY_MAJOR_VERSION >= 3
                    PyList_SetItem(v, i, PyUnicode_FromString(vect[i]));
#else
                    PyList_SetItem(v, i, PyString_FromString(vect[i]));
#endif
            }
            else
#if PY_MAJOR_VERSION >= 3
                v = PyUnicode_FromString((char*)prop->val);
#else
                v = PyString_FromString((char*)prop->val);
#endif
            break;
        }
        case MPR_TYPE:
        {
            if (prop->len > 1) {
                mpr_type *vect = (mpr_type*)prop->val;
                for (i=0; i<prop->len; i++)
                    PyList_SetItem(v, i, Py_BuildValue("i", (int)vect[i]));
            }
            else
                v = Py_BuildValue("i", (int)(*(mpr_type*)prop->val));
            break;
        }
        case MPR_INT32:
        {
            if (prop->len > 1) {
                int *vect = (int*)prop->val;
                for (i=0; i<prop->len; i++)
                    PyList_SetItem(v, i, Py_BuildValue("i", vect[i]));
            }
            else
                v = Py_BuildValue("i", *(int*)prop->val);
            break;
        }
        case MPR_INT64:
        {
            if (prop->len > 1) {
                int64_t *vect = (int64_t*)prop->val;
                for (i=0; i<prop->len; i++)
                    PyList_SetItem(v, i, Py_BuildValue("l", vect[i]));
            }
            else
                v = Py_BuildValue("l", *(int64_t*)prop->val);
            break;
        }
        case MPR_FLT:
        {
            if (prop->len > 1) {
                float *vect = (float*)prop->val;
                for (i=0; i<prop->len; i++)
                    PyList_SetItem(v, i, Py_BuildValue("f", vect[i]));
            }
            else
                v = Py_BuildValue("f", *(float*)prop->val);
            break;
        }
        case MPR_DBL:
        {
            if (prop->len > 1) {
                double *vect = (double*)prop->val;
                for (i=0; i<prop->len; i++)
                    PyList_SetItem(v, i, Py_BuildValue("d", vect[i]));
            }
            else
                v = Py_BuildValue("d", *(double*)prop->val);
            break;
        }
        case MPR_TIME:
        {
            mpr_time *vect = (mpr_time*)prop->val;
            if (prop->len > 1) {
                for (i=0; i<prop->len; i++) {
                    PyObject *py_tt = SWIG_NewPointerObj(SWIG_as_voidptr(&vect[i]),
                                                         SWIGTYPE_p__time, 0);
                    PyList_SetItem(v, i, Py_BuildValue("O", py_tt));
                }
            }
            else {
                PyObject *py_tt = SWIG_NewPointerObj(SWIG_as_voidptr(vect),
                                                     SWIGTYPE_p__time, 0);
                v = Py_BuildValue("O", py_tt);
            }
            break;
        }
        case MPR_BOOL:
            if (prop->len > 1) {
                int *vect = (int*)prop->val;
                for (i=0; i<prop->len; i++)
                    PyList_SetItem(v, i, PyBool_FromLong(vect[i]));
            }
            else
                v = PyBool_FromLong(*(int*)prop->val);
            break;
        case MPR_DEV:
            if (prop->len == 1) {
                PyObject *py_dev = SWIG_NewPointerObj(SWIG_as_voidptr((mpr_dev)prop->val),
                                                      SWIGTYPE_p__device, 0);
                v = Py_BuildValue("O", py_dev);
            }
            break;
        case MPR_SIG:
            if (prop->len == 1) {
                PyObject *py_sig = SWIG_NewPointerObj(SWIG_as_voidptr((mpr_sig)prop->val),
                                                      SWIGTYPE_p__signal, 0);
                v = Py_BuildValue("O", py_sig);
            }
            break;
        case MPR_MAP:
            if (prop->len == 1) {
                PyObject *py_map = SWIG_NewPointerObj(SWIG_as_voidptr((mpr_map)prop->val),
                                                      SWIGTYPE_p__map, 0);
                v = Py_BuildValue("O", py_map);
            }
            break;
        case MPR_LIST: {
            mpr_list l = (mpr_list)prop->val;
            PyObject *o = Py_None;
            if (l && *l) {
                switch (mpr_obj_get_type((mpr_obj)*l)) {
                    case MPR_DEV: {
                        device_list *ret = malloc(sizeof(struct _device_list));
                        ret->list = l;
                        o = SWIG_NewPointerObj(SWIG_as_voidptr(ret), SWIGTYPE_p__device_list, 0);
                        break;
                    }
                    case MPR_SIG: {
                        signal_list *ret = malloc(sizeof(struct _signal_list));
                        ret->list = l;
                        o = SWIG_NewPointerObj(SWIG_as_voidptr(ret), SWIGTYPE_p__signal_list, 0);
                        break;
                    }
                    case MPR_MAP: {
                        map_list *ret = malloc(sizeof(struct _map_list));
                        ret->list = l;
                        o = SWIG_NewPointerObj(SWIG_as_voidptr(ret), SWIGTYPE_p__map_list, 0);
                        break;
                    }
                    default:
                        printf("[libmapper] unknown list type (prop_to_py).\n");
                        return 0;
                }
            }
            v = Py_BuildValue("O", o);
            break;
        }
        default:
            return 0;
            break;
    }
    if (key) {
        PyObject *o = Py_BuildValue("sO", key, v);
        return o;
    }
    return v;
}

/* Note: We want to call the signal object 'signal', but there is already a
 * function in the C standard library called signal(). Solution is to name it
 * something else (signal__) but still tell SWIG that it is called 'signal',
 * and then use the preprocessor to do replacement. Should work as long as
 * signal() doesn't need to be called from the SWIG wrapper code. */
#define signal signal__
#define time time__

/* Wrapper for callback back to python when a mpr_sig handler is called. */
static void signal_handler_py(mpr_sig sig, mpr_sig_evt e, mpr_id id, int len,
                              mpr_type type, const void *val, mpr_time tt)
{
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject *arglist=0;
    PyObject *vallist=0;
    PyObject *result=0;
    int i;

    PyObject *py_sig = SWIG_NewPointerObj(SWIG_as_voidptr(sig), SWIGTYPE_p__signal, 0);
    PyObject *py_tt = SWIG_NewPointerObj(SWIG_as_voidptr(&tt), SWIGTYPE_p__time, 0);

    if (val) {
        if (type == MPR_INT32) {
            int *vint = (int*)val;
            if (len > 1) {
                vallist = PyList_New(len);
                for (i = 0; i < len; i++) {
                    PyObject *o = Py_BuildValue("i", vint[i]);
                    PyList_SET_ITEM(vallist, i, o);
                }
                arglist = Py_BuildValue("(OiLOO)", py_sig, e, id, vallist, py_tt);
            }
            else
                arglist = Py_BuildValue("(OiLiO)", py_sig, e, id, *(int*)val, py_tt);
        }
        else if (type == MPR_FLT) {
            if (len > 1) {
                float *vfloat = (float*)val;
                vallist = PyList_New(len);
                for (i = 0; i < len; i++) {
                    PyObject *o = Py_BuildValue("f", vfloat[i]);
                    PyList_SET_ITEM(vallist, i, o);
                }
                arglist = Py_BuildValue("(OiLOO)", py_sig, e, id, vallist, py_tt);
            }
            else
                arglist = Py_BuildValue("(OiLfO)", py_sig, e, id, *(float*)val, py_tt);
        }
    }
    else {
        arglist = Py_BuildValue("(OiLOO)", py_sig, e, id, Py_None, py_tt);
    }
    if (!arglist) {
        printf("[libmapper] Could not build arglist (signal_handler_py).\n");
        return;
    }
    PyObject **callbacks = (PyObject**)sig->obj.data;
    result = PyEval_CallObject(callbacks[0], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(vallist);
    Py_XDECREF(result);

    PyGILState_Release(gstate);
}

typedef int booltype;

typedef struct _signal_array {
    mpr_sig *sigs;
    int len;
} signal_array_t, *signal_array;

/* Wrapper for callback back to python when a graph handler is called. */
static void graph_handler_py(mpr_graph g, mpr_obj obj, mpr_graph_evt e, const void *data)
{
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject *py_obj;
    mpr_type type = mpr_obj_get_type(obj);
    switch (type) {
        case MPR_DEV:
            py_obj = SWIG_NewPointerObj(SWIG_as_voidptr(obj), SWIGTYPE_p__device, 0);
            break;
        case MPR_SIG:
            py_obj = SWIG_NewPointerObj(SWIG_as_voidptr(obj), SWIGTYPE_p__signal, 0);
            break;
        case MPR_MAP:
            py_obj = SWIG_NewPointerObj(SWIG_as_voidptr(obj), SWIGTYPE_p__map, 0);
            break;
        default:
            printf("[libmapper] Unknown object type (graph_handler_py).\n");
            return;
    }

    PyObject *arglist = Py_BuildValue("(iOi)", type, py_obj, e);
    if (!arglist) {
        printf("[libmapper] Could not build arglist (graph_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)data, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);

    PyGILState_Release(gstate);
}

static void set_obj_prop(mpr_obj o, mpr_prop p, const char *s, propval v, booltype pub) {
    if (MPR_PROP_DATA == p || (s && !strcmp(s, "data")))
        return;
    if (v)
        mpr_obj_set_prop(o, p, s, v->len, v->type, v->val, pub);
    else
        mpr_obj_remove_prop(o, p, s);
}

static propval get_obj_prop_by_key(mpr_obj obj, const char *key) {
    mpr_prop prop;
    int len, pub;
    mpr_type type;
    const void *val;
    prop = mpr_obj_get_prop_by_key(obj, key, &len, &type, &val, &pub);
    if (MPR_PROP_UNKNOWN == prop || !val)
        return 0;
    if (MPR_PROP_DATA == prop) {
        // don't include user data
        return 0;
    }
    propval pval = malloc(sizeof(propval_t));
    pval->len = len;
    pval->type = type;
    pval->val = (void*)val;
    pval->free_val = 0;
    pval->pub = pub;
    return pval;
}

static named_prop get_obj_prop_by_idx(mpr_obj obj, int idx) {
    mpr_prop prop;
    const char *key;
    int len, pub;
    mpr_type type;
    const void *val;
    prop = mpr_obj_get_prop_by_idx(obj, idx, &key, &len, &type, &val, &pub);
    if (MPR_PROP_UNKNOWN == prop)
        return 0;
    if (MPR_PROP_DATA == prop) {
        // don't include user data
        return 0;
    }
    named_prop named = malloc(sizeof(named_prop_t));
    named->prop = prop;
    named->key = key;
    named->val.len = len;
    named->val.type = type;
    named->val.val = (void*)val;
    named->val.free_val = 0;
    named->val.pub = pub;
    return named;
}

%}

/*! Possible data types. */
%constant int INT32                     = MPR_INT32;
%constant int INT64                     = MPR_INT64;
%constant int FLT                       = MPR_FLT;
%constant int DBL                       = MPR_DBL;
%constant int STR                       = MPR_STR;
%constant int BOOL                      = MPR_BOOL;
%constant int TIME                      = MPR_TIME;
%constant int TYPE                      = MPR_TYPE;
%constant int PTR                       = MPR_PTR;
%constant int DEV                       = MPR_DEV;
%constant int SIG                       = MPR_SIG;
%constant int SIG_IN                    = MPR_SIG_IN;
%constant int SIG_OUT                   = MPR_SIG_OUT;
%constant int MAP                       = MPR_MAP;
%constant int MAP_IN                    = MPR_MAP_IN;
%constant int MAP_OUT                   = MPR_MAP_OUT;
%constant int OBJ                       = MPR_OBJ;
%constant int LIST                      = MPR_LIST;
%constant int NULL                      = MPR_NULL;

/*! Symbolic representation of recognized properties. */
%constant int PROP_UNKNOWN              = MPR_PROP_UNKNOWN;
%constant int PROP_CALIB                = MPR_PROP_CALIB;
%constant int PROP_DEV                  = MPR_PROP_DEV;
%constant int PROP_DIR                  = MPR_PROP_DIR;
%constant int PROP_EXPR                 = MPR_PROP_EXPR;
%constant int PROP_HOST                 = MPR_PROP_HOST;
%constant int PROP_ID                   = MPR_PROP_ID;
%constant int PROP_INST                 = MPR_PROP_INST;
%constant int PROP_IS_LOCAL             = MPR_PROP_IS_LOCAL;
%constant int PROP_JITTER               = MPR_PROP_JITTER;
%constant int PROP_LEN                  = MPR_PROP_LEN;
%constant int PROP_LIBVER               = MPR_PROP_LIBVER;
%constant int PROP_LINKED               = MPR_PROP_LINKED;
%constant int PROP_MAX                  = MPR_PROP_MAX;
%constant int PROP_MIN                  = MPR_PROP_MIN;
%constant int PROP_MUTED                = MPR_PROP_MUTED;
%constant int PROP_NAME                 = MPR_PROP_NAME;
%constant int PROP_NUM_INST             = MPR_PROP_NUM_INST;
%constant int PROP_NUM_MAPS             = MPR_PROP_NUM_MAPS;
%constant int PROP_NUM_MAPS_IN          = MPR_PROP_NUM_MAPS_IN;
%constant int PROP_NUM_MAPS_OUT         = MPR_PROP_NUM_MAPS_OUT;
%constant int PROP_NUM_SIGS_IN          = MPR_PROP_NUM_SIGS_IN;
%constant int PROP_NUM_SIGS_OUT         = MPR_PROP_NUM_SIGS_OUT;
%constant int PROP_ORDINAL              = MPR_PROP_ORDINAL;
%constant int PROP_PERIOD               = MPR_PROP_PERIOD;
%constant int PROP_PORT                 = MPR_PROP_PORT;
%constant int PROP_PROCESS_LOC          = MPR_PROP_PROCESS_LOC;
%constant int PROP_PROTOCOL             = MPR_PROP_PROTOCOL;
%constant int PROP_RATE                 = MPR_PROP_RATE;
%constant int PROP_SCOPE                = MPR_PROP_SCOPE;
%constant int PROP_SIG                  = MPR_PROP_SIG;
%constant int PROP_SLOT                 = MPR_PROP_SLOT;
%constant int PROP_STATUS               = MPR_PROP_STATUS;
%constant int PROP_STEAL_MODE           = MPR_PROP_STEAL_MODE;
%constant int PROP_SYNCED               = MPR_PROP_SYNCED;
%constant int PROP_TYPE                 = MPR_PROP_TYPE;
%constant int PROP_UNIT                 = MPR_PROP_UNIT;
%constant int PROP_USE_INST             = MPR_PROP_USE_INST;
%constant int PROP_DATA                 = MPR_PROP_DATA;
%constant int PROP_VERSION              = MPR_PROP_VERSION;
%constant int PROP_EXTRA                = MPR_PROP_EXTRA;

/*! Possible operations for composing graph queries. */
%constant int OP_NEX                    = MPR_OP_NEX;
%constant int OP_EQ                     = MPR_OP_EQ;
%constant int OP_EX                     = MPR_OP_EX;
%constant int OP_GT                     = MPR_OP_GT;
%constant int OP_GTE                    = MPR_OP_GTE;
%constant int OP_LT                     = MPR_OP_LT;
%constant int OP_LTE                    = MPR_OP_LTE;
%constant int OP_NEQ                    = MPR_OP_NEQ;

/*! Describes the possible locations for map stream processing. */
%constant int LOC_UNDEFINED             = MPR_LOC_UNDEFINED;
%constant int LOC_SRC                   = MPR_LOC_SRC;
%constant int LOC_DST                   = MPR_LOC_DST;
%constant int LOC_ANY                   = MPR_LOC_ANY;

/*! Describes the possible network protocol for map communication. */
%constant int PROTO_UNDEFINED           = MPR_PROTO_UNDEFINED;
%constant int PROTO_UDP                 = MPR_PROTO_UDP;
%constant int PROTO_TCP                 = MPR_PROTO_TCP;

/*! The set of possible directions for a signal. */
%constant int DIR_UNDEFINED             = MPR_DIR_UNDEFINED;
%constant int DIR_IN                    = MPR_DIR_IN;
%constant int DIR_OUT                   = MPR_DIR_OUT;
%constant int DIR_ANY                   = MPR_DIR_ANY;

/*! The set of possible actions on a signal, used to register callbacks to
 *  inform them of what is happening. */
%constant int SIG_INST_NEW              = MPR_SIG_INST_NEW;
%constant int SIG_REL_UPSTRM            = MPR_SIG_REL_UPSTRM;
%constant int SIG_REL_DNSTRM            = MPR_SIG_REL_DNSTRM;
%constant int SIG_INST_OFLW             = MPR_SIG_INST_OFLW;
%constant int SIG_UPDATE                = MPR_SIG_UPDATE;
%constant int SIG_ALL                   = MPR_SIG_ALL;

/*! Describes the voice-stealing mode for instances. */
%constant int STEAL_NONE                = MPR_STEAL_NONE;
%constant int STEAL_OLDEST              = MPR_STEAL_OLDEST;
%constant int STEAL_NEWEST              = MPR_STEAL_NEWEST;

/*! The set of possible events for a graph record, used to inform callbacks
 *  of what is happening to a record. */
%constant int OBJ_NEW                   = MPR_OBJ_NEW;
%constant int OBJ_MOD                   = MPR_OBJ_MOD;
%constant int OBJ_REM                   = MPR_OBJ_REM;
%constant int OBJ_EXP                   = MPR_OBJ_EXP;

%constant int STATUS_RESERVED           = MPR_STATUS_RESERVED;
%constant int STATUS_STAGED             = MPR_STATUS_STAGED;
%constant int STATUS_READY              = MPR_STATUS_READY;
%constant int STATUS_ACTIVE             = MPR_STATUS_ACTIVE;

%constant char* version                 = PACKAGE_VERSION;

typedef struct _device {} device;
typedef struct _signal {} signal;
typedef struct _map {} map;
typedef struct _graph {} graph;
typedef struct _time {} time;

typedef struct _device_list {
    mpr_list list;
} device_list;

%exception _device_list::next {
    assert(!my_error);
    $action
    if (my_error) {
        my_error = 0;
        PyErr_SetString(PyExc_StopIteration, "End of list");
        return NULL;
    }
}

%extend _device_list {
    _device_list(const device_list *orig) {
        struct _device_list *d = malloc(sizeof(struct _device_list));
        d->list = mpr_list_get_cpy(orig->list);
        return d;
    }
    ~_device_list() {
        mpr_list_free($self->list);
        free($self);
    }
    struct _device_list *__iter__() {
        return $self;
    }
    device *next() {
        mpr_obj result = 0;
        if ($self->list) {
            result = *($self->list);
            $self->list = mpr_list_get_next($self->list);
        }
        if (result)
            return (device*)result;
        my_error = 1;
        return NULL;
    }
    device_list *join(device_list *d) {
        if (!d || !d->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(d->list);
        $self->list = mpr_list_get_union($self->list, cpy);
        return $self;
    }
    device_list *intersect(device_list *d) {
        if (!d || !d->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(d->list);
        $self->list = mpr_list_get_isect($self->list, cpy);
        return $self;
    }
    device_list *subtract(device_list *d) {
        if (!d || !d->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(d->list);
        $self->list = mpr_list_get_diff($self->list, cpy);
        return $self;
    }
    device_list *filter(const char *key, propval val=0, mpr_op op=MPR_OP_EQ) {
        if (key && val) {
            $self->list = mpr_list_filter($self->list, MPR_PROP_UNKNOWN, key,
                                          val->len, val->type, val->val, op);
        }
        return $self;
    }
    device_list *filter(int prop, propval val=0, mpr_op op=MPR_OP_EQ) {
        if (val) {
            $self->list = mpr_list_filter($self->list, prop, NULL, val->len,
                                          val->type, val->val, op);
        }
        return $self;
    }
    int length() {
        return mpr_list_get_size($self->list);
    }
    device *__getitem__(int index) {
        // python lists allow negative indexes
        if (index < 0)
            index += mpr_list_get_size($self->list);
        return (device*)mpr_list_get_idx($self->list, index);
    }
    %pythoncode {
        def __next__(self):
            return self.next()
        def __len__(self):
            return self.length()
    }
}

%extend _device {
    _device(const char *name, graph *DISOWN=0) {
        device *d = (device*)mpr_dev_new(name, (mpr_graph) DISOWN);
        return d;
    }
    ~_device() {
        PyObject **callbacks = (PyObject**)((mpr_obj)$self)->data;
        if (callbacks) {
            if (callbacks[0])
                Py_XDECREF(callbacks[0]);
            if (callbacks[1])
                Py_XDECREF(callbacks[1]);
            free(callbacks);
        }
        mpr_dev_free((mpr_dev)$self);
    }
    graph *graph() {
        return (graph*)mpr_obj_get_graph((mpr_obj)$self);
    }

    // functions
    /* Note, these functions return memory which is _not_ owned by Python.
     * Correspondingly, the SWIG default is to set thisown to False, which is
     * correct for this case. */
    signal *add_signal(int dir, const char *name, int len=1, char type=MPR_FLT,
                       const char *unit=0, propval min=0, propval max=0,
                       propval num_inst=0, PyObject *PyFunc=0,
                       int events=MPR_SIG_ALL)
    {
        void *h = 0;
        PyObject **callbacks = 0;
        if (PyFunc) {
            h = signal_handler_py;
            callbacks = malloc(2 * sizeof(PyObject*));
            callbacks[0] = PyFunc;
            callbacks[1] = 0;
            Py_INCREF(PyFunc);
        }
        int *pnum_inst = num_inst && MPR_INT32 == num_inst->type ? num_inst->val : 0;
        mpr_sig sig = mpr_sig_new((mpr_dev)$self, dir, name, len, type, unit,
                                  NULL, NULL, pnum_inst, h, events);
        if (sig) {
            sig->obj.data = callbacks;
            if (max)
                mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_MAX, NULL, max->len, max->type, max->val, 1);
            if (min)
                mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_MIN, NULL, min->len, min->type, min->val, 1);
        }
        return (signal*)sig;
    }
    device *remove_signal(signal *sig) {
        mpr_sig msig = (mpr_sig)sig;
        if (msig->obj.data) {
            PyObject **callbacks = msig->obj.data;
            Py_XDECREF(callbacks[0]);
            Py_XDECREF(callbacks[1]);
            free(callbacks);
        }
        mpr_sig_free(msig);
        return $self;
    }

    device *update_maps() {
        mpr_dev_update_maps((mpr_dev)$self);
        return $self;
    }

    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mpr_dev_poll((mpr_dev)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }

    // queue management
    time *get_time() {
        mpr_time *tt = (mpr_time*)malloc(sizeof(mpr_time));
        mpr_time_set(tt, mpr_dev_get_time((mpr_dev)$self));
        return (time*)tt;
    }
    device *set_time(time *py_tt) {
        mpr_time *tt = (mpr_time*)py_tt;
        mpr_dev_set_time((mpr_dev)$self, *tt);
        return $self;
    }

    mpr_id generate_unique_id() {
        return mpr_dev_generate_unique_id((mpr_dev)$self);
    }

    // property getters
    int get_num_properties() {
        return mpr_obj_get_num_props((mpr_obj)$self, 0);
    }
    booltype get_is_ready() {
        return mpr_dev_get_is_ready((mpr_dev)$self);
    }
    propval get_property(const char *key) {
        return get_obj_prop_by_key((mpr_obj)$self, key);
    }
    named_prop get_property(int idx) {
        return get_obj_prop_by_idx((mpr_obj)$self, idx);
    }

    // property setters
    device *set_property(const char *key, propval val=0, booltype publish=1) {
        set_obj_prop((mpr_obj)$self, MPR_PROP_UNKNOWN, key, val, publish);
        return $self;
    }
    device *set_property(int idx, propval val=0, booltype publish=1) {
        set_obj_prop((mpr_obj)$self, idx, NULL, val, publish);
        return $self;
    }
    device *remove_property(const char *key) {
        if (key && strcmp(key, "data"))
            mpr_obj_remove_prop((mpr_obj)$self, MPR_PROP_UNKNOWN, key);
        return $self;
    }
    device *remove_property(int idx) {
        if (MPR_PROP_UNKNOWN != idx && MPR_PROP_DATA != idx)
            mpr_obj_remove_prop((mpr_obj)$self, idx, NULL);
        return $self;
    }
    device *push() {
        mpr_obj_push((mpr_obj)$self);
        return $self;
    }

    // signal getter
    signal_list *signals(int dir=MPR_DIR_ANY) {
        signal_list *ret = malloc(sizeof(struct _signal_list));
        ret->list = mpr_dev_get_sigs((mpr_dev)$self, dir);
        return ret;
    }

    // map getter
    map_list *maps(int dir=MPR_DIR_ANY) {
        map_list *ret = malloc(sizeof(struct _map_list));
        ret->list = mpr_dev_get_maps((mpr_dev)$self, dir);
        return ret;
    }

    %pythoncode {
        num_properties = property(get_num_properties)
        ready = property(get_is_ready)
        def get_properties(self):
            props = {}
            for i in range(self.num_properties):
                prop = self.get_property(i)
                if prop:
                    props[prop[0]] = prop[1];
            return props
        def __propgetter(self):
            device = self
            props = self.get_properties()
            class propsetter(dict):
                __getitem__ = props.__getitem__
                def __setitem__(self, key, val):
                    props[key] = val
                    device.set_property(key, val)
            return propsetter(self.get_properties())
        properties = property(__propgetter)
        def set_properties(self, props):
            [self.set_property(k, props[k]) for k in props]
        def __getitem__(self, key):
            return self.get_property(key)
        def __setitem__(self, key, val):
            self.set_property(key, val)
            return self;
        def __nonzero__(self):
            return False if self.this is None else True
        def __eq__(self, rhs):
            return rhs != None and self['id'] == rhs['id']
    }
}

typedef struct _signal_list {
    mpr_list list;
} signal_list;

%exception _signal_list::next {
    assert(!my_error);
    $action
    if (my_error) {
        my_error = 0;
        PyErr_SetString(PyExc_StopIteration, "End of list");
        return NULL;
    }
}

%extend _signal_list {
    _signal_list(const signal_list *orig) {
        struct _signal_list *s = malloc(sizeof(struct _signal_list));
        s->list = mpr_list_get_cpy(orig->list);
        return s;
    }
    ~_signal_list() {
        mpr_list_free($self->list);
        free($self);
    }
    struct _signal_list *__iter__() {
        return $self;
    }
    signal *next() {
        mpr_obj result = 0;
        if ($self->list) {
            result = *($self->list);
            $self->list = mpr_list_get_next($self->list);
        }
        if (result)
            return (signal*)result;
        my_error = 1;
        return NULL;
    }
    signal_list *join(signal_list *s) {
        if (!s || !s->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(s->list);
        $self->list = mpr_list_get_union($self->list, cpy);
        return $self;
    }
    signal_list *intersect(signal_list *s) {
        if (!s || !s->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(s->list);
        $self->list = mpr_list_get_isect($self->list, cpy);
        return $self;
    }
    signal_list *subtract(signal_list *s) {
        if (!s || !s->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(s->list);
        $self->list = mpr_list_get_diff($self->list, cpy);
        return $self;
    }
    signal_list *filter(const char *key, propval val=0, mpr_op op=MPR_OP_EQ) {
        if (key && val) {
            $self->list = mpr_list_filter($self->list, MPR_PROP_UNKNOWN, key,
                                          val->len, val->type, val->val, op);
        }
        return $self;
    }
    signal_list *filter(int prop, propval val=0, mpr_op op=MPR_OP_EQ) {
        if (val) {
            $self->list = mpr_list_filter($self->list, prop, NULL, val->len,
                                          val->type, val->val, op);
        }
        return $self;
    }
    int length() {
        return mpr_list_get_size($self->list);
    }
    signal *__getitem__(int index) {
        // python lists allow negative indexes
        if (index < 0)
            index += mpr_list_get_size($self->list);
        return (signal*)mpr_list_get_idx($self->list, index);
    }
    %pythoncode {
        def __next__(self):
            return self.next()
        def __len__(self):
            return self.length()
    }
}

%extend _signal {
    device *device() {
        return (device*)mpr_sig_get_dev((mpr_sig)$self);
    }

    // functions
    int instance_id(int idx, int status=MPR_STATUS_ACTIVE) {
        return mpr_sig_get_inst_id((mpr_sig)$self, idx, status);
    }
    signal *release_instance(int id) {
        mpr_sig_release_inst((mpr_sig)$self, id);
        return $self;
    }
    signal *remove_instance(int id) {
        mpr_sig_remove_inst((mpr_sig)$self, id);
        return $self;
    }
    signal *reserve_instances(int num=1) {
        mpr_sig_reserve_inst((mpr_sig)$self, num, 0, 0);
        return $self;
    }
    signal *reserve_instances(int num_ids, mpr_id *argv) {
        mpr_sig_reserve_inst((mpr_sig)$self, num_ids, argv, 0);
        return $self;
    }
    signal *set_callback(PyObject *PyFunc=0, int events=MPR_SIG_UPDATE) {
        mpr_sig_handler *h = 0;
        mpr_sig sig = (mpr_sig)$self;
        PyObject **callbacks = (PyObject**)sig->obj.data;
        if (PyFunc) {
            h = signal_handler_py;
            if (callbacks) {
                callbacks[0] = PyFunc;
            }
            else {
                callbacks = malloc(2 * sizeof(PyObject*));
                callbacks[0] = PyFunc;
                callbacks[1] = 0;
            }
            Py_INCREF(PyFunc);
        }
        else if (callbacks) {
            Py_XDECREF(callbacks[0]);
            if (callbacks[1]) {
                callbacks[0] = 0;
            }
            else {
                free(callbacks);
                callbacks = 0;
            }
        }
        sig->obj.data = callbacks;
        mpr_sig_set_cb(sig, h, events);
        return $self;
    }
    signal *set_value(propval val=0) {
        mpr_sig sig = (mpr_sig)$self;
        if (!val) {
            mpr_sig_set_value(sig, 0, 0, MPR_NULL, NULL);
            return $self;
        }
        mpr_sig_set_value(sig, 0, val->len, val->type, val->val);
        return $self;
    }
    signal *set_value(int id, propval val=0) {
        mpr_sig sig = (mpr_sig)$self;
        if (!val) {
            mpr_sig_set_value(sig, 0, 0, MPR_NULL, NULL);
            return $self;
        }
        mpr_sig_set_value(sig, id, val->len, val->type, val->val);
        return $self;
    }

    // property getters
    int get_num_properties() {
        return mpr_obj_get_num_props((mpr_obj)$self, 0);
    }
    int num_instances(int status=MPR_STATUS_ACTIVE) {
        return mpr_sig_get_num_inst((mpr_sig)$self, status);
    }
    propval get_property(const char *key) {
        return get_obj_prop_by_key((mpr_obj)$self, key);
    }
    named_prop get_property(int idx) {
        return get_obj_prop_by_idx((mpr_obj)$self, idx);
    }
    propval value() {
        mpr_sig sig = (mpr_sig)$self;
        const void *val = mpr_sig_get_value(sig, 0, 0);
        if (!val)
            return 0;
        propval prop = malloc(sizeof(propval_t));
        prop->len = sig->len;
        prop->type = sig->type;
        prop->val = (void*)val;
        prop->pub = 1;
        return prop;
    }
    propval value(int id) {
        mpr_sig sig = (mpr_sig)$self;
        const void *val = mpr_sig_get_value(sig, id, 0);
        if (!val)
            return 0;
        propval prop = malloc(sizeof(propval_t));
        prop->len = sig->len;
        prop->type = sig->type;
        prop->val = (void*)val;
        prop->pub = 1;
        return prop;
    }

    // property setters
    signal *set_property(const char *key, propval val=0, booltype publish=1) {
        set_obj_prop((mpr_obj)$self, MPR_PROP_UNKNOWN, key, val, publish);
        return $self;
    }
    signal *set_property(int idx, propval val=0, booltype publish=1) {
        set_obj_prop((mpr_obj)$self, idx, NULL, val, publish);
        return $self;
    }
    signal *remove_property(const char *key) {
        if (key && strcmp(key, "data"))
            mpr_obj_remove_prop((mpr_obj)$self, MPR_PROP_UNKNOWN, key);
        return $self;
    }
    signal *remove_property(int idx) {
        if (MPR_PROP_UNKNOWN != idx && MPR_PROP_DATA != idx)
            mpr_obj_remove_prop((mpr_obj)$self, idx, NULL);
        return $self;
    }
    signal *push() {
        mpr_obj_push((mpr_obj)$self);
        return $self;
    }

    map_list *maps(int dir=MPR_DIR_ANY) {
        map_list *ret = malloc(sizeof(struct _map_list));
        ret->list = mpr_sig_get_maps((mpr_sig)$self, dir);
        return ret;
    }

    %pythoncode {
        num_properties = property(get_num_properties)
        def get_properties(self):
            props = {}
            for i in range(self.num_properties):
                prop = self.get_property(i)
                if prop:
                    props[prop[0]] = prop[1];
            return props
        def __propgetter(self):
            signal = self
            props = self.get_properties()
            class propsetter(dict):
                __getitem__ = props.__getitem__
                def __setitem__(self, key, val):
                    props[key] = val
                    signal.set_property(key, val)
            return propsetter(self.get_properties())
        properties = property(__propgetter)
        def set_properties(self, props):
            [self.set_property(k, props[k]) for k in props]
        def __getitem__(self, key):
            return self.get_property(key)
        def __setitem__(self, key, val):
            self.set_property(key, val)
            return self;
        def __nonzero__(self):
            return False if self.this is None else True
        def __eq__(self, rhs):
            return rhs != None and self['id'] == rhs['id']
    }
}

typedef struct _map_list {
    mpr_list list;
} map_list;

%exception _map_list::next {
    assert(!my_error);
    $action
    if (my_error) {
        my_error = 0;
        PyErr_SetString(PyExc_StopIteration, "End of list");
        return NULL;
    }
}

%extend _map_list {
    _map_list(const map_list *orig) {
        struct _map_list *mq = malloc(sizeof(struct _map_list));
        mq->list = mpr_list_get_cpy(orig->list);
        return mq;
    }
    ~_map_list() {
        mpr_list_free($self->list);
        free($self);
    }
    struct _map_list *__iter__() {
        return $self;
    }
    map *next() {
        mpr_obj result = 0;
        if ($self->list) {
            result = *($self->list);
            $self->list = mpr_list_get_next($self->list);
        }
        if (result)
            return (map*)result;
        my_error = 1;
        return NULL;
    }
    map_list *join(map_list *m) {
        if (!m || !m->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(m->list);
        $self->list = mpr_list_get_union($self->list, cpy);
        return $self;
    }
    map_list *intersect(map_list *m) {
        if (!m || !m->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(m->list);
        $self->list = mpr_list_get_isect($self->list, cpy);
        return $self;
    }
    map_list *subtract(map_list *m) {
        if (!m || !m->list)
            return $self;
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy(m->list);
        $self->list = mpr_list_get_diff($self->list, cpy);
        return $self;
    }
    map_list *filter(const char *key, propval val=0, mpr_op op=MPR_OP_EQ) {
        if (key && val) {
            $self->list = mpr_list_filter($self->list, MPR_PROP_UNKNOWN, key,
                                          val->len, val->type, val->val, op);
        }
        return $self;
    }
    map_list *filter(int prop, propval val=0, mpr_op op=MPR_OP_EQ) {
        if (val) {
            $self->list = mpr_list_filter($self->list, prop, NULL, val->len,
                                          val->type, val->val, op);
        }
        return $self;
    }
    int length() {
        return mpr_list_get_size($self->list);
    }
    map *__getitem__(int index) {
        // python lists allow negative indexes
        if (index < 0)
            index += mpr_list_get_size($self->list);
        return (map*)mpr_list_get_idx($self->list, index);
    }
    map_list *release() {
        // need to use a copy of query
        mpr_obj *cpy = mpr_list_get_cpy($self->list);
        while (cpy) {
            mpr_map_release((mpr_map)*cpy);
            cpy = mpr_list_get_next(cpy);
        }
        return $self;
    }
    %pythoncode {
        def __next__(self):
            return self.next()
        def __len__(self):
            return self.length()
    }
}

%extend _map {
    _map(signal_array srcs=0, signal_array dsts=0) {
        if (!srcs || !dsts)
            return (map*)0;
        return (map*)mpr_map_new(srcs->len, srcs->sigs, dsts->len, dsts->sigs);
    }
    _map(const char *str, signal *sig0, signal *sig1, signal *sig2=NULL, signal *sig3=NULL,
         signal *sig4=NULL, signal *sig5=NULL, signal *sig6=NULL, signal *sig7=NULL,
         signal *sig8=NULL, signal *sig9=NULL) {
        if (!str || !sig0 || !sig1)
            return (map*)0;
        return (map*)mpr_map_new_from_str(str, sig0, sig1, sig2, sig3, sig4,
                                          sig5, sig6, sig7, sig8, sig9);
    }
    ~_map() {
        ;
    }

    map *refresh() {
        mpr_map_refresh((mpr_map)$self);
        return $self;
    }
    void release() {
        mpr_map_release((mpr_map)$self);
    }

    // signal getter
    signal_list *signals(int loc=MPR_LOC_ANY) {
        signal_list *ret = malloc(sizeof(struct _signal_list));
        ret->list = mpr_map_get_sigs((mpr_map)$self, loc);
        return ret;
    }
    signal *signal(int idx, int loc=MPR_LOC_ANY) {
        return (signal*)mpr_map_get_sig((mpr_map)$self, idx, loc);
    }
    int index(signal *sig) {
        return mpr_map_get_sig_idx((mpr_map)$self, (mpr_sig)sig);
    }

    // scopes
    map *add_scope(device *dev) {
        mpr_map_add_scope((mpr_map)$self, (mpr_dev)dev);
        return $self;
    }
    map *remove_scope(device *dev) {
        mpr_map_remove_scope((mpr_map)$self, (mpr_dev)dev);
        return $self;
    }

    // property getters
    int get_num_properties() {
        return mpr_obj_get_num_props((mpr_obj)$self, 0);
    }
    booltype get_ready() {
        return mpr_map_get_is_ready((mpr_map)$self);
    }
    propval get_property(const char *key) {
        return get_obj_prop_by_key((mpr_obj)$self, key);
    }
    named_prop get_property(int idx) {
        return get_obj_prop_by_idx((mpr_obj)$self, idx);
    }

    // property setters
    map *set_property(const char *key, propval val=0, booltype publish=1) {
        set_obj_prop((mpr_obj)$self, MPR_PROP_UNKNOWN, key, val, publish);
        return $self;
    }
    map *set_property(int idx, propval val=0, booltype publish=1) {
        set_obj_prop((mpr_obj)$self, idx, NULL, val, publish);
        return $self;
    }
    map *remove_property(const char *key) {
        if (key && strcmp(key, "data"))
            mpr_obj_remove_prop((mpr_obj)$self, MPR_PROP_UNKNOWN, key);
        return $self;
    }
    map *remove_property(int idx) {
        if (MPR_PROP_UNKNOWN != idx && MPR_PROP_DATA != idx)
            mpr_obj_remove_prop((mpr_obj)$self, idx, NULL);
        return $self;
    }
    map *push() {
        mpr_obj_push((mpr_obj)$self);
        return $self;
    }

    %pythoncode {
        num_properties = property(get_num_properties)
        ready = property(get_ready)
        def get_properties(self):
            props = {}
            for i in range(self.num_properties):
                prop = self.get_property(i)
                if prop:
                    props[prop[0]] = prop[1];
            return props
        def __propgetter(self):
            map = self
            props = self.get_properties()
            class propsetter(dict):
                __getitem__ = props.__getitem__
                def __setitem__(self, key, val):
                    props[key] = val
                    map.set_property(key, val, true)
            return propsetter(self.get_properties())
        properties = property(__propgetter)
        def set_properties(self, props):
            [self.set_property(k, props[k]) for k in props]
        def __getitem__(self, key):
            return self.get_property(key)
        def __setitem__(self, key, val):
            self.set_property(key, val)
            return self;
        def __nonzero__(self):
            return False if self.this is None else True
        def __eq__(self, rhs):
            return rhs != None and self['id'] == rhs['id']
    }
}

%extend _graph {
    _graph(int flags=0x00) {
        return (graph*)mpr_graph_new(flags);
    }
    ~_graph() {
        mpr_graph_free((mpr_graph)$self);
    }
    const char *get_interface() {
        return mpr_graph_get_interface((mpr_graph)$self);
    }
    graph *set_interface(const char *iface) {
        mpr_graph_set_interface((mpr_graph)$self, iface);
        return $self;
    }
    const char *get_address() {
        return mpr_graph_get_address((mpr_graph)$self);
    }
    graph *set_address(const char *group, int port) {
        mpr_graph_set_address((mpr_graph)$self, group, port);
        return $self;
    }
    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mpr_graph_poll((mpr_graph)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }
    graph *subscribe(device *dev, int subscribe_flags, int timeout=-1) {
        mpr_graph_subscribe((mpr_graph)$self, (mpr_dev)dev, subscribe_flags,
                            timeout);
        return $self;
    }
    graph *subscribe(int subscribe_flags, int timeout=-1) {
        mpr_graph_subscribe((mpr_graph)$self, 0, subscribe_flags, timeout);
        return $self;
    }
    graph *unsubscribe(device *dev=0) {
        mpr_graph_unsubscribe((mpr_graph)$self, (mpr_dev)dev);
        return $self;
    }
    graph *add_callback(PyObject *PyFunc, int type_flags=MPR_OBJ) {
        if (mpr_graph_add_cb((mpr_graph)$self, graph_handler_py, type_flags, PyFunc))
            Py_XINCREF(PyFunc);
        return $self;
    }
    graph *remove_callback(PyObject *PyFunc) {
        if (mpr_graph_remove_cb((mpr_graph)$self, graph_handler_py, PyFunc))
            Py_XDECREF(PyFunc);
        return $self;
    }
    device_list *devices() {
        device_list *ret = malloc(sizeof(struct _device_list));
        ret->list = mpr_graph_get_objs((mpr_graph)$self, MPR_DEV);
        return ret;
    }
    signal_list *signals() {
        signal_list *ret = malloc(sizeof(struct _signal_list));
        ret->list = mpr_graph_get_objs((mpr_graph)$self, MPR_SIG);
        return ret;
    }
    map_list *maps() {
        map_list *ret = malloc(sizeof(struct _map_list));
        ret->list = mpr_graph_get_objs((mpr_graph)$self, MPR_MAP);
        return ret;
    }
    %pythoncode {
        interface = property(get_interface, set_interface)
        address = property(get_address, set_address)
        def __nonzero__(self):
            return False if self.this is None else True
    }
}

%extend _time {
    _time() {
        mpr_time *tt = (mpr_time*)malloc(sizeof(mpr_time));
        mpr_time_set(tt, MPR_NOW);
        return (time*)tt;
    }
    _time(double val) {
        mpr_time *tt = malloc(sizeof(mpr_time));
        mpr_time_set_dbl(tt, val);
        return (time*)tt;
    }
    ~_time() {
        free((mpr_time*)$self);
    }
    time *now() {
        mpr_time_set((mpr_time*)$self, MPR_NOW);
        return $self;
    }
    double get_double() {
        return mpr_time_as_dbl(*(mpr_time*)$self);
    }
    time *__add__(time *addend) {
        mpr_time *tt = malloc(sizeof(mpr_time));
        mpr_time_set(tt, *(mpr_time*)$self);
        mpr_time_add(tt, *(mpr_time*)addend);
        return (time*)tt;
    }
    time *__add__(double addend) {
        mpr_time *tt = malloc(sizeof(mpr_time));
        mpr_time_set(tt, *(mpr_time*)$self);
        mpr_time_add_dbl(tt, addend);
        return (time*)tt;
    }
    time *__iadd__(time *addend) {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time_add(tt, *(mpr_time*)addend);
        return $self;
    }
    time *__iadd__(double addend) {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time_add_dbl(tt, addend);
        return $self;
    }
    double __radd__(double val) {
        return val + mpr_time_as_dbl(*(mpr_time*)$self);
    }
    time *__sub__(time *subtrahend) {
        mpr_time *tt = malloc(sizeof(mpr_time));
        mpr_time_set(tt, *(mpr_time*)$self);
        mpr_time_sub(tt, *(mpr_time*)subtrahend);
        return (time*)tt;
    }
    time *__sub__(double subtrahend) {
        mpr_time *tt = malloc(sizeof(mpr_time));
        mpr_time_set(tt, *(mpr_time*)$self);
        mpr_time_add_dbl(tt, -subtrahend);
        return (time*)tt;
    }
    time *__isub__(time *subtrahend) {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time_sub(tt, *(mpr_time*)subtrahend);
        return $self;
    }
    time *__isub__(double subtrahend) {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time_add_dbl(tt, -subtrahend);
        return $self;
    }
    double __rsub__(double val) {
        return val - mpr_time_as_dbl(*(mpr_time*)$self);
    }
    time *__mul__(double multiplicand) {
        mpr_time *tt = malloc(sizeof(mpr_time));
        mpr_time_set(tt, *(mpr_time*)$self);
        mpr_time_mul(tt, multiplicand);
        return (time*)tt;
    }
    time *__imul__(double multiplicand) {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time_mul(tt, multiplicand);
        return $self;
    }
    double __rmul__(double val) {
        return val + mpr_time_as_dbl(*(mpr_time*)$self);
    }
    time *__div__(double divisor) {
        mpr_time *tt = malloc(sizeof(mpr_time));
        mpr_time_set(tt, *(mpr_time*)$self);
        mpr_time_mul(tt, 1/divisor);
        return (time*)tt;
    }
    time *__idiv__(double divisor) {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time_mul(tt, 1/divisor);
        return $self;
    }
    double __rdiv__(double val) {
        return val / mpr_time_as_dbl(*(mpr_time*)$self);
    }

    booltype __lt__(time *rhs) {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time *rhs_tt = (mpr_time*)rhs;
        return (tt->sec < rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac < rhs_tt->frac));
    }
    booltype __lt__(double val) {
        return mpr_time_as_dbl(*(mpr_time*)$self) < val;
    }
    booltype __le__(time *rhs)
    {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time *rhs_tt = (mpr_time*)rhs;
        return (tt->sec < rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac <= rhs_tt->frac));
    }
    booltype __le__(double val)
    {
        return mpr_time_as_dbl(*(mpr_time*)$self) <= val;
    }
    booltype __eq__(time *rhs)
    {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time *rhs_tt = (mpr_time*)rhs;
        return (tt->sec == rhs_tt->sec && tt->frac == rhs_tt->frac);
    }
    booltype __eq__(double val)
    {
        return mpr_time_as_dbl(*(mpr_time*)$self) == val;
    }
    booltype __ge__(time *rhs)
    {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time *rhs_tt = (mpr_time*)rhs;
        return (tt->sec > rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac >= rhs_tt->frac));
    }
    booltype __ge__(double val)
    {
        return mpr_time_as_dbl(*(mpr_time*)$self) >= val;
    }
    booltype __gt__(time *rhs)
    {
        mpr_time *tt = (mpr_time*)$self;
        mpr_time *rhs_tt = (mpr_time*)rhs;
        return (tt->sec > rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac > rhs_tt->frac));
    }
    booltype __gt__(double val)
    {
        return mpr_time_as_dbl(*(mpr_time*)$self) > val;
    }
}
