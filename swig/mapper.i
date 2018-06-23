%module mapper
%include "typemaps.i"
%typemap(in) PyObject *PyFunc {
    if ($input!=Py_None && !PyCallable_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "Need a callable object!");
        return NULL;
    }
    $1 = $input;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_INT64) mapper_id {
    $1 = (PyInt_Check($input) || PyLong_Check($input)) ? 1 : 0;
}
%typemap(in) (mapper_id id) {
    if (PyInt_Check($input))
        $1 = PyInt_AsLong($input);
    else if (PyLong_Check($input)) {
        // truncate to 64 bits
        $1 = PyLong_AsUnsignedLongLong($input);
        if ($1 == -1) {
            PyErr_SetString(PyExc_ValueError, "Id value must fit into 64 bits.");
            return NULL;
        }
    }
    else {
        PyErr_SetString(PyExc_ValueError, "Id must be int or long type.");
        return NULL;
    }
}
%typemap(out) mapper_id {
    $result = Py_BuildValue("l", (uint64_t)$1);
}
%typemap(in) (int num_ids, mapper_id *argv) {
    int i;
    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_ValueError, "Expecting a list");
        return NULL;
    }
    $1 = PyList_Size($input);
    $2 = (mapper_id*) malloc($1*sizeof(mapper_id));
    for (i = 0; i < $1; i++) {
        PyObject *s = PyList_GetItem($input,i);
        if (PyInt_Check(s))
            $2[i] = (int)PyInt_AsLong(s);
        else if (PyFloat_Check(s))
            $2[i] = (int)PyFloat_AsDouble(s);
        else {
            free($2);
            PyErr_SetString(PyExc_ValueError,
                            "List items must be int or float.");
            return NULL;
        }
    }
}
%typemap(typecheck) (int num_ids, mapper_id *argv) {
    $1 = PyList_Check($input) ? 1 : 0;
}
%typemap(freearg) (int num_ids, mapper_id *argv) {
    if ($2) free($2);
}
%typemap(in) (signal_list) {
    int i;
    signal *s;
    signal_list_t *sl = alloca(sizeof(*sl));
    if (PyList_Check($input)) {
        sl->len = PyList_Size($input);
        sl->sigs = (mapper_signal*) malloc(sl->len * sizeof(mapper_signal));
        for (i = 0; i < sl->len; i++) {
            PyObject *o = PyList_GetItem($input, i);
            if (o && strcmp(o->ob_type->tp_name, "signal")==0) {
                if (!SWIG_IsOK(SWIG_ConvertPtr(o, (void**)&s, SWIGTYPE_p__signal, 0))) {
                    SWIG_exception_fail(SWIG_TypeError,
                                        "in method '$symname', expecting type signal");
                    free(sl->sigs);
                    return NULL;
                }
                sl->sigs[i] = (mapper_signal)s;
            }
            else {
                free(sl->sigs);
                PyErr_SetString(PyExc_ValueError,
                                "List items must be signals.");
                return NULL;
            }
        }
    }
    else if ($input && strcmp(($input)->ob_type->tp_name, "signal")==0) {
        sl->len = 1;
        if (!SWIG_IsOK(SWIG_ConvertPtr($input, (void**)&s, SWIGTYPE_p__signal, 0))) {
            SWIG_exception_fail(SWIG_TypeError,
                                "in method '$symname', expecting type signal");
            return NULL;
        }
        sl->sigs = (mapper_signal*) malloc(sizeof(mapper_signal));
        sl->sigs[0] = (mapper_signal)s;
    }
    else {
        SWIG_exception_fail(SWIG_TypeError,
                            "in method '$symname', expecting type signal");
        return NULL;
    }
    $1 = sl;
}
%typemap(freearg) (signal_list) {
    if ($1) {
        signal_list sl = (signal_list)$1;
        if (sl->sigs)
            free (sl->sigs);
//        free(sl);
    }
}
%typemap(in) propval %{
    propval_t *prop = alloca(sizeof(*prop));
    if ($input == Py_None)
        $1 = 0;
    else {
        prop->type = 0;
        check_type($input, &prop->type, 1, 1);
        if (!prop->type) {
            PyErr_SetString(PyExc_ValueError,
                            "Problem determining value type.");
            return NULL;
        }
        if (PyList_Check($input))
            prop->len = PyList_Size($input);
        else
            prop->len = 1;
        prop->val = malloc(prop->len * mapper_type_size(prop->type));
        prop->free_val = 1;
        if (py_to_prop($input, prop, 0)) {
            free(prop->val);
            PyErr_SetString(PyExc_ValueError, "Problem parsing property value.");
            return NULL;
        }
        $1 = prop;
    }
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
        prop->name = 0;
        prop->val.type = 0;
        check_type($input, &prop->val.type, 1, 1);
        if (!prop->val.type) {
            PyErr_SetString(PyExc_ValueError,
                            "Problem determining value type.");
            return NULL;
        }
        if (PyList_Check($input))
            prop->val.len = PyList_Size($input);
        else
            prop->val.len = 1;
        prop->val.val = malloc(prop->val.len * mapper_type_size(prop->val.type));
        prop->val.free_val = 1;
        if (py_to_prop($input, &prop->val, &prop->name)) {
            free(prop->val.val);
            PyErr_SetString(PyExc_ValueError, "Problem parsing property value.");
            return NULL;
        }
        $1 = prop;
    }
