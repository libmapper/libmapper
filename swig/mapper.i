%module mapper
%include "typemaps.i"
%typemap(in) PyObject *PyFunc {
    if ($input!=Py_None && !PyCallable_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "Need a callable object!");
        return NULL;
    }
    $1 = $input;
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
%typemap(in) (int num_sources, const char **sources) {
    int i;
    if (PyList_Check($input)) {
        $1 = PyList_Size($input);
        $2 = (char **) malloc($1*sizeof(char*));
        for (i = 0; i < $1; i++) {
            PyObject *s = PyList_GetItem($input,i);
            if (PyString_Check(s)) {
                $2[i] = PyString_AsString(s);
            }
            else {
                free($2);
                PyErr_SetString(PyExc_ValueError,
                                "List items must be string.");
                return NULL;
            }
        }
    }
    else if (PyString_Check($input)) {
        $1 = 1;
        $2 = (char**) malloc(sizeof(char*));
        $2[0] = PyString_AsString($input);
    }
    else {
        return NULL;
    }
}
%typemap(freearg) (int num_sources, const char **sources) {
    if ($2) free($2);
}
%typemap(in) property_value %{
    property_value_t *prop = alloca(sizeof(*prop));
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
            prop->length = PyList_Size($input);
        else
            prop->length = 1;
        prop->value = malloc(prop->length * mapper_type_size(prop->type));
        prop->free_value = 1;
        if (py_to_prop($input, prop, 0)) {
            free(prop->value);
            PyErr_SetString(PyExc_ValueError, "Problem parsing property value.");
            return NULL;
        }
        $1 = prop;
    }
%}
%typemap(out) property_value {
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
%typemap(freearg) property_value {
    if ($1) {
        property_value prop = (property_value)$1;
        if (prop->free_value) {
            if (prop->value)
                free(prop->value);
        }
        else
            free(prop);
    }
}
%typemap(in) named_property %{
    named_property_t *prop = alloca(sizeof(*prop));
    if ($input == Py_None)
        $1 = 0;
        else {
            prop->name = 0;
            prop->value.type = 0;
            check_type($input, &prop->value.type, 1, 1);
            if (!prop->value.type) {
                PyErr_SetString(PyExc_ValueError,
                                "Problem determining value type.");
                return NULL;
            }
            if (PyList_Check($input))
                prop->value.length = PyList_Size($input);
            else
                prop->value.length = 1;
            prop->value.value = malloc(prop->value.length
                                       * mapper_type_size(prop->value.type));
            prop->value.free_value = 1;
            if (py_to_prop($input, &prop->value, &prop->name)) {
                free(prop->value.value);
                PyErr_SetString(PyExc_ValueError,
                                "Problem parsing property value.");
                return NULL;
            }
            $1 = prop;
        }
    %}
%typemap(out) named_property {
    if ($1) {
        named_property prop = (named_property)$1;
        $result = prop_to_py(&prop->value, prop->name);
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
%typemap(freearg) named_property {
    if ($1) {
        named_property prop = (named_property)$1;
        if (prop->value.free_value) {
            if (prop->value.value)
                free(prop->value.value);
        }
        else
            free(prop);
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
%typemap(in) booltype {
    $1 = PyObject_IsTrue($input);
 }
%typemap(out) booltype {
    PyObject *o = $1 ? Py_True : Py_False;
    Py_INCREF(o);
    return o;
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
typedef struct _slot {} slot;
typedef struct _database {} database;
typedef struct _network {} network;

typedef struct _device_query {
    mapper_device *query;
} device_query;

typedef struct _signal_query {
    mapper_signal *query;
} signal_query;

typedef struct _map_query {
    mapper_map *query;
} map_query;

typedef struct {
    void *value;
    int length;
    char free_value;
    char type;
} property_value_t, *property_value;

typedef struct {
    const char *name;
    property_value_t value;
} named_property_t, *named_property;

PyThreadState *_save;

static int py_to_prop(PyObject *from, property_value prop, const char **name)
{
    // here we are assuming sufficient memory has already been allocated
    if (!from || !prop->length)
        return 1;

    int i;

    switch (prop->type) {
        case 's':
        {
            // only strings are valid
            if (prop->length > 1) {
                char **str_to = (char**)prop->value;
                for (i = 0; i < prop->length; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
                    if (!PyString_Check(element))
                        return 1;
                    str_to[i] = strdup(PyString_AsString(element));
                }
            }
            else {
                if (!PyString_Check(from))
                    return 1;
                char **str_to = (char**)&prop->value;
                *str_to = strdup(PyString_AsString(from));
            }
            break;
        }
        case 'c':
        {
            // only strings are valid
            char *char_to = (char*)prop->value;
            if (prop->length > 1) {
                for (i = 0; i < prop->length; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
                    if (!PyString_Check(element))
                        return 1;
                    char *temp = PyString_AsString(element);
                    char_to[i] = temp[0];
                }
            }
            else {
                if (!PyString_Check(from))
                    return 1;
                char *temp = PyString_AsString(from);
                *char_to = temp[0];
            }
            break;
        }
        case 'i':
        {
            int *int_to = (int*)prop->value;
            if (prop->length > 1) {
                for (i = 0; i < prop->length; i++) {
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
        case 'f':
        {
            float *float_to = (float*)prop->value;
            if (prop->length > 1) {
                for (i = 0; i < prop->length; i++) {
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
        default:
            return 1;
            break;
    }
    return 0;
}

static int check_type(PyObject *v, char *c, int can_promote, int allow_sequence)
{
    if (PySequence_Check(v) && !PyString_Check(v)) {
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
    else if (PyInt_Check(v) || PyBool_Check(v)) {
        if (*c == 0)
            *c = 'i';
        else if (*c == 's')
            return 1;
    }
    else if (PyFloat_Check(v)) {
        if (*c == 0)
            *c = 'f';
        else if (*c == 's')
            return 1;
        else if (*c == 'i' && can_promote)
            *c = 'f';
    }
    else if (PyString_Check(v)) {
        if (*c == 0)
            *c = 's';
        else if (*c != 's' && *c != 'c')
            return 1;
    }
    return 0;
}

static PyObject *prop_to_py(property_value prop, const char *name)
{
    if (!prop->length)
        return 0;

    int i;
    PyObject *v = 0;
    if (prop->length > 1)
        v = PyList_New(prop->length);

    switch (prop->type) {
        case 's':
        case 'S':
        {
            if (prop->length > 1) {
                char **vect = (char**)prop->value;
                for (i=0; i<prop->length; i++)
                    PyList_SetItem(v, i, PyString_FromString(vect[i]));
            }
            else
                v = PyString_FromString((char*)prop->value);
            break;
        }
        case 'c':
        {
            if (prop->length > 1) {
                char *vect = (char*)prop->value;
                for (i=0; i<prop->length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("c", vect[i]));
            }
            else
                v = Py_BuildValue("c", *(char*)prop->value);
            break;
        }
        case 'i':
        {
            if (prop->length > 1) {
                int *vect = (int*)prop->value;
                for (i=0; i<prop->length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("i", vect[i]));
            }
            else
                v = Py_BuildValue("i", *(int*)prop->value);
            break;
        }
        case 'h':
        {
            if (prop->length > 1) {
                int64_t *vect = (int64_t*)prop->value;
                for (i=0; i<prop->length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("l", vect[i]));
            }
            else
                v = Py_BuildValue("l", *(int64_t*)prop->value);
            break;
        }
        case 'f':
        {
            if (prop->length > 1) {
                float *vect = (float*)prop->value;
                for (i=0; i<prop->length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("f", vect[i]));
            }
            else
                v = Py_BuildValue("f", *(float*)prop->value);
            break;
        }
        case 'd':
        {
            if (prop->length > 1) {
                double *vect = (double*)prop->value;
                for (i=0; i<prop->length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("d", vect[i]));
            }
            else
                v = Py_BuildValue("d", *(double*)prop->value);
            break;
        }
        case 't':
        {
            mapper_timetag_t *vect = (mapper_timetag_t*)prop->value;
            if (prop->length > 1) {
                for (i=0; i<prop->length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("d",
                        mapper_timetag_double(vect[i])));
            }
            else
                v = Py_BuildValue("d", mapper_timetag_double(*vect));
            break;
        }
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
static void signal_handler_py(mapper_signal sig, mapper_id instance,
                              const void *value, int count,
                              mapper_timetag_t *tt)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist=0;
    PyObject *valuelist=0;
    PyObject *result=0;
    int i;

    PyObject *py_sig = SWIG_NewPointerObj(SWIG_as_voidptr(sig),
                                          SWIGTYPE_p__signal, 0);

    double timetag = mapper_timetag_double(*tt);
    char type = mapper_signal_type(sig);
    int length = mapper_signal_length(sig);

    if (value) {
        if (type == 'i') {
            int *vint = (int*)value;
            if (length > 1 || count > 1) {
                valuelist = PyList_New(length * count);
                for (i=0; i<length * count; i++) {
                    PyObject *o = Py_BuildValue("i", vint[i]);
                    PyList_SET_ITEM(valuelist, i, o);
                }
                arglist = Py_BuildValue("(OLOd)", py_sig, instance, valuelist,
                                        timetag);
            }
            else
                arglist = Py_BuildValue("(OLid)", py_sig, instance,
                                        *(int*)value, timetag);
        }
        else if (type == 'f') {
            if (length > 1 || count > 1) {
                float *vfloat = (float*)value;
                valuelist = PyList_New(length * count);
                for (i=0; i<length * count; i++) {
                    PyObject *o = Py_BuildValue("f", vfloat[i]);
                    PyList_SET_ITEM(valuelist, i, o);
                }
                arglist = Py_BuildValue("(OLOd)", py_sig, instance, valuelist,
                                        timetag);
            }
            else
                arglist = Py_BuildValue("(OLfd)", py_sig, instance,
                                        *(float*)value, timetag);
        }
    }
    else {
        arglist = Py_BuildValue("(OiOd)", py_sig, instance, Py_None, timetag);
    }
    if (!arglist) {
        printf("[mapper] Could not build arglist (signal_handler_py).\n");
        return;
    }
    PyObject **callbacks = (PyObject**)mapper_signal_user_data(sig);
    result = PyEval_CallObject(callbacks[0], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(valuelist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_instance_event handler
 * is called. */
static void instance_event_handler_py(mapper_signal sig, mapper_id instance,
                                      int event, mapper_timetag_t *tt)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist=0;
    PyObject *result=0;

    PyObject *py_sig = SWIG_NewPointerObj(SWIG_as_voidptr(sig),
                                          SWIGTYPE_p__signal, 0);

    unsigned long long int timetag = 0;
    if (tt) {
        timetag = tt->sec;
        timetag = (timetag << 32) + tt->frac;
    }

    arglist = Py_BuildValue("(OLiL)", py_sig, instance, event, timetag);
    if (!arglist) {
        printf("[mapper] Could not build arglist (instance_event_handler_py).\n");
        return;
    }
    PyObject **callbacks = (PyObject**)mapper_signal_user_data(sig);
    result = PyEval_CallObject(callbacks[1], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a device map handler is called. */
static void device_map_handler_py(mapper_map map, mapper_record_action action,
                                  const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_map = SWIG_NewPointerObj(SWIG_as_voidptr(map),
                                          SWIGTYPE_p__map, 0);

    PyObject *arglist = Py_BuildValue("Oi", py_map, action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (device_map_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

static int coerce_prop(property_value prop, char type)
{
    int i;
    if (!prop)
        return 1;
    if (prop->type == type)
        return 0;

    switch (type) {
        case 'i':
        {
            int *to = malloc(prop->length * sizeof(int));
            if (prop->type == 'f') {
                float *from = (float*)prop->value;
                for (i = 0; i < prop->length; i++)
                    to[i] = (int)from[i];
            }
            else if (prop->type == 'd') {
                double *from = (double*)prop->value;
                for (i = 0; i < prop->length; i++)
                    to[i] = (int)from[i];
            }
            else {
                free(to);
                return 1;
            }
            if (prop->free_value)
                free(prop->value);
            prop->value = to;
            prop->free_value = 1;
            break;
        }
        case 'f':
        {
            float *to = malloc(prop->length * sizeof(float));
            if (prop->type == 'i') {
                int *from = (int*)prop->value;
                for (i = 0; i < prop->length; i++)
                    to[i] = (float)from[i];
            }
            else if (prop->type == 'd') {
                double *from = (double*)prop->value;
                for (i = 0; i < prop->length; i++)
                    to[i] = (float)from[i];
            }
            else {
                free(to);
                return 1;
            }
            if (prop->free_value)
                free(prop->value);
            prop->value = to;
            prop->free_value = 1;
            break;
        }
        case 'd':
        {
            double *to = malloc(prop->length * sizeof(double));
            if (prop->type == 'i') {
                int *from = (int*)prop->value;
                for (i = 0; i < prop->length; i++)
                    to[i] = (double)from[i];
            }
            else if (prop->type == 'f') {
                float *from = (float*)prop->value;
                for (i = 0; i < prop->length; i++)
                    to[i] = (double)from[i];
            }
            else {
                free(to);
                return 1;
            }
            if (prop->free_value)
                free(prop->value);
            prop->value = to;
            prop->free_value = 1;
            break;
        }
        default:
            return 1;
            break;
    }
    return 0;
}

typedef int* maybeInt;
typedef int booltype;

/* Wrapper for callback back to python when a mapper_database_device handler
 * is called. */
static void device_database_handler_py(mapper_device dev,
                                       mapper_record_action action,
                                       const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_dev = SWIG_NewPointerObj(SWIG_as_voidptr(dev),
                                          SWIGTYPE_p__device, 0);

    PyObject *arglist = Py_BuildValue("(Oi)", py_dev, action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (device_database_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_database_signal handler
 * is called. */
static void signal_database_handler_py(mapper_signal sig,
                                       mapper_record_action action,
                                       const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_sig = SWIG_NewPointerObj(SWIG_as_voidptr(sig),
                                          SWIGTYPE_p__signal, 0);

    PyObject *arglist = Py_BuildValue("(Oi)", py_sig, action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (signal_database_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_database_map handler
 * is called. */
static void map_database_handler_py(mapper_map map, mapper_record_action action,
                                    const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_map = SWIG_NewPointerObj(SWIG_as_voidptr(map),
                                           SWIGTYPE_p__map, 0);

    PyObject *arglist = Py_BuildValue("(Oi)", py_map, action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (map_database_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

static mapper_signal add_signal_internal(mapper_device dev, mapper_direction dir,
                                         const char *name, int length,
                                         const char type, const char *unit,
                                         property_value minimum,
                                         property_value maximum,
                                         PyObject *PyFunc)
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
    if (type == 'f')
    {
        if (minimum && minimum->length == length) {
            if (minimum->type == 'f')
                pmn = minimum->value;
            else if (minimum->type == 'i') {
                float *to = (float*)malloc(length * sizeof(float));
                int *from = (int*)minimum->value;
                for (i=0; i<length; i++) {
                    to[i] = (float)from[i];
                }
                pmn = to;
                pmn_coerced = 1;
            }
        }
        if (maximum && maximum->length == length) {
            if (maximum->type == 'f')
                pmx = maximum->value;
            else if (maximum->type == 'i') {
                float *to = (float*)malloc(length * sizeof(float));
                int *from = (int*)maximum->value;
                for (i=0; i<length; i++) {
                    to[i] = (float)from[i];
                }
                pmx = to;
                pmx_coerced = 1;
            }
        }
    }
    else if (type == 'i')
    {
        if (minimum && minimum->length == length) {
            if (minimum->type == 'i')
                pmn = minimum->value;
            else if (minimum->type == 'f') {
                int *to = (int*)malloc(length * sizeof(int));
                float *from = (float*)minimum->value;
                for (i=0; i<length; i++) {
                    to[i] = (int)from[i];
                }
                pmn = to;
                pmn_coerced = 1;
            }
        }
        if (maximum && maximum->length == length) {
            if (maximum->type == 'i')
                pmx = maximum->value;
            else if (maximum->type == 'f') {
                int *to = (int*)malloc(length * sizeof(int));
                float *from = (float*)maximum->value;
                for (i=0; i<length; i++) {
                    to[i] = (int)from[i];
                }
                pmx = to;
                pmx_coerced = 1;
            }
        }
    }
    mapper_signal sig = mapper_device_add_signal(dev, dir, name, length, type,
                                                 unit, pmn, pmx, h, callbacks);
    if (pmn_coerced)
        free(pmn);
    if (pmx_coerced)
        free(pmx);
    return sig;
}

%}

typedef enum {
    MAPPER_BOUND_UNDEFINED,
    MAPPER_BOUND_NONE,    //!< Value is passed through unchanged. This is the default.
    MAPPER_BOUND_MUTE,    //!< Value is muted.
    MAPPER_BOUND_CLAMP,   //!< Value is limited to the boundary.
    MAPPER_BOUND_FOLD,    //!< Value continues in opposite direction.
    MAPPER_BOUND_WRAP,    /*!< Value appears as modulus offset at the opposite
                           *   boundary. */
    NUM_MAPPER_BOUNDARY_ACTIONS
} mapper_boundary_action;

/*! Describes the map mode. */
typedef enum {
    MAPPER_MODE_UNDEFINED,  //!< Not yet defined
    MAPPER_MODE_RAW,        //!< No type coercion
    MAPPER_MODE_LINEAR,     //!< Linear scaling
    MAPPER_MODE_EXPRESSION, //!< Expression
    NUM_MAPPER_MODES
} mapper_mode;

/*! Describes the possible locations for map stream processing. */
typedef enum {
    MAPPER_LOC_UNDEFINED,
    MAPPER_LOC_SOURCE,
    MAPPER_LOC_DESTINATION,
    NUM_MAPPER_LOCATIONS
} mapper_location;

/*! The set of possible directions for a signal or mapping slot. */
typedef enum {
    MAPPER_DIR_ANY      = 0x00,
    MAPPER_DIR_INCOMING = 0x01,
    MAPPER_DIR_OUTGOING = 0x02,
} mapper_direction;

/*! Describes the voice-stealing mode for instances. */
typedef enum {
    MAPPER_NO_STEALING,
    MAPPER_STEAL_OLDEST,    //!< Steal the oldest instance
    MAPPER_STEAL_NEWEST,    //!< Steal the newest instance
} mapper_instance_stealing_type;

/*! The set of possible actions on an instance, used to register callbacks to
 *  inform them of what is happening. */
%constant int MAPPER_NEW_INSTANCE       = 0x01;
%constant int MAPPER_UPSTREAM_RELEASE   = 0x02;
%constant int MAPPER_DOWNSTREAM_RELEASE = 0x04;
%constant int MAPPER_INSTANCE_OVERFLOW  = 0x08;
%constant int MAPPER_INSTANCE_ALL       = 0x0F;

/*! Possible object types for subscriptions. */
%constant int MAPPER_OBJ_NONE           = 0x00;
%constant int MAPPER_OBJ_DEVICES        = 0x01;
%constant int MAPPER_OBJ_INPUT_SIGNALS  = 0x02;
%constant int MAPPER_OBJ_OUTPUT_SIGNALS = 0x04;
%constant int MAPPER_OBJ_SIGNALS        = 0x06;
%constant int MAPPER_OBJ_INCOMING_MAPS  = 0x10;
%constant int MAPPER_OBJ_OUTGOING_MAPS  = 0x20;
%constant int MAPPER_OBJ_MAPS           = 0x30;
%constant int MAPPER_OBJ_ALL            = 0xFF;

/*! The set of possible actions on a database record, used to inform callbacks
 *  of what is happening to a record. */
typedef enum {
    MAPPER_ADDED,
    MAPPER_MODIFIED,
    MAPPER_REMOVED,
    MAPPER_EXPIRED,
} mapper_record_action;

typedef struct _device {} device;
typedef struct _signal {} signal;
typedef struct _map {} map;
typedef struct _slot {} slot;
typedef struct _database {} database;
typedef struct _network {} network;

typedef struct _device_query {
    mapper_device *query;
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
        d->query = mapper_device_query_copy(orig->query);
        return d;
    }
    ~_device_query() {
        mapper_device_query_done($self->query);
        free($self);
    }
    struct _device_query *__iter__() {
        return $self;
    }
    device *next() {
        mapper_device result = 0;
        if ($self->query) {
            result = *($self->query);
            $self->query = mapper_device_query_next($self->query);
        }
        if (result)
            return (device*)result;
        my_error = 1;
        return NULL;
    }
    device_query *join(device_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_device *copy = mapper_device_query_copy(q->query);
        $self->query = mapper_device_query_union($self->query, copy);
        return $self;
    }
    device_query *intersect(device_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_device *copy = mapper_device_query_copy(q->query);
        $self->query = mapper_device_query_intersection($self->query, copy);
        return $self;
    }
    device_query *subtract(device_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_device *copy = mapper_device_query_copy(q->query);
        $self->query = mapper_device_query_difference($self->query, copy);
        return $self;
    }
}

%extend _device {
    _device(const char *name, int port=0, network *DISOWN=0) {
        device *d = (device*)mapper_device_new(name, port, (mapper_network) DISOWN);
        return d;
    }
    ~_device() {
        mapper_device_free((mapper_device)$self);
    }
    network *network() {
        return (network*)mapper_device_network((mapper_device)$self);
    }
    database *database() {
        return (database*)mapper_device_database((mapper_device)$self);
    }

    // functions
    /* Note, these functions return memory which is _not_ owned by Python.
     * Correspondingly, the SWIG default is to set thisown to False, which is
     * correct for this case. */
    signal *add_signal(mapper_direction dir, const char *name, int length=1,
                       const char type='f', const char *unit=0,
                       property_value minimum=0, property_value maximum=0,
                       PyObject *PyFunc=0)
    {
        return (signal*)add_signal_internal((mapper_device)$self, dir, name,
                                            length, type, unit, minimum,
                                            maximum, PyFunc);
    }
    signal *add_input_signal(const char *name, int length=1,
                             const char type='f', const char *unit=0,
                             property_value minimum=0, property_value maximum=0,
                             PyObject *PyFunc=0)
    {
        return (signal*)add_signal_internal((mapper_device)$self,
                                            MAPPER_DIR_INCOMING, name, length,
                                            type, unit, minimum, maximum,
                                            PyFunc);
    }
    signal *add_output_signal(const char *name, int length=1, const char type='f',
                              const char *unit=0, property_value minimum=0,
                              property_value maximum=0, PyObject *PyFunc=0)
    {
        return (signal*)add_signal_internal((mapper_device)$self,
                                            MAPPER_DIR_OUTGOING, name, length,
                                            type, unit, minimum, maximum,
                                            PyFunc);
    }
    double now() {
        mapper_timetag_t tt;
        mapper_device_now((mapper_device)$self, &tt);
        return mapper_timetag_double(tt);
    }
    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mapper_device_poll((mapper_device)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }
    void push() {
        mapper_device_push((mapper_device)$self);
    }
    int ready() {
        return mapper_device_ready((mapper_device)$self);
    }
    void remove_signal(signal *sig) {
        mapper_signal msig = (mapper_signal)sig;
        if (msig->user_data) {
            PyObject **callbacks = msig->user_data;
            Py_XDECREF(callbacks[0]);
            Py_XDECREF(callbacks[1]);
            free(callbacks);
        }
        return mapper_device_remove_signal((mapper_device)$self, msig);
    }
    void send_queue(double timetag) {
        mapper_timetag_t tt;
        mapper_timetag_set_double(&tt, timetag);
        mapper_device_send_queue((mapper_device)$self, tt);
    }
    void set_map_callback(PyObject *PyFunc=0) {
        void *h = 0;
        if (PyFunc) {
            Py_XINCREF(PyFunc);
            h = device_map_handler_py;
        }
        else
            Py_XDECREF(((mapper_device)$self)->local->map_handler_userdata);
        mapper_device_set_map_callback((mapper_device)$self, h, PyFunc);
    }
    double start_queue(double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        mapper_device_start_queue((mapper_device)$self, tt);
        return mapper_timetag_double(tt);
    }
    double synced() {
        mapper_timetag_t tt;
        mapper_device_synced((mapper_device)$self, &tt);
        return mapper_timetag_double(tt);
    }
    mapper_id unique_id() {
        return mapper_device_unique_id((mapper_device)$self);
    }

    // property getters
    int get_num_properties() {
        return mapper_device_num_properties((mapper_device)$self);
    }
    const char *get_description() {
        return mapper_device_description((mapper_device)$self);
    }
    const char *get_host() {
        return mapper_device_host((mapper_device)$self);
    }
    mapper_id get_id() {
        return mapper_device_id((mapper_device)$self);
    }
    booltype get_is_local() {
        return mapper_device_is_local((mapper_device)$self);
    }
    const char *get_libversion() {
        return mapper_libversion();
    }
    const char *get_name() {
        return mapper_device_name((mapper_device)$self);
    }
    int get_num_maps(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_device_num_maps((mapper_device)$self, dir);
    }
    int get_num_signals() {
        return mapper_device_num_signals((mapper_device)$self, MAPPER_DIR_ANY);
    }
    int get_num_input_signals() {
        return mapper_device_num_signals((mapper_device)$self,
                                         MAPPER_DIR_INCOMING);
    }
    int get_num_output_signals() {
        return mapper_device_num_signals((mapper_device)$self,
                                         MAPPER_DIR_OUTGOING);
    }
    unsigned int get_ordinal() {
        return mapper_device_ordinal((mapper_device)$self);
    }
    maybeInt get_port() {
        mapper_device md = (mapper_device)$self;
        int port = mapper_device_port(md);
        if (port) {
            int *pi = malloc(sizeof(int));
            *pi = port;
        }
        return 0;
    }
    int get_version() {
        return mapper_device_version((mapper_device)$self);
    }
    property_value get_property(const char *key) {
        mapper_device dev = (mapper_device)$self;
        int length;
        char type;
        const void *value;
        if (!mapper_device_property(dev, key, &length, &type, &value)) {
            if (type == 'v') {
                // don't include user_data
                return 0;
            }
            property_value prop = malloc(sizeof(property_value_t));
            prop->length = length;
            prop->type = type;
            prop->value = (void*)value;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    named_property get_property(int index) {
        mapper_device dev = (mapper_device)$self;
        const char *name;
        int length;
        char type;
        const void *value;
        if (!mapper_device_property_index(dev, index, &name, &length, &type,
                                          &value)) {
            if (type == 'v') {
                // don't include user_data
                return 0;
            }
            named_property prop = malloc(sizeof(named_property_t));
            prop->name = name;
            prop->value.length = length;
            prop->value.type = type;
            prop->value.value = (void*)value;
            prop->value.free_value = 0;
            return prop;
        }
        return 0;
    }

    // property setters
    void set_description(const char *description) {
        mapper_device_set_description((mapper_device)$self, description);
    }
    void set_property(const char *key, property_value val=0) {
        if (!key || strcmp(key, "user_data")==0)
            return;
        if (val)
            mapper_device_set_property((mapper_device)$self, key, val->length,
                                       val->type, val->value);
        else
            mapper_device_remove_property((mapper_device)$self, key);
    }
    void remove_property(const char *key) {
        if (!key || strcmp(key, "user_data")==0)
            return;
        mapper_device_remove_property((mapper_device)$self, key);
    }

    // signal getters
    signal *signal(mapper_id id) {
        return (signal*)mapper_device_signal_by_id((mapper_device)$self, id);
    }
    signal *signal(int index, mapper_direction dir) {
        return (signal*)mapper_device_signal_by_index((mapper_device)$self,
                                                      index, dir);
    }
    signal *signal(const char *name) {
        return (signal*)mapper_device_signal_by_name((mapper_device)$self, name);
    }
    signal_query *signals(mapper_direction dir=MAPPER_DIR_ANY) {
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = mapper_device_signals((mapper_device)$self, dir);
        return ret;
    }

    // map getters
    map_query *maps(mapper_direction dir=MAPPER_DIR_ANY) {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_device_maps((mapper_device)$self, dir);
        return ret;
    }
    %pythoncode {
        description = property(get_description, set_description)
        id = property(get_id)
        is_local = property(get_is_local)
        name = property(get_name)
        num_maps = property(get_num_maps)
        num_properties = property(get_num_properties)
        num_signals = property(get_num_signals)
        num_input_signals = property(get_num_input_signals)
        num_output_signals = property(get_num_output_signals)
        ordinal = property(get_ordinal)
        port = property(get_port)
        version = property(get_version)
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
                def __setitem__(self, key, value):
                    props[key] = value
                    device.set_property(key, value)
            return propsetter(self.get_properties())
        properties = property(__propgetter)
        def set_properties(self, props):
            [self.set_property(k, props[k]) for k in props]
    }
}

typedef struct _signal_query {
    mapper_signal *query;
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
        s->query = mapper_signal_query_copy(orig->query);
        return s;
    }
    ~_signal_query() {
        mapper_signal_query_done($self->query);
        free($self);
    }
    struct _signal_query *__iter__() {
        return $self;
    }
    signal *next() {
        mapper_signal result = 0;
        if ($self->query) {
            result = *($self->query);
            $self->query = mapper_signal_query_next($self->query);
        }
        if (result)
            return (signal*)result;
        my_error = 1;
        return NULL;
    }
    signal_query *join(signal_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_signal *copy = mapper_signal_query_copy(q->query);
        $self->query = mapper_signal_query_union($self->query, copy);
        return $self;
    }
    signal_query *intersect(signal_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_signal *copy = mapper_signal_query_copy(q->query);
        $self->query = mapper_signal_query_intersection($self->query, copy);
        return $self;
    }
    signal_query *subtract(signal_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_signal *copy = mapper_signal_query_copy(q->query);
        $self->query = mapper_signal_query_difference($self->query, copy);
        return $self;
    }
}

%extend _signal {
    device *device() {
        return (device*)mapper_signal_device((mapper_signal)$self);
    }

    // functions
    int active_instance_id(int index) {
        return mapper_signal_active_instance_id((mapper_signal)$self, index);
    }
    int instance_id(int index) {
        return mapper_signal_instance_id((mapper_signal)$self, index);
    }
    void push() {
        mapper_signal_push((mapper_signal)$self);
    }
    int query_remotes(double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        return mapper_signal_query_remotes((mapper_signal)$self, tt);
    }
    void release_instance(int id, double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        mapper_signal_instance_release((mapper_signal)$self, id, tt);
    }
    void remove_instance(int id) {
        mapper_signal_remove_instance((mapper_signal)$self, id);
    }
    void reserve_instances(int num=1) {
        mapper_signal_reserve_instances((mapper_signal)$self, num, 0, 0);
    }
    void reserve_instances(int num_ids, mapper_id *argv) {
        mapper_signal_reserve_instances((mapper_signal)$self, num_ids, argv, 0);
    }
    void set_instance_event_callback(PyObject *PyFunc=0, int flags=0) {
        mapper_instance_event_handler *h = 0;
        mapper_signal sig = (mapper_signal)$self;
        PyObject **callbacks = (PyObject**)sig->user_data;
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
        mapper_signal_set_instance_event_callback((mapper_signal)$self, h,
                                                  flags, callbacks);
    }
    void set_callback(PyObject *PyFunc=0) {
        mapper_signal_update_handler *h = 0;
        mapper_signal sig = (mapper_signal)$self;
        PyObject **callbacks = (PyObject**)sig->user_data;
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
        mapper_signal_set_callback((mapper_signal)$self, h, callbacks);
    }
    void update(property_value val=0, double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_update(sig, 0, 1, tt);
            return;
        }
        else if ( val->length < sig->length ||
                 (val->length % sig->length) != 0) {
            printf("Signal update requires multiples of %i values.\n",
                   sig->length);
            return;
        }
        int count = val->length / sig->length;
        if (coerce_prop(val, sig->type)) {
            printf("update: type mismatch\n");
            return;
        }
        mapper_signal_update(sig, val->value, count, tt);
    }
    void update_instance(int id, property_value val=0, double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_update(sig, 0, 1, tt);
            return;
        }
        else if (val->length < sig->length ||
                 (val->length % sig->length) != 0) {
            printf("Signal update requires multiples of %i values.\n",
                   sig->length);
            return;
        }
        int count = val->length / sig->length;
        if (coerce_prop(val, sig->type)) {
            printf("update: type mismatch\n");
            return;
        }
        mapper_signal_instance_update(sig, id, val->value, count, tt);
    }

    // property getters
    int get_num_properties() {
        return mapper_signal_num_properties((mapper_signal)$self);
    }
    const char *get_description() {
        return mapper_signal_description((mapper_signal)$self);
    }
    int get_direction() {
        return ((mapper_signal)$self)->direction;
    }
    mapper_id get_id() {
        return mapper_signal_id((mapper_signal)$self);
    }
    booltype get_is_local() {
        return mapper_signal_is_local((mapper_signal)$self);
    }
    mapper_instance_stealing_type get_instance_stealing_mode()
    {
        return mapper_signal_instance_stealing_mode((mapper_signal)$self);
    }
    int get_length() {
        return ((mapper_signal)$self)->length;
    }
    property_value get_minimum() {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->minimum) {
            property_value prop = malloc(sizeof(property_value_t));
            prop->type = sig->type;
            prop->length = sig->length;
            prop->value = sig->minimum;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    property_value get_maximum() {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->maximum) {
            property_value prop = malloc(sizeof(property_value_t));
            prop->type = sig->type;
            prop->length = sig->length;
            prop->value = sig->maximum;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    const char *get_name() {
        return ((mapper_signal)$self)->name;
    }
    int get_num_active_instances() {
        return mapper_signal_num_active_instances((mapper_signal)$self);
    }
    int get_num_instances() {
        return mapper_signal_num_instances((mapper_signal)$self);
    }
    int get_num_maps(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_signal_num_maps((mapper_signal)$self, dir);
    }
    int get_num_reserved_instances() {
        return mapper_signal_num_reserved_instances((mapper_signal)$self);
    }
    float get_rate() {
        return mapper_signal_rate((mapper_signal)$self);
    }
    char get_type() {
        return ((mapper_signal)$self)->type;
    }
    const char *get_unit() {
        return ((mapper_signal)$self)->unit;
    }
    property_value get_property(const char *key) {
        mapper_signal sig = (mapper_signal)$self;
        int length;
        char type;
        const void *value;
        if (!mapper_signal_property(sig, key, &length, &type, &value)) {
            if (type == 'v') {
                // don't include user_data
                return 0;
            }
            property_value prop = malloc(sizeof(property_value_t));
            prop->length = length;
            prop->type = type;
            prop->value = (void*)value;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    named_property get_property(int index) {
        fflush(stdout);
        mapper_signal sig = (mapper_signal)$self;
        const char *name;
        int length;
        char type;
        const void *value;
        if (!mapper_signal_property_index(sig, index, &name, &length, &type,
                                          &value)) {
            if (type == 'v') {
                // don't include user_data
                return 0;
            }
            named_property prop = malloc(sizeof(named_property_t));
            prop->name = name;
            prop->value.length = length;
            prop->value.type = type;
            prop->value.value = (void*)value;
            prop->value.free_value = 0;
            return prop;
        }
        return 0;
    }
    property_value value() {
        mapper_signal sig = (mapper_signal)$self;
        const void *value = mapper_signal_value(sig, 0);
        if (!value)
            return 0;
        property_value prop = malloc(sizeof(property_value_t));
        prop->length = sig->length;
        prop->type = sig->type;
        prop->value = (void*)value;
        return prop;
    }

    // property setters
    void set_description(const char *description) {
        mapper_signal_set_description((mapper_signal)$self, description);
    }
    void set_instance_stealing_mode(mapper_instance_stealing_type mode) {
        mapper_signal_set_instance_stealing_mode((mapper_signal)$self, mode);
    }
    void set_maximum(property_value val=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_set_maximum((mapper_signal)$self, 0);
            return;
        }
        else if (sig->length != val->length) {
            printf("set_maximum: value length must be %i\n", sig->length);
            return;
        }
        else if (coerce_prop(val, sig->type)) {
            printf("set_maximum: value type mismatch\n");
            return;
        }
        mapper_signal_set_maximum((mapper_signal)$self, val->value);
    }
    void set_minimum(property_value val=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_set_minimum((mapper_signal)$self, 0);
            return;
        }
        else if (sig->length != val->length) {
            printf("set_minimum: value length must be %i\n", sig->length);
            return;
        }
        else if (coerce_prop(val, sig->type)) {
            printf("set_minimum: value type mismatch\n");
            return;
        }
        mapper_signal_set_minimum((mapper_signal)$self, val->value);
    }
    void set_rate(float rate) {
        mapper_signal_set_rate((mapper_signal)$self, rate);
    }
    void set_unit(const char *unit) {
        mapper_signal_set_unit((mapper_signal)$self, unit);
    }
    void set_property(const char *key, property_value val=0) {
        if (!key || strcmp(key, "user_data")==0)
            return;
        if (val)
            mapper_signal_set_property((mapper_signal)$self, key, val->length,
                                       val->type, val->value);
        else
            mapper_signal_remove_property((mapper_signal)$self, key);
    }
    void remove_property(const char *key) {
        if (!key || strcmp(key, "user_data")==0)
            return;
        mapper_signal_remove_property((mapper_signal)$self, key);
    }
    map_query *maps(mapper_direction dir=MAPPER_DIR_ANY) {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_signal_maps((mapper_signal)$self, dir);
        return ret;
    }
    %pythoncode {
        description = property(get_description, set_description)
        direction = property(get_direction)
        id = property(get_id)
        instance_stealing_mode = property(get_instance_stealing_mode,
                                          set_instance_stealing_mode)
        is_local = property(get_is_local)
        length = property(get_length)
        maximum = property(get_maximum, set_maximum)
        minimum = property(get_minimum, set_minimum)
        name = property(get_name)
        num_active_instances = property(get_num_active_instances)
        num_instances = property(get_num_instances)
        num_maps = property(get_num_maps)
        num_properties = property(get_num_properties)
        num_reserved_instances = property(get_num_reserved_instances)
        rate = property(get_rate, set_rate)
        type = property(get_type)
        unit = property(get_unit, set_unit)
        def get_properties(self):
            num_props = self.num_properties
            props = {}
            for i in range(num_props):
                prop = self.get_property(i)
                if prop:
                    props[prop[0]] = prop[1];
            return props
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
    }
}

typedef struct _map_query {
    mapper_map *query;
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
        mq->query = mapper_map_query_copy(orig->query);
        return mq;
    }
    ~_map_query() {
        mapper_map_query_done($self->query);
        free($self);
    }
    struct _map_query *__iter__() {
        return $self;
    }
    map *next() {
        mapper_map result = 0;
        if ($self->query) {
            result = *($self->query);
            $self->query = mapper_map_query_next($self->query);
        }
        if (result)
            return (map*)result;
        my_error = 1;
        return NULL;
    }
    map_query *join(map_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_map *copy = mapper_map_query_copy(q->query);
        $self->query = mapper_map_query_union($self->query, copy);
        return $self;
    }
    map_query *intersect(map_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_map *copy = mapper_map_query_copy(q->query);
        $self->query = mapper_map_query_intersection($self->query, copy);
        return $self;
    }
    map_query *subtract(map_query *q) {
        if (!q || !q->query)
            return $self;
        // need to use a copy of q
        mapper_map *copy = mapper_map_query_copy(q->query);
        $self->query = mapper_map_query_difference($self->query, copy);
        return $self;
    }
}

%extend _map {
    _map(signal *src, signal *dst) {
        mapper_signal msig = (mapper_signal)src;
        return (map*)mapper_map_new(1, &msig, (mapper_signal)dst);
    }
    ~_map() {
        ;
    }
    map *push() {
        mapper_map_push((mapper_map)$self);
        return $self;
    }
    void release() {
        mapper_map_release((mapper_map)$self);
    }
    booltype ready() {
        return mapper_map_ready((mapper_map)$self);
    }

    // slot getters
    // TODO: return generator for source slot iterable
    slot *source(int index=0) {
        return (slot*)mapper_map_source_slot((mapper_map)$self, index);
    }
    slot *destination() {
        return (slot*)mapper_map_destination_slot((mapper_map)$self);
    }
    slot *slot(signal *sig) {
        return (slot*)mapper_map_slot_by_signal((mapper_map)$self,
                                                (mapper_signal)sig);
    }

    // scopes
    void add_scope(device *dev) {
        mapper_map_add_scope((mapper_map)$self, (mapper_device)dev);
    }
    void remove_scope(device *dev) {
        mapper_map_remove_scope((mapper_map)$self, (mapper_device)dev);
    }
    device_query *scopes() {
        device_query *ret = malloc(sizeof(struct _device_query));
        ret->query = mapper_map_scopes((mapper_map)$self);
        return ret;
    }

    // property getters
    int get_num_properties() {
        return mapper_map_num_properties((mapper_map)$self);
    }
    const char *get_description() {
        return mapper_map_description((mapper_map)$self);
    }
    const char *get_expression() {
        return mapper_map_expression((mapper_map)$self);
    }
    mapper_id get_id() {
        return mapper_map_id((mapper_map)$self);
    }
    mapper_mode get_mode() {
        return mapper_map_mode((mapper_map)$self);
    }
    booltype get_muted() {
        return mapper_map_muted((mapper_map)$self);
    }
    int get_num_sources() {
        return mapper_map_num_sources((mapper_map)$self);
    }
    mapper_location get_process_location() {
        return mapper_map_process_location((mapper_map)$self);
    }
    property_value get_property(const char *key) {
        mapper_map map = (mapper_map)$self;
        int length;
        char type;
        const void *value;
        if (!mapper_map_property(map, key, &length, &type, &value)) {
            if (type == 'v') {
                // don't include user_data
                return 0;
            }
            property_value prop = malloc(sizeof(property_value_t));
            prop->length = length;
            prop->type = type;
            prop->value = (void*)value;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    named_property get_property(int index) {
        mapper_map map = (mapper_map)$self;
        const char *name;
        int length;
        char type;
        const void *value;
        if (!mapper_map_property_index(map, index, &name, &length, &type,
                                       &value)) {
            if (type == 'v') {
                // don't include user_data
                return 0;
            }
            named_property prop = malloc(sizeof(named_property_t));
            prop->name = name;
            prop->value.length = length;
            prop->value.type = type;
            prop->value.value = (void*)value;
            prop->value.free_value = 0;
            return prop;
        }
        return 0;
    }

    // property setters
    void set_description(const char *description) {
        mapper_map_set_description((mapper_map)$self, description);
    }
    void set_expression(const char *expression) {
        mapper_map_set_expression((mapper_map)$self, expression);
    }
    void set_mode(mapper_mode mode) {
        mapper_map_set_mode((mapper_map)$self, mode);
    }
    void set_muted(booltype muted) {
        mapper_map_set_muted((mapper_map)$self, muted);
    }
    void set_process_location(mapper_location loc) {
        mapper_map_set_process_location((mapper_map)$self, loc);
    }
    void set_property(const char *key, property_value val=0) {
        if (!key || strcmp(key, "user_data")==0)
            return;
        if (val)
            mapper_map_set_property((mapper_map)$self, key, val->length,
                                    val->type, val->value);
        else
            mapper_map_remove_property((mapper_map)$self, key);
    }
    void remove_property(const char *key) {
        if (!key || strcmp(key, "user_data")==0)
            return;
        mapper_map_remove_property((mapper_map)$self, key);
    }
    %pythoncode {
        description = property(get_description, set_description)
        expression = property(get_expression, set_expression)
        id = property(get_id)
        mode = property(get_mode, set_mode)
        muted = property(get_muted, set_muted)
        num_properties = property(get_num_properties)
        num_sources = property(get_num_sources)
        process_location = property(get_process_location, set_process_location)
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
                def __setitem__(self, key, value):
                    props[key] = value
                    map.set_property(key, value)
            return propsetter(self.get_properties())
        properties = property(__propgetter)
        def set_properties(self, props):
            [self.set_property(k, props[k]) for k in props]
    }
}

%extend _slot {
    signal *signal() {
        return (signal*)mapper_slot_signal((mapper_slot)$self);
    }

    // property getters
    int get_num_properties() {
        return mapper_slot_num_properties((mapper_slot)$self);
    }
    mapper_boundary_action get_bound_max() {
        return mapper_slot_bound_max((mapper_slot)$self);
    }
    mapper_boundary_action get_bound_min() {
        return mapper_slot_bound_min((mapper_slot)$self);
    }
    booltype get_calibrating() {
        return mapper_slot_calibrating((mapper_slot)$self);
    }
    booltype get_causes_update() {
        return mapper_slot_causes_update((mapper_slot)$self);
    }
    int get_index() {
        return mapper_slot_index((mapper_slot)$self);
    }
    property_value get_minimum() {
        mapper_slot slot = (mapper_slot)$self;
        if (slot->minimum) {
            mapper_signal sig = mapper_slot_signal(slot);
            property_value prop = malloc(sizeof(property_value_t));
            prop->type = sig->type;
            prop->length = sig->length;
            prop->value = slot->minimum;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    property_value get_maximum() {
        mapper_slot slot = (mapper_slot)$self;
        if (slot->maximum) {
            mapper_signal sig = mapper_slot_signal(slot);
            property_value prop = malloc(sizeof(property_value_t));
            prop->type = sig->type;
            prop->length = sig->length;
            prop->value = slot->maximum;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    booltype get_use_as_instance() {
        return mapper_slot_use_as_instance((mapper_slot)$self);
    }
    property_value get_property(const char *key) {
        mapper_slot slot = (mapper_slot)$self;
        int length;
        char type;
        const void *value;
        if (!mapper_slot_property(slot, key, &length, &type, &value)) {
            if (type == 'v') {
                // don't include user_data
                return 0;
            }
            property_value prop = malloc(sizeof(property_value_t));
            prop->length = length;
            prop->type = type;
            prop->value = (void*)value;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    named_property get_property(int index) {
        mapper_slot slot = (mapper_slot)$self;
        const char *name;
        int length;
        char type;
        const void *value;
        if (!mapper_slot_property_index(slot, index, &name, &length, &type,
                                        &value)) {
            if (type == 'v') {
                // don't include user_data
                return 0;
            }
            named_property prop = malloc(sizeof(named_property_t));
            prop->value.length = length;
            prop->value.type = type;
            prop->value.value = (void*)value;
            prop->value.free_value = 0;
            return prop;
        }
        return 0;
    }

    // property setters
    void set_bound_max(mapper_boundary_action action) {
        mapper_slot_set_bound_max((mapper_slot)$self, action);
    }
    void set_bound_min(mapper_boundary_action action) {
        mapper_slot_set_bound_min((mapper_slot)$self, action);
    }
    void set_calibrating(booltype calibrating) {
        mapper_slot_set_calibrating((mapper_slot)$self, calibrating);
    }
    void set_causes_update(booltype causes_update) {
        mapper_slot_set_causes_update((mapper_slot)$self, causes_update);
    }
    void set_maximum(property_value val=0) {
        if (!val)
            mapper_slot_set_maximum((mapper_slot)$self, 0, 0, 0);
        else
            mapper_slot_set_maximum((mapper_slot)$self, val->length, val->type,
                                    val->value);
    }
    void set_minimum(property_value val=0) {
        if (!val)
            mapper_slot_set_minimum((mapper_slot)$self, 0, 0, 0);
        else
            mapper_slot_set_minimum((mapper_slot)$self,val->length, val->type,
                                    val->value);
    }
    void set_property(const char *key, property_value val=0) {
        if (!key || strcmp(key, "user_data")==0)
            return;
        if (val)
            mapper_slot_set_property((mapper_slot)$self, key, val->length,
                                     val->type, val->value);
        else
            mapper_slot_remove_property((mapper_slot)$self, key);
    }
    void set_use_as_instance(booltype use_as_instance) {
        mapper_slot_set_use_as_instance((mapper_slot)$self, use_as_instance);
    }
    void remove_property(const char *key) {
        if (!key || strcmp(key, "user_data")==0)
            return;
        mapper_slot_remove_property((mapper_slot)$self, key);
    }
    %pythoncode {
        bound_max = property(get_bound_max, set_bound_max)
        bound_min = property(get_bound_min, set_bound_min)
        calibrating = property(get_calibrating, set_calibrating)
        causes_update = property(get_causes_update, set_causes_update)
        index = property(get_index)
        maximum = property(get_maximum, set_maximum)
        minimum = property(get_minimum, set_minimum)
        num_properties = property(get_num_properties)
        use_as_instance = property(get_use_as_instance, set_use_as_instance)
        def get_properties(self):
            props = {}
            for i in range(self.num_properties):
                prop = self.get_property(i)
                if prop:
                    props[prop[0]] = prop[1];
            return props
        def __propgetter(self):
            slot = self
            props = self.get_properties()
            class propsetter(dict):
                __getitem__ = props.__getitem__
                def __setitem__(self, key, value):
                    props[key] = value
                    slot.set_property(key, value)
            return propsetter(self.get_properties())
        properties = property(__propgetter)
        def set_properties(self, props):
            [self.set_property(k, props[k]) for k in props]
    }
}

%extend _database {
    _database(network *DISOWN=0, int subscribe_flags=0x00) {
        return (database*)mapper_database_new((mapper_network)DISOWN,
                                              subscribe_flags);
    }
    ~_database() {
        mapper_database_free((mapper_database)$self);
    }
    const char *libversion() {
        return mapper_libversion();
    }
    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mapper_database_poll((mapper_database)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }
    void subscribe(device *dev, int subscribe_flags=0, int timeout=0) {
        return mapper_database_subscribe((mapper_database)$self,
                                         (mapper_device)dev,
                                         subscribe_flags, timeout);
    }
    void unsubscribe(device *dev) {
        return mapper_database_unsubscribe((mapper_database)$self,
                                           (mapper_device)dev);
    }
    void request_devices() {
        mapper_database_request_devices((mapper_database)$self);
    }
    void flush(int timeout=-1, int quiet=1) {
        mapper_database_flush((mapper_database)$self, timeout, quiet);
    }
    network *network() {
        return (network*)mapper_database_network((mapper_database)$self);
    }
    int timeout() {
        return mapper_database_timeout((mapper_database)$self);
    }
    void set_timeout(int timeout) {
        mapper_database_set_timeout((mapper_database)$self, timeout);
    }
    void add_device_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_database_add_device_callback((mapper_database)$self,
                                            device_database_handler_py, PyFunc);
    }
    void remove_device_callback(PyObject *PyFunc) {
        mapper_database_remove_device_callback((mapper_database)$self,
                                               device_database_handler_py,
                                               PyFunc);
        Py_XDECREF(PyFunc);
    }
    void add_signal_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_database_add_signal_callback((mapper_database)$self,
                                            signal_database_handler_py, PyFunc);
    }
    void remove_signal_callback(PyObject *PyFunc) {
        mapper_database_remove_signal_callback((mapper_database)$self,
                                               signal_database_handler_py,
                                               PyFunc);
        Py_XDECREF(PyFunc);
    }
    void add_map_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_database_add_map_callback((mapper_database)$self,
                                         map_database_handler_py, PyFunc);
    }
    void remove_map_callback(PyObject *PyFunc) {
        mapper_database_remove_map_callback((mapper_database)$self,
                                            map_database_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
    }
    mapper_device device(const char *device_name) {
        return mapper_database_device_by_name((mapper_database)$self,
                                              device_name);
    }
    mapper_device device(mapper_id id) {
        return mapper_database_device_by_id((mapper_database)$self, id);
    }
    device_query *devices() {
        device_query *ret = malloc(sizeof(struct _device_query));
        ret->query = mapper_database_devices((mapper_database)$self);
        return ret;
    }
    device_query *devices_by_name_match(const char *pattern) {
        device_query *ret = malloc(sizeof(struct _device_query));
        ret->query = mapper_database_devices_by_name_match((mapper_database)$self,
                                                           pattern);
        return ret;
    }
    device_query *devices_by_property(named_property prop=0,
                                      mapper_op op=MAPPER_OP_EQUAL) {
        if (!prop || !prop->name)
            return 0;
        property_value val = &prop->value;
        device_query *ret = malloc(sizeof(struct _device_query));
        ret->query = mapper_database_devices_by_property((mapper_database)$self,
                                                         prop->name, val->length,
                                                         val->type, val->value,
                                                         op);
        return ret;
    }
    device_query *local_devices() {
        device_query *ret = malloc(sizeof(struct _device_query));
        ret->query = mapper_database_local_devices((mapper_database)$self);
        return ret;
    }
    mapper_signal signal(mapper_id id) {
        return mapper_database_signal_by_id((mapper_database)$self, id);
    }
    signal_query *signals(mapper_direction dir=MAPPER_DIR_ANY) {
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = mapper_database_signals((mapper_database)$self, dir);
        return ret;
    }
    signal_query *signals_by_name(const char *name) {
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = mapper_database_signals_by_name((mapper_database)$self,
                                                     name);
        return ret;
    }
    signal_query *signals_by_name_match(const char *name) {
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = mapper_database_signals_by_name_match((mapper_database)$self,
                                                           name);
        return ret;
    }
    signal_query *signals_by_property(named_property prop=0,
                                      mapper_op op=MAPPER_OP_EQUAL) {
        if (!prop || !prop->name)
            return 0;
        property_value val = &prop->value;
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = mapper_database_signals_by_property((mapper_database)$self,
                                                         prop->name, val->length,
                                                         val->type, val->value,
                                                         op);
        return ret;
    }
    mapper_map map(mapper_id id) {
        return mapper_database_map_by_id((mapper_database)$self, id);
    }
    map_query *maps() {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_database_maps((mapper_database)$self);
        return ret;
    }
    map_query *maps_by_property(named_property prop=0,
                                mapper_op op=MAPPER_OP_EQUAL) {
        if (!prop || !prop->name)
            return 0;
        property_value val = &prop->value;
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_database_maps_by_property((mapper_database)$self,
                                                      prop->name, val->length,
                                                      val->type, val->value,
                                                      op);
        return ret;
    }
    map_query *maps_by_scope(device *dev) {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_database_maps_by_scope((mapper_database)$self,
                                                   (mapper_device)dev);
        return ret;
    }
    map_query *maps_by_slot_property(named_property prop=0,
                                     mapper_op op=MAPPER_OP_EQUAL) {
        if (!prop || !prop->name)
            return 0;
        property_value val = &prop->value;
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_database_maps_by_slot_property((mapper_database)$self,
                                                           prop->name, val->length,
                                                           val->type, val->value,
                                                           op);
        return ret;
    }
}

%extend _network {
    _network(const char *iface=0, const char *ip=0, int port=7570) {
        return (network*)mapper_network_new(iface, ip, port);
    }
    ~_network() {
        mapper_network_free((mapper_network)$self);
    }
    const char *libversion() {
        return mapper_libversion();
    }
    database *database() {
        return (database*)mapper_network_database((mapper_network)$self);
    }
    const char *get_group() {
        return mapper_network_group((mapper_network)$self);
    }
    const char *get_interface() {
        return mapper_network_interface((mapper_network)$self);
    }
    const char *get_ip4() {
        const struct in_addr *a = mapper_network_ip4((mapper_network)$self);
        return a ? inet_ntoa(*a) : 0;
    }
    double now() {
        mapper_timetag_t tt;
        mapper_network_now((mapper_network)$self, &tt);
        return mapper_timetag_double(tt);
    }
    int get_port() {
        return mapper_network_port((mapper_network)$self);
    }
//    void send_message() {
//        return mapper_network_send_message();
//    }
    %pythoncode {
        group = property(get_group)
        ip4 = property(get_ip4)
        interface = property(get_interface)
        port = property(get_port)
    }
}