%}
%typemap(out) named_prop {
    if ($1) {
        named_prop prop = (named_prop)$1;
        $result = prop_to_py(&prop->val, prop->name);
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
typedef struct _timetag {} timetag;

typedef struct _device_query {
    mapper_object *query;
} device_query;

typedef struct _signal_query {
    mapper_object *query;
} signal_query;

typedef struct _map_query {
    mapper_object *query;
} map_query;

typedef struct {
    void *val;
    int len;
    char free_val;
    mapper_type type;
} propval_t, *propval;

typedef struct {
    const char *name;
    propval_t val;
    mapper_property prop;
} named_prop_t, *named_prop;

PyThreadState *_save;

static int py_to_prop(PyObject *from, propval prop, const char **name)
{
    // here we are assuming sufficient memory has already been allocated
    if (!from || !prop->len)
        return 1;

    int i;

    switch (prop->type) {
        case MAPPER_STRING:
        {
            // only strings (bytes in py3) are valid
            if (prop->len > 1) {
                char **str_to = (char**)prop->val;
                for (i = 0; i < prop->len; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
#if PY_MAJOR_VERSION >= 3
                    if (!PyBytes_Check(element))
                        return 1;
                    str_to[i] = strdup(PyBytes_AsString(element));
#else
                    if (!PyString_Check(element))
                        return 1;
                    str_to[i] = strdup(PyString_AsString(element));
#endif
                }
            }
            else {
#if PY_MAJOR_VERSION >= 3
                if (!PyBytes_Check(from))
                    return 1;
                char **str_to = (char**)&prop->val;
                *str_to = strdup(PyBytes_AsString(from));
#else
                if (!PyString_Check(from))
                    return 1;
                char **str_to = (char**)&prop->val;
                *str_to = strdup(PyString_AsString(from));
#endif
            }
            break;
        }
        case MAPPER_CHAR:
        {
            // only strings are valid
            char *char_to = (char*)prop->val;
            if (prop->len > 1) {
                for (i = 0; i < prop->len; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
#if PY_MAJOR_VERSION >= 3
                    if (!PyBytes_Check(element))
                        return 1;
                    char *temp = PyBytes_AsString(element);
                    char_to[i] = temp[0];
#else
                    if (!PyString_Check(element))
                        return 1;
                    char *temp = PyString_AsString(element);
                    char_to[i] = temp[0];
#endif
                }
            }
            else {
#if PY_MAJOR_VERSION >= 3
                if (!PyString_Check(from))
                    return 1;
                char *temp = PyString_AsString(from);
                *char_to = temp[0];
#else
                if (!PyBytes_Check(from))
                    return 1;
                char *temp = PyBytes_AsString(from);
                *char_to = temp[0];
#endif
            }
            break;
        }
        case MAPPER_INT32:
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
        case MAPPER_FLOAT:
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
        case MAPPER_BOOL:
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

static int check_type(PyObject *v, mapper_type *c, int can_promote, int allow_sequence)
{
    if (PyBool_Check(v)) {
        if (*c == 0)
            *c = MAPPER_BOOL;
        else if (*c == MAPPER_STRING)
            return 1;
    }
    else if (PyInt_Check(v) || PyLong_Check(v)) {
        if (*c == 0)
            *c = MAPPER_INT32;
        else if (*c == MAPPER_STRING)
            return 1;
    }
    else if (PyFloat_Check(v)) {
        if (*c == 0)
            *c = MAPPER_FLOAT;
        else if (*c == MAPPER_STRING)
            return 1;
        else if (*c == MAPPER_INT32 && can_promote)
            *c = MAPPER_FLOAT;
    }
    else if (PyString_Check(v)
             || PyUnicode_Check(v)
#if PY_MAJOR_VERSION >= 3
             || PyBytes_Check(v)
#endif
             ) {
        if (*c == 0)
            *c = MAPPER_STRING;
        else if (*c != MAPPER_STRING && *c != MAPPER_CHAR)
            return 1;
    }
    else if (PySequence_Check(v)) {
        if (allow_sequence) {
            int i;
            for (i=0; i<PySequence_Size(v); i++) {
                if (check_type(PySequence_GetItem(v, i), c, can_promote, 0))
                    return 1;
            }
        }
        else
            return 1;
    }
    return 0;
}

static PyObject *prop_to_py(propval prop, const char *name)
{
    if (!prop->len)
        return 0;

    int i;
    PyObject *v = 0;
    if (prop->len > 1)
        v = PyList_New(prop->len);

    switch (prop->type) {
        case MAPPER_STRING:
        {
            if (prop->len > 1) {
                char **vect = (char**)prop->val;
                for (i=0; i<prop->len; i++)
#if PY_MAJOR_VERSION >= 3
                    PyList_SetItem(v, i, PyBytes_FromString(vect[i]));
#else
                    PyList_SetItem(v, i, PyString_FromString(vect[i]));
#endif
            }
            else
#if PY_MAJOR_VERSION >= 3
                v = PyBytes_FromString((char*)prop->val);
#else
                v = PyString_FromString((char*)prop->val);
#endif
            break;
        }
        case MAPPER_CHAR:
        {
            if (prop->len > 1) {
                char *vect = (char*)prop->val;
                for (i=0; i<prop->len; i++)
                    PyList_SetItem(v, i, Py_BuildValue("c", vect[i]));
            }
            else
                v = Py_BuildValue("c", *(char*)prop->val);
            break;
        }
        case MAPPER_INT32:
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
        case MAPPER_INT64:
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
        case MAPPER_FLOAT:
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
        case MAPPER_DOUBLE:
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
        case MAPPER_TIME:
        {
            mapper_time_t *vect = (mapper_time_t*)prop->val;
            if (prop->len > 1) {
                for (i=0; i<prop->len; i++) {
                    PyObject *py_tt = SWIG_NewPointerObj(SWIG_as_voidptr(&vect[i]),
                                                         SWIGTYPE_p__timetag, 0);
                    PyList_SetItem(v, i, Py_BuildValue("O", py_tt));
                }
            }
            else {
                PyObject *py_tt = SWIG_NewPointerObj(SWIG_as_voidptr(vect),
                                                     SWIGTYPE_p__timetag, 0);
                v = Py_BuildValue("O", py_tt);
            }
            break;
        }
        case MAPPER_BOOL:
            if (prop->len > 1) {
                int *vect = (int*)prop->val;
                for (i=0; i<prop->len; i++)
                    PyList_SetItem(v, i, PyBool_FromLong(vect[i]));
            }
            else
                v = PyBool_FromLong(*(int*)prop->val);
            break;
        default:
            return 0;
            break;
    }
    if (name) {
        PyObject *o = Py_BuildValue("sO", name, v);
        return o;
    }
    return v;
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
static void signal_handler_py(mapper_signal sig, mapper_id id, int len,
                              mapper_type type, const void *val, mapper_time tt)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist=0;
    PyObject *vallist=0;
    PyObject *result=0;
    int i;

    PyObject *py_sig = SWIG_NewPointerObj(SWIG_as_voidptr(sig),
                                          SWIGTYPE_p__signal, 0);
    PyObject *py_tt = SWIG_NewPointerObj(SWIG_as_voidptr(tt),
                                         SWIGTYPE_p__timetag, 0);

    if (val) {
        if (type == MAPPER_INT32) {
            int *vint = (int*)val;
            if (len > 1) {
                vallist = PyList_New(len);
                for (i = 0; i < len; i++) {
                    PyObject *o = Py_BuildValue("i", vint[i]);
                    PyList_SET_ITEM(vallist, i, o);
                }
                arglist = Py_BuildValue("(OLOO)", py_sig, id, vallist, py_tt);
            }
            else
                arglist = Py_BuildValue("(OLiO)", py_sig, id, *(int*)val, py_tt);
        }
        else if (type == MAPPER_FLOAT) {
            if (len > 1) {
                float *vfloat = (float*)val;
                vallist = PyList_New(len);
                for (i = 0; i < len; i++) {
                    PyObject *o = Py_BuildValue("f", vfloat[i]);
                    PyList_SET_ITEM(vallist, i, o);
                }
                arglist = Py_BuildValue("(OLOO)", py_sig, id, vallist, py_tt);
            }
            else
                arglist = Py_BuildValue("(OLfO)", py_sig, id, *(float*)val, py_tt);
        }
    }
    else {
        arglist = Py_BuildValue("(OiOO)", py_sig, id, Py_None, py_tt);
    }
    if (!arglist) {
        printf("[mapper] Could not build arglist (signal_handler_py).\n");
        return;
    }
    PyObject **callbacks = (PyObject**)sig->obj.user;
    result = PyEval_CallObject(callbacks[0], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(vallist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_instance_event handler
 * is called. */
static void instance_event_handler_py(mapper_signal sig, mapper_id id,
                                      mapper_instance_event event,
                                      mapper_time_t *tt)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist=0;
    PyObject *result=0;

    PyObject *py_sig = SWIG_NewPointerObj(SWIG_as_voidptr(sig),
                                          SWIGTYPE_p__signal, 0);
    PyObject *py_tt = SWIG_NewPointerObj(SWIG_as_voidptr(tt),
                                         SWIGTYPE_p__timetag, 0);

    arglist = Py_BuildValue("(OLiO)", py_sig, id, event, py_tt);
    if (!arglist) {
        printf("[mapper] Could not build arglist (instance_event_handler_py).\n");
        return;
    }
    PyObject **callbacks = (PyObject**)sig->obj.user;
    result = PyEval_CallObject(callbacks[1], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

typedef int booltype;

typedef struct _signal_list {
    mapper_signal *sigs;
    int len;
} signal_list_t, *signal_list;

/* Wrapper for callback back to python when a graph handler is called. */
static void graph_handler_py(mapper_graph g, mapper_object obj,
                             mapper_record_event event, const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_obj;
    mapper_object_type type = mapper_object_get_type(obj);
    switch (type) {
        case MAPPER_OBJ_DEVICE:
            py_obj = SWIG_NewPointerObj(SWIG_as_voidptr(obj),
                                        SWIGTYPE_p__device, 0);
            break;
        case MAPPER_OBJ_SIGNAL:
            py_obj = SWIG_NewPointerObj(SWIG_as_voidptr(obj),
                                        SWIGTYPE_p__signal, 0);
            break;
        case MAPPER_OBJ_MAP:
            py_obj = SWIG_NewPointerObj(SWIG_as_voidptr(obj),
                                        SWIGTYPE_p__map, 0);
            break;
        default:
            printf("[mapper] Unknown object type (graph_handler_py).\n");
            return;
    }

    PyObject *arglist = Py_BuildValue("(iOi)", type, py_obj, event);
    if (!arglist) {
        printf("[mapper] Could not build arglist (graph_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

static mapper_signal add_signal_internal(mapper_device dev, mapper_direction dir,
                                         int num_instances, const char *name,
                                         int len, char type, const char *unit,
                                         propval min, propval max, PyObject *PyFunc)
{
    int i;
    void *h = 0;
    PyObject **callbacks = 0;
    if (PyFunc) {
        h = signal_handler_py;
        callbacks = malloc(2 * sizeof(PyObject*));
        callbacks[0] = PyFunc;
        callbacks[1] = 0;
        Py_INCREF(PyFunc);
    }
    void *pmn=0, *pmx=0;
    int pmn_coerced=0, pmx_coerced=0;
    if (type == MAPPER_FLOAT)
    {
        if (min && min->len == len) {
            if (min->type == MAPPER_FLOAT)
                pmn = min->val;
            else if (min->type == MAPPER_INT32) {
                float *to = (float*)malloc(len * sizeof(float));
                int *from = (int*)min->val;
                for (i=0; i<len; i++) {
                    to[i] = (float)from[i];
                }
                pmn = to;
                pmn_coerced = 1;
            }
        }
        if (max && max->len == len) {
            if (max->type == MAPPER_FLOAT)
                pmx = max->val;
            else if (max->type == MAPPER_INT32) {
                float *to = (float*)malloc(len * sizeof(float));
                int *from = (int*)max->val;
                for (i=0; i<len; i++) {
                    to[i] = (float)from[i];
                }
                pmx = to;
                pmx_coerced = 1;
            }
        }
    }
    else if (type == MAPPER_INT32)
    {
        if (min && min->len == len) {
            if (min->type == MAPPER_INT32)
                pmn = min->val;
            else if (min->type == MAPPER_FLOAT) {
                int *to = (int*)malloc(len * sizeof(int));
                float *from = (float*)min->val;
                for (i=0; i<len; i++) {
                    to[i] = (int)from[i];
                }
                pmn = to;
                pmn_coerced = 1;
            }
        }
        if (max && max->len == len) {
            if (max->type == MAPPER_INT32)
                pmx = max->val;
            else if (max->type == MAPPER_FLOAT) {
                int *to = (int*)malloc(len * sizeof(int));
                float *from = (float*)max->val;
                for (i=0; i<len; i++) {
                    to[i] = (int)from[i];
                }
                pmx = to;
                pmx_coerced = 1;
            }
        }
    }
    mapper_signal sig = mapper_device_add_signal(dev, dir, num_instances, name,
                                                 len, type, unit, pmn, pmx, h);
    sig->obj.user = callbacks;

    if (pmn_coerced)
        free(pmn);
    if (pmx_coerced)
        free(pmx);
    return sig;
}

static void set_object_prop(mapper_object obj, mapper_property prop,
                            const char *name, propval val, booltype publish) {
    if (MAPPER_PROP_USER_DATA == prop || (name && !strcmp(name, "user_data")))
        return;
    if (val)
        mapper_object_set_prop(obj, prop, name, val->len, val->type, val->val,
                               publish);
    else
        mapper_object_remove_prop(obj, prop, name);
}

static propval get_object_prop_by_name(mapper_object obj, const char *name) {
    mapper_property prop;
    int len;
    mapper_type type;
    const void *val;
    prop = mapper_object_get_prop_by_name(obj, name, &len, &type, &val);
    if (MAPPER_PROP_UNKNOWN == prop)
        return 0;
    if (MAPPER_PROP_USER_DATA == prop) {
        // don't include user_data
        return 0;
    }
    propval pval = malloc(sizeof(propval_t));
    pval->len = len;
    pval->type = type;
    pval->val = (void*)val;
    pval->free_val = 0;
    return pval;
}

static named_prop get_object_prop_by_index(mapper_object obj, int idx) {
    mapper_property prop;
    const char *name;
    int len;
    mapper_type type;
    const void *val;
    prop = mapper_object_get_prop_by_index(obj, idx, &name, &len, &type, &val);
    if (MAPPER_PROP_UNKNOWN == prop)
        return 0;
    if (MAPPER_PROP_USER_DATA == prop) {
            // don't include user_data
        return 0;
    }
    named_prop named = malloc(sizeof(named_prop_t));
    named->prop = prop;
    named->name = name;
    named->val.len = len;
    named->val.type = type;
    named->val.val = (void*)val;
    named->val.free_val = 0;
    return named;
}

%}

/*! Possible data types. */
%constant char INT32                    = MAPPER_INT32;
%constant char INT64                    = MAPPER_INT64;
%constant char FLOAT                    = MAPPER_FLOAT;
%constant char DOUBLE                   = MAPPER_DOUBLE;
%constant char STRING                   = MAPPER_STRING;
%constant char BOOL                     = MAPPER_BOOL;
%constant char TIMETAG                  = MAPPER_TIME;
%constant char CHAR                     = MAPPER_CHAR;
%constant char PTR                      = MAPPER_PTR;
%constant char DEVICE                   = MAPPER_DEVICE;
%constant char SIGNAL                   = MAPPER_SIGNAL;
%constant char MAP                      = MAPPER_MAP;
%constant char NULL                     = MAPPER_NULL;

/*! Symbolic representation of recognized properties. */
%constant int PROP_UNKNOWN              = MAPPER_PROP_UNKNOWN;
%constant int PROP_CALIB                = MAPPER_PROP_CALIB;
%constant int PROP_DEVICE               = MAPPER_PROP_DEVICE;
%constant int PROP_DIR                  = MAPPER_PROP_DIR;
%constant int PROP_EXPR                 = MAPPER_PROP_EXPR;
%constant int PROP_HOST                 = MAPPER_PROP_HOST;
%constant int PROP_ID                   = MAPPER_PROP_ID;
%constant int PROP_INSTANCE             = MAPPER_PROP_INSTANCE;
%constant int PROP_IS_LOCAL             = MAPPER_PROP_IS_LOCAL;
%constant int PROP_LENGTH               = MAPPER_PROP_LENGTH;
%constant int PROP_LIB_VERSION          = MAPPER_PROP_LIB_VERSION;
%constant int PROP_MAX                  = MAPPER_PROP_MAX;
%constant int PROP_MIN                  = MAPPER_PROP_MIN;
%constant int PROP_MUTED                = MAPPER_PROP_MUTED;
%constant int PROP_NAME                 = MAPPER_PROP_NAME;
%constant int PROP_NUM_INPUTS           = MAPPER_PROP_NUM_INPUTS;
%constant int PROP_NUM_INSTANCES        = MAPPER_PROP_NUM_INSTANCES;
%constant int PROP_NUM_MAPS             = MAPPER_PROP_NUM_MAPS;
%constant int PROP_NUM_MAPS_IN          = MAPPER_PROP_NUM_MAPS_IN;
%constant int PROP_NUM_MAPS_OUT         = MAPPER_PROP_NUM_MAPS_OUT;
%constant int PROP_NUM_OUTPUTS          = MAPPER_PROP_NUM_OUTPUTS;
%constant int PROP_ORDINAL              = MAPPER_PROP_ORDINAL;
%constant int PROP_PORT                 = MAPPER_PROP_PORT;
%constant int PROP_PROCESS_LOC          = MAPPER_PROP_PROCESS_LOC;
%constant int PROP_PROTOCOL             = MAPPER_PROP_PROTOCOL;
%constant int PROP_RATE                 = MAPPER_PROP_RATE;
%constant int PROP_SCOPE                = MAPPER_PROP_SCOPE;
%constant int PROP_SIGNAL               = MAPPER_PROP_SIGNAL;
%constant int PROP_SLOT                 = MAPPER_PROP_SLOT;
%constant int PROP_STATUS               = MAPPER_PROP_STATUS;
%constant int PROP_SYNCED               = MAPPER_PROP_SYNCED;
%constant int PROP_TYPE                 = MAPPER_PROP_TYPE;
%constant int PROP_UNIT                 = MAPPER_PROP_UNIT;
%constant int PROP_USE_INSTANCES        = MAPPER_PROP_USE_INSTANCES;
%constant int PROP_USER_DATA            = MAPPER_PROP_USER_DATA;
%constant int PROP_VERSION              = MAPPER_PROP_VERSION;
%constant int PROP_EXTRA                = MAPPER_PROP_EXTRA;

/*! Possible object types for subscriptions. */
%constant int OBJ_NONE                  = MAPPER_OBJ_NONE;
%constant int OBJ_DEVICE                = MAPPER_OBJ_DEVICE;
%constant int OBJ_INPUT_SIGNAL          = MAPPER_OBJ_INPUT_SIGNAL;
%constant int OBJ_OUTPUT_SIGNAL         = MAPPER_OBJ_OUTPUT_SIGNAL;
%constant int OBJ_SIGNAL                = MAPPER_OBJ_SIGNAL;
%constant int OBJ_MAP_IN                = MAPPER_OBJ_MAP_IN;
%constant int OBJ_MAP_OUT               = MAPPER_OBJ_MAP_OUT;
%constant int OBJ_MAP                   = MAPPER_OBJ_MAP;
%constant int OBJ_ALL                   = MAPPER_OBJ_ALL;

/*! Possible operations for composing graph queries. */
%constant int OP_DOES_NOT_EXIST         = MAPPER_OP_DOES_NOT_EXIST;
%constant int OP_EQUAL                  = MAPPER_OP_EQUAL;
%constant int OP_EXISTS                 = MAPPER_OP_EXISTS;
%constant int OP_GREATER_THAN           = MAPPER_OP_GREATER_THAN;
%constant int OP_GREATER_THAN_OR_EQUAL  = MAPPER_OP_GREATER_THAN_OR_EQUAL;
%constant int OP_LESS_THAN              = MAPPER_OP_LESS_THAN;
%constant int OP_LESS_THAN_OR_EQUAL     = MAPPER_OP_LESS_THAN_OR_EQUAL;
%constant int OP_NOT_EQUAL              = MAPPER_OP_NOT_EQUAL;

/*! Describes the possible locations for map stream processing. */
%constant int LOC_UNDEFINED             = MAPPER_LOC_UNDEFINED;
%constant int LOC_SRC                   = MAPPER_LOC_SRC;
%constant int LOC_DST                   = MAPPER_LOC_DST;
%constant int LOC_ANY                   = MAPPER_LOC_ANY;

/*! Describes the possible network protocol for map communication. */
%constant int PROTO_UNDEFINED           = MAPPER_PROTO_UNDEFINED;
%constant int PROTO_UDP                 = MAPPER_PROTO_UDP;
%constant int PROTO_TCP                 = MAPPER_PROTO_TCP;

/*! The set of possible directions for a signal. */
%constant int DIR_UNDEFINED             = MAPPER_DIR_UNDEFINED;
%constant int DIR_IN                    = MAPPER_DIR_IN;
%constant int DIR_OUT                   = MAPPER_DIR_OUT;
%constant int DIR_ANY                   = MAPPER_DIR_ANY;

/*! The set of possible actions on an instance, used to register callbacks to
 *  inform them of what is happening. */
%constant int NEW_INSTANCE              = MAPPER_NEW_INSTANCE;
%constant int UPSTREAM_RELEASE          = MAPPER_UPSTREAM_RELEASE;
%constant int DOWNSTREAM_RELEASE        = MAPPER_DOWNSTREAM_RELEASE;
%constant int INSTANCE_OVERFLOW         = MAPPER_INSTANCE_OVERFLOW;
%constant int INSTANCE_ALL              = MAPPER_INSTANCE_ALL;

/*! Describes the voice-stealing mode for instances. */
%constant int NO_STEALING               = MAPPER_NO_STEALING;
%constant int STEAL_OLDEST              = MAPPER_STEAL_OLDEST;
%constant int STEAL_NEWEST              = MAPPER_STEAL_NEWEST;

/*! The set of possible events for a graph record, used to inform callbacks
 *  of what is happening to a record. */
%constant int ADDED                     = MAPPER_ADDED;
%constant int MODIFIED                  = MAPPER_MODIFIED;
%constant int REMOVED                   = MAPPER_REMOVED;
%constant int EXPIRED                   = MAPPER_EXPIRED;

%constant char* version                 = PACKAGE_VERSION;

typedef struct _device {} device;
typedef struct _signal {} signal;
typedef struct _map {} map;
typedef struct _graph {} graph;
typedef struct _timetag {} timetag;

typedef struct _device_query {
    mapper_object *query;
} device_query;

%exception _device_query::next {
    assert(!my_error);
    $action
    if (my_error) {
        my_error = 0;
        PyErr_SetString(PyExc_StopIteration, "End of list");
        return NULL;
    }
}

%extend _device_query {
    _device_query(const device_query *orig) {
        struct _device_query *d = malloc(sizeof(struct _device_query));
        d->query = mapper_object_list_copy(orig->query);
        return d;
    }
    ~_device_query() {
        mapper_object_list_free($self->query);
        free($self);
    }
    struct _device_query *__iter__() {
        return $self;
    }
    device *next() {
        mapper_object result = 0;
        if ($self->query) {
            result = *($self->query);
            $self->query = mapper_object_list_next($self->query);
        }
        if (result)
            return (device*)result;
        my_error = 1;
        return NULL;
    }
    device_query *join(device_query *d) {
        if (!d || !d->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(d->query);
        $self->query = mapper_object_list_union($self->query, copy);
        return $self;
    }
    device_query *intersect(device_query *d) {
        if (!d || !d->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(d->query);
        $self->query = mapper_object_list_intersection($self->query, copy);
        return $self;
    }
    device_query *subtract(device_query *d) {
        if (!d || !d->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(d->query);
        $self->query = mapper_object_list_difference($self->query, copy);
        return $self;
    }
    device_query *filter(const char *name, propval val=0,
                         mapper_op op=MAPPER_OP_EQUAL) {
        if (name && val) {
            $self->query = mapper_object_list_filter($self->query,
                                                     MAPPER_PROP_UNKNOWN, name,
                                                     val->len, val->type,
                                                     val->val, op);
        }
        return $self;
    }
    int length() {
        return mapper_object_list_get_length($self->query);
    }
}

%extend _device {
    _device(const char *name, graph *DISOWN=0) {
        device *d = (device*)mapper_device_new(name, (mapper_graph) DISOWN);
        return d;
    }
    ~_device() {
        PyObject **callbacks = (PyObject**)((mapper_object)$self)->user;
        if (callbacks) {
            if (callbacks[0])
                Py_XDECREF(callbacks[0]);
            if (callbacks[1])
                Py_XDECREF(callbacks[1]);
            free(callbacks);
        }
        mapper_device_free((mapper_device)$self);
    }
    graph *graph() {
        return (graph*)mapper_object_get_graph((mapper_object)$self);
    }

    // functions
    /* Note, these functions return memory which is _not_ owned by Python.
     * Correspondingly, the SWIG default is to set thisown to False, which is
     * correct for this case. */
    signal *add_signal(int dir, int num_instances, const char *name,
                       int len=1, char type=MAPPER_FLOAT, const char *unit=0,
                       propval min=0, propval max=0, PyObject *PyFunc=0)
    {
        return (signal*)add_signal_internal((mapper_device)$self, dir,
                                            num_instances, name, len, type,
                                            unit, min, max, PyFunc);
    }
    device *remove_signal(signal *sig) {
        mapper_signal msig = (mapper_signal)sig;
        if (msig->obj.user) {
            PyObject **callbacks = msig->obj.user;
            Py_XDECREF(callbacks[0]);
            Py_XDECREF(callbacks[1]);
            free(callbacks);
        }
        mapper_device_remove_signal((mapper_device)$self, msig);
        return $self;
    }

    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mapper_device_poll((mapper_device)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }

    // queue management
    timetag *start_queue(timetag *py_tt=0) {
        if (py_tt) {
            mapper_time_t *tt = (mapper_time_t*)py_tt;
            mapper_device_start_queue((mapper_device)$self, *tt);
            return py_tt;
        }
        else {
            mapper_time_t *tt = (mapper_time_t*)malloc(sizeof(mapper_time_t));
            mapper_time_now(tt);
            mapper_device_start_queue((mapper_device)$self, MAPPER_NOW);
            return (timetag*)tt;
        }
    }
    device *send_queue(timetag *py_tt) {
        mapper_time_t *tt = (mapper_time_t*)py_tt;
        mapper_device_send_queue((mapper_device)$self, *tt);
        return $self;
    }

    mapper_signal_group add_signal_group() {
        return mapper_device_add_signal_group((mapper_device)$self);
    }
    device *remove_signal_group(mapper_signal_group group) {
        mapper_device_remove_signal_group((mapper_device)$self, group);
        return $self;
    }
    mapper_id generate_unique_id() {
        return mapper_device_generate_unique_id((mapper_device)$self);
    }

    // property getters
    int get_num_properties() {
        return mapper_object_get_num_props((mapper_object)$self, 0);
    }
    booltype get_is_ready() {
        return mapper_device_ready((mapper_device)$self);
    }
    int get_num_signals(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_device_get_num_signals((mapper_device)$self, dir);
    }
    int get_num_maps(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_device_get_num_maps((mapper_device)$self, dir);
    }
    propval get_property(const char *name) {
        return get_object_prop_by_name((mapper_object)$self, name);
    }
    named_prop get_property(int idx) {
        return get_object_prop_by_index((mapper_object)$self, idx);
    }

    // property setters
    device *set_property(const char *name, propval val=0, booltype publish=1) {
        set_object_prop((mapper_object)$self, MAPPER_PROP_UNKNOWN, name, val,
                        publish);
        return $self;
    }
    device *set_property(int idx, propval val=0, booltype publish=1) {
        set_object_prop((mapper_object)$self, idx, NULL, val, publish);
        return $self;
    }
    device *remove_property(const char *name) {
        if (name && strcmp(name, "user_data"))
            mapper_object_remove_prop((mapper_object)$self, MAPPER_PROP_UNKNOWN,
                                      name);
        return $self;
    }
    device *remove_property(int idx) {
        if (MAPPER_PROP_UNKNOWN != idx && MAPPER_PROP_USER_DATA != idx)
            mapper_object_remove_prop((mapper_object)$self, idx, NULL);
        return $self;
    }
    device *push() {
        mapper_object_push((mapper_object)$self);
        return $self;
    }

    // signal getter
    signal_query *signals(mapper_direction dir=MAPPER_DIR_ANY) {
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = mapper_device_get_signals((mapper_device)$self, dir);
        return ret;
    }

    // map getter
    map_query *maps(mapper_direction dir=MAPPER_DIR_ANY) {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_device_get_maps((mapper_device)$self, dir);
        return ret;
    }

    %pythoncode {
        num_signals = property(get_num_signals)
        num_maps = property(get_num_maps)
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
                def __setitem__(self, name, val):
                    props[name] = val
                    device.set_property(name, val)
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
    }
}

typedef struct _signal_query {
    mapper_object *query;
} signal_query;

%exception _signal_query::next {
    assert(!my_error);
    $action
    if (my_error) {
        my_error = 0;
        PyErr_SetString(PyExc_StopIteration, "End of list");
        return NULL;
    }
}

%extend _signal_query {
    _signal_query(const signal_query *orig) {
        struct _signal_query *s = malloc(sizeof(struct _signal_query));
        s->query = mapper_object_list_copy(orig->query);
        return s;
    }
    ~_signal_query() {
        mapper_object_list_free($self->query);
        free($self);
    }
    struct _signal_query *__iter__() {
        return $self;
    }
    signal *next() {
        mapper_object result = 0;
        if ($self->query) {
            result = *($self->query);
            $self->query = mapper_object_list_next($self->query);
        }
        if (result)
            return (signal*)result;
        my_error = 1;
        return NULL;
    }
    signal_query *join(signal_query *s) {
        if (!s || !s->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(s->query);
        $self->query = mapper_object_list_union($self->query, copy);
        return $self;
    }
    signal_query *intersect(signal_query *s) {
        if (!s || !s->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(s->query);
        $self->query = mapper_object_list_intersection($self->query, copy);
        return $self;
    }
    signal_query *subtract(signal_query *s) {
        if (!s || !s->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(s->query);
        $self->query = mapper_object_list_difference($self->query, copy);
        return $self;
    }
    signal_query *filter(const char *name, propval val=0,
                         mapper_op op=MAPPER_OP_EQUAL) {
        if (name && val) {
            $self->query = mapper_object_list_filter($self->query,
                                                     MAPPER_PROP_UNKNOWN, name,
                                                     val->len, val->type,
                                                     val->val, op);
        }
        return $self;
    }
    int length() {
        return mapper_object_list_get_length($self->query);
    }
}

%extend _signal {
    device *device() {
        return (device*)mapper_signal_get_device((mapper_signal)$self);
    }

    // functions
    int active_instance_id(int idx) {
        return mapper_signal_get_active_instance_id((mapper_signal)$self, idx);
    }
    int instance_id(int idx) {
        return mapper_signal_get_instance_id((mapper_signal)$self, idx);
    }
    signal *release_instance(int id, timetag *tt=0) {
        mapper_signal_release_instance((mapper_signal)$self, id,
                                       tt ? *(mapper_time_t*)tt : MAPPER_NOW);
        return $self;
    }
    signal *remove_instance(int id) {
        mapper_signal_remove_instance((mapper_signal)$self, id);
        return $self;
    }
    signal *reserve_instances(int num=1) {
        mapper_signal_reserve_instances((mapper_signal)$self, num, 0, 0);
        return $self;
    }
    signal *reserve_instances(int num_ids, mapper_id *argv) {
        mapper_signal_reserve_instances((mapper_signal)$self, num_ids, argv, 0);
        return $self;
    }
    signal *set_instance_event_callback(PyObject *PyFunc=0, int flags=0) {
        mapper_instance_event_handler *h = 0;
        mapper_signal sig = (mapper_signal)$self;
        PyObject **callbacks = (PyObject**)sig->obj.user;
        if (PyFunc) {
            h = instance_event_handler_py;
            if (callbacks) {
                callbacks[1] = PyFunc;
            }
            else {
                callbacks = malloc(2 * sizeof(PyObject*));
                callbacks[0] = 0;
                callbacks[1] = PyFunc;
            }
            Py_INCREF(PyFunc);
        }
        else if (callbacks) {
            Py_XDECREF(callbacks[1]);
            if (callbacks[0])
                callbacks[1] = 0;
            else {
                free(callbacks);
                callbacks = 0;
            }
        }
        mapper_signal_set_instance_event_callback(sig, h, flags);
        return $self;
    }
    signal *set_callback(PyObject *PyFunc=0) {
        mapper_signal_update_handler *h = 0;
        mapper_signal sig = (mapper_signal)$self;
        PyObject **callbacks = (PyObject**)sig->obj.user;
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
        sig->obj.user = callbacks;
        mapper_signal_set_callback(sig, h);
        return $self;
    }
    signal *set_value(propval val, timetag *tt=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_set_value(sig, 0, 0, MAPPER_NULL, NULL,
                                    tt ? *(mapper_time_t*)tt : MAPPER_NOW);
            return $self;
        }
        mapper_signal_set_value(sig, 0, val->len, val->type, val->val,
                                tt ? *(mapper_time_t*)tt : MAPPER_NOW);
        return $self;
    }
    signal *set_value(int id, propval val, timetag *tt=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_set_value(sig, 0, 0, MAPPER_NULL, NULL,
                                    tt ? *(mapper_time_t*)tt : MAPPER_NOW);
            return $self;
        }
        mapper_signal_set_value(sig, id, val->len, val->type, val->val,
                                tt ? *(mapper_time_t*)tt : MAPPER_NOW);
        return $self;
    }

    // property getters
    int get_num_properties() {
        return mapper_object_get_num_props((mapper_object)$self, 0);
    }
    mapper_signal_group get_group() {
        return mapper_signal_get_group((mapper_signal)$self);
    }
    int get_stealing_mode()
    {
        return mapper_signal_get_stealing_mode((mapper_signal)$self);
    }
    int get_num_active_instances() {
        return mapper_signal_get_num_active_instances((mapper_signal)$self);
    }
    int get_num_instances() {
        return mapper_signal_get_num_instances((mapper_signal)$self);
    }
    int get_num_maps(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_signal_get_num_maps((mapper_signal)$self, dir);
    }
    int get_num_reserved_instances() {
        return mapper_signal_get_num_reserved_instances((mapper_signal)$self);
    }
    propval get_property(const char *name) {
        return get_object_prop_by_name((mapper_object)$self, name);
    }
    named_prop get_property(int idx) {
        return get_object_prop_by_index((mapper_object)$self, idx);
    }
    propval value() {
        mapper_signal sig = (mapper_signal)$self;
        const void *val = mapper_signal_get_value(sig, 0, 0);
        if (!val)
            return 0;
        propval prop = malloc(sizeof(propval_t));
        prop->len = sig->len;
        prop->type = sig->type;
        prop->val = (void*)val;
        return prop;
    }
    propval value(int id) {
        mapper_signal sig = (mapper_signal)$self;
        const void *val = mapper_signal_get_value(sig, id, 0);
        if (!val)
            return 0;
        propval prop = malloc(sizeof(propval_t));
        prop->len = sig->len;
        prop->type = sig->type;
        prop->val = (void*)val;
        return prop;
    }

    // property setters
    signal *set_group(mapper_signal_group group) {
        mapper_signal_set_group((mapper_signal)$self, group);
        return $self;
    }
    signal *set_stealing_mode(int mode) {
        mapper_signal_set_stealing_mode((mapper_signal)$self, mode);
        return $self;
    }
    signal *set_property(const char *name, propval val=0, booltype publish=1) {
        set_object_prop((mapper_object)$self, MAPPER_PROP_UNKNOWN, name, val,
                        publish);
        return $self;
    }
    signal *set_property(int idx, propval val=0, booltype publish=1) {
        set_object_prop((mapper_object)$self, idx, NULL, val, publish);
        return $self;
    }
    signal *remove_property(const char *name) {
        if (name && strcmp(name, "user_data"))
            mapper_object_remove_prop((mapper_object)$self, MAPPER_PROP_UNKNOWN,
                                      name);
        return $self;
    }
    signal *remove_property(int idx) {
        if (MAPPER_PROP_UNKNOWN != idx && MAPPER_PROP_USER_DATA != idx)
            mapper_object_remove_prop((mapper_object)$self, idx, NULL);
        return $self;
    }
    signal *push() {
        mapper_object_push((mapper_object)$self);
        return $self;
    }

    map_query *maps(mapper_direction dir=MAPPER_DIR_ANY) {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_signal_get_maps((mapper_signal)$self, dir);
        return ret;
    }

    %pythoncode {
        group = property(get_group, set_group)
        stealing_mode = property(get_stealing_mode, set_stealing_mode)
        num_active_instances = property(get_num_active_instances)
        num_instances = property(get_num_instances)
        num_maps = property(get_num_maps)
        num_properties = property(get_num_properties)
        num_reserved_instances = property(get_num_reserved_instances)
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
                def __setitem__(self, name, val):
                    props[name] = val
                    signal.set_property(name, val)
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
    }
}

typedef struct _map_query {
    mapper_object *query;
} map_query;

%exception _map_query::next {
    assert(!my_error);
    $action
    if (my_error) {
        my_error = 0;
        PyErr_SetString(PyExc_StopIteration, "End of list");
        return NULL;
    }
}

%extend _map_query {
    _map_query(const map_query *orig) {
        struct _map_query *mq = malloc(sizeof(struct _map_query));
        mq->query = mapper_object_list_copy(orig->query);
        return mq;
    }
    ~_map_query() {
        mapper_object_list_free($self->query);
        free($self);
    }
    struct _map_query *__iter__() {
        return $self;
    }
    map *next() {
        mapper_object result = 0;
        if ($self->query) {
            result = *($self->query);
            $self->query = mapper_object_list_next($self->query);
        }
        if (result)
            return (map*)result;
        my_error = 1;
        return NULL;
    }
    map_query *join(map_query *m) {
        if (!m || !m->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(m->query);
        $self->query = mapper_object_list_union($self->query, copy);
        return $self;
    }
    map_query *intersect(map_query *m) {
        if (!m || !m->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(m->query);
        $self->query = mapper_object_list_intersection($self->query, copy);
        return $self;
    }
    map_query *subtract(map_query *m) {
        if (!m || !m->query)
            return $self;
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy(m->query);
        $self->query = mapper_object_list_difference($self->query, copy);
        return $self;
    }
    map_query *filter(const char *name, propval val=0,
                      mapper_op op=MAPPER_OP_EQUAL) {
        if (name && val) {
            $self->query = mapper_object_list_filter($self->query,
                                                     MAPPER_PROP_UNKNOWN, name,
                                                     val->len, val->type,
                                                     val->val, op);
        }
        return $self;
    }
    int length() {
        return mapper_object_list_get_length($self->query);
    }
    map_query *release() {
        // need to use a copy of query
        mapper_object *copy = mapper_object_list_copy($self->query);
        while (copy) {
            mapper_map_release((mapper_map)*copy);
            copy = mapper_object_list_next(copy);
        }
        return $self;
    }
}

%extend _map {
    _map(signal_list srcs=0, signal_list dsts=0) {
        if (!srcs || !dsts)
            return (map*)0;
        return (map*)mapper_map_new(srcs->len, srcs->sigs, dsts->len, dsts->sigs);
    }
    ~_map() {
        ;
    }

    map *refresh() {
        mapper_map_refresh((mapper_map)$self);
        return $self;
    }
    void release() {
        mapper_map_release((mapper_map)$self);
    }

    // signal getters
    // TODO: return generator for source signal iterable
    signal *source(int idx=0) {
        return (signal*)mapper_map_get_signal((mapper_map)$self, MAPPER_LOC_SRC,
                                              idx);
    }
    signal *destination(int idx=0) {
        return (signal*)mapper_map_get_signal((mapper_map)$self, MAPPER_LOC_DST,
                                              0);
    }
    signal *signal(mapper_location loc, int idx=0) {
        return (signal*)mapper_map_get_signal((mapper_map)$self, loc, idx);
    }
    int index(signal *sig) {
        return mapper_map_get_signal_index((mapper_map)$self, (mapper_signal)sig);
    }

    // scopes
    map *add_scope(device *dev) {
        mapper_map_add_scope((mapper_map)$self, (mapper_device)dev);
        return $self;
    }
    map *remove_scope(device *dev) {
        mapper_map_remove_scope((mapper_map)$self, (mapper_device)dev);
        return $self;
    }
    device_query *scopes() {
        device_query *ret = malloc(sizeof(struct _device_query));
        ret->query = mapper_map_get_scopes((mapper_map)$self);
        return ret;
    }

    // property getters
    int get_num_properties() {
        return mapper_object_get_num_props((mapper_object)$self, 0);
    }
    booltype get_ready() {
        return mapper_map_ready((mapper_map)$self);
    }
    int get_num_signals(mapper_location loc=MAPPER_DIR_ANY) {
        return mapper_map_get_num_signals((mapper_map)$self, loc);
    }
    propval get_property(const char *name) {
        return get_object_prop_by_name((mapper_object)$self, name);
    }
    named_prop get_property(int idx) {
        return get_object_prop_by_index((mapper_object)$self, idx);
    }

    // property setters
    map *set_property(const char *name, propval val=0, booltype publish=1) {
        set_object_prop((mapper_object)$self, MAPPER_PROP_UNKNOWN, name, val,
                        publish);
        return $self;
    }
    map *set_property(int idx, propval val=0, booltype publish=1) {
        set_object_prop((mapper_object)$self, idx, NULL, val, publish);
        return $self;
    }
    map *remove_property(const char *name) {
        if (name && strcmp(name, "user_data"))
            mapper_object_remove_prop((mapper_object)$self, MAPPER_PROP_UNKNOWN,
                                      name);
        return $self;
    }
    map *remove_property(int idx) {
        if (MAPPER_PROP_UNKNOWN != idx && MAPPER_PROP_USER_DATA != idx)
            mapper_object_remove_prop((mapper_object)$self, idx, NULL);
        return $self;
    }
    map *push() {
        mapper_object_push((mapper_object)$self);
        return $self;
    }

    %pythoncode {
        num_properties = property(get_num_properties)
        num_signals = property(get_num_signals)
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
                def __setitem__(self, name, val):
                    props[name] = val
                    map.set_property(name, val, true)
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
    }
}

%extend _graph {
    _graph(int flags=0x00) {
        return (graph*)mapper_graph_new(flags);
    }
    ~_graph() {
        mapper_graph_free((mapper_graph)$self);
    }
    const char *get_interface() {
        return mapper_graph_get_interface((mapper_graph)$self);
    }
    graph *set_interface(const char *iface) {
        mapper_graph_set_interface((mapper_graph)$self, iface);
        return $self;
    }
    const char *get_multicast_addr() {
        return mapper_graph_get_multicast_addr((mapper_graph)$self);
    }
    graph *set_multicast_addr(const char *group, int port) {
        mapper_graph_set_multicast_addr((mapper_graph)$self, group, port);
        return $self;
    }
    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mapper_graph_poll((mapper_graph)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }
    graph *subscribe(device *dev, int subscribe_flags=0, int timeout=-1) {
        mapper_graph_subscribe((mapper_graph)$self, (mapper_device)dev,
                               subscribe_flags, timeout);
        return $self;
    }
    graph *subscribe(int subscribe_flags=0, int timeout=-1) {
        mapper_graph_subscribe((mapper_graph)$self, 0, subscribe_flags, timeout);
        return $self;
    }
    graph *unsubscribe(device *dev=0) {
        mapper_graph_unsubscribe((mapper_graph)$self, (mapper_device)dev);
        return $self;
    }
    graph *request_devices() {
        mapper_graph_request_devices((mapper_graph)$self);
        return $self;
    }
    graph *add_callback(PyObject *PyFunc, int type_flags=MAPPER_OBJ_ALL) {
        Py_XINCREF(PyFunc);
        mapper_graph_add_callback((mapper_graph)$self, graph_handler_py,
                                  type_flags, PyFunc);
        return $self;
    }
    graph *remove_callback(PyObject *PyFunc) {
        mapper_graph_remove_callback((mapper_graph)$self, graph_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
        return $self;
    }
    device_query *devices() {
        device_query *ret = malloc(sizeof(struct _device_query));
        ret->query = mapper_graph_get_objects((mapper_graph)$self, MAPPER_OBJ_DEVICE);
        return ret;
    }
    signal_query *signals() {
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = mapper_graph_get_objects((mapper_graph)$self, MAPPER_OBJ_SIGNAL);
        return ret;
    }
    map_query *maps() {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_graph_get_objects((mapper_graph)$self, MAPPER_OBJ_MAP);
        return ret;
    }
    map_query *maps_by_scope(device *dev) {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_graph_get_maps_by_scope((mapper_graph)$self,
                                                    (mapper_device)dev);
        return ret;
    }
    %pythoncode {
        interface = property(get_interface, set_interface)
        multicast_addr = property(get_multicast_addr, set_multicast_addr)
        def __nonzero__(self):
            return False if self.this is None else True
    }
}

%extend _timetag {
    _timetag() {
        mapper_time_t *tt = (mapper_time_t*)malloc(sizeof(mapper_time_t));
        mapper_time_now(tt);
        return (timetag*)tt;
    }
    _timetag(double val = 0) {
        mapper_time_t *tt = malloc(sizeof(mapper_time_t));
        mapper_time_set_double(tt, val);
        return (timetag*)tt;
    }
    ~_timetag() {
        free((mapper_time_t*)$self);
    }
    timetag *now() {
        mapper_time_now((mapper_time_t*)$self);
        return $self;
    }
    double get_double() {
        return mapper_time_get_double(*(mapper_time_t*)$self);
    }
    timetag *__add__(timetag *addend) {
        mapper_time_t *tt = malloc(sizeof(mapper_time_t));
        mapper_time_copy(tt, *(mapper_time_t*)$self);
        mapper_time_add(tt, *(mapper_time_t*)addend);
        return (timetag*)tt;
    }
    timetag *__add__(double addend) {
        mapper_time_t *tt = malloc(sizeof(mapper_time_t));
        mapper_time_copy(tt, *(mapper_time_t*)$self);
        mapper_time_add_double(tt, addend);
        return (timetag*)tt;
    }
    timetag *__iadd__(timetag *addend) {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_add(tt, *(mapper_time_t*)addend);
        return $self;
    }
    timetag *__iadd__(double addend) {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_add_double(tt, addend);
        return $self;
    }
    double __radd__(double val) {
        return val + mapper_time_get_double(*(mapper_time_t*)$self);
    }
    timetag *__sub__(timetag *subtrahend) {
        mapper_time_t *tt = malloc(sizeof(mapper_time_t));
        mapper_time_copy(tt, *(mapper_time_t*)$self);
        mapper_time_subtract(tt, *(mapper_time_t*)subtrahend);
        return (timetag*)tt;
    }
    timetag *__sub__(double subtrahend) {
        mapper_time_t *tt = malloc(sizeof(mapper_time_t));
        mapper_time_copy(tt, *(mapper_time_t*)$self);
        mapper_time_add_double(tt, -subtrahend);
        return (timetag*)tt;
    }
    timetag *__isub__(timetag *subtrahend) {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_subtract(tt, *(mapper_time_t*)subtrahend);
        return $self;
    }
    timetag *__isub__(double subtrahend) {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_add_double(tt, -subtrahend);
        return $self;
    }
    double __rsub__(double val) {
        return val - mapper_time_get_double(*(mapper_time_t*)$self);
    }
    timetag *__mul__(double multiplicand) {
        mapper_time_t *tt = malloc(sizeof(mapper_time_t));
        mapper_time_copy(tt, *(mapper_time_t*)$self);
        mapper_time_multiply(tt, multiplicand);
        return (timetag*)tt;
    }
    timetag *__imul__(double multiplicand) {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_multiply(tt, multiplicand);
        return $self;
    }
    double __rmul__(double val) {
        return val + mapper_time_get_double(*(mapper_time_t*)$self);
    }
    timetag *__div__(double divisor) {
        mapper_time_t *tt = malloc(sizeof(mapper_time_t));
        mapper_time_copy(tt, *(mapper_time_t*)$self);
        mapper_time_multiply(tt, 1/divisor);
        return (timetag*)tt;
    }
    timetag *__idiv__(double divisor) {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_multiply(tt, 1/divisor);
        return $self;
    }
    double __rdiv__(double val) {
        return val / mapper_time_get_double(*(mapper_time_t*)$self);
    }

    booltype __lt__(timetag *rhs) {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_t *rhs_tt = (mapper_time_t*)rhs;
        return (tt->sec < rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac < rhs_tt->frac));
    }
    booltype __lt__(double val) {
        return mapper_time_get_double(*(mapper_time_t*)$self) < val;
    }
    booltype __le__(timetag *rhs)
    {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_t *rhs_tt = (mapper_time_t*)rhs;
        return (tt->sec < rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac <= rhs_tt->frac));
    }
    booltype __le__(double val)
    {
        return mapper_time_get_double(*(mapper_time_t*)$self) <= val;
    }
    booltype __eq__(timetag *rhs)
    {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_t *rhs_tt = (mapper_time_t*)rhs;
        return (tt->sec == rhs_tt->sec && tt->frac == rhs_tt->frac);
    }
    booltype __eq__(double val)
    {
        return mapper_time_get_double(*(mapper_time_t*)$self) == val;
    }
    booltype __ge__(timetag *rhs)
    {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_t *rhs_tt = (mapper_time_t*)rhs;
        return (tt->sec > rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac >= rhs_tt->frac));
    }
    booltype __ge__(double val)
    {
        return mapper_time_get_double(*(mapper_time_t*)$self) >= val;
    }
    booltype __gt__(timetag *rhs)
    {
        mapper_time_t *tt = (mapper_time_t*)$self;
        mapper_time_t *rhs_tt = (mapper_time_t*)rhs;
        return (tt->sec > rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac > rhs_tt->frac));
    }
    booltype __gt__(double val)
    {
        return mapper_time_get_double(*(mapper_time_t*)$self) > val;
    }
}
