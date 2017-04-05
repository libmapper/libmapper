%module mapper
%include "typemaps.i"
%typemap(in) PyObject *PyFunc {
    if ($input!=Py_None && !PyCallable_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "Need a callable object!");
        return NULL;
    }
    $1 = $input;
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
typedef struct _link {} link__;
typedef struct _signal {} signal__;
typedef struct _map {} map;
typedef struct _slot {} slot;
typedef struct _database {} database;
typedef struct _network {} network;
typedef struct _timetag {} timetag;

typedef struct _device_query {
    mapper_device *query;
} device_query;

typedef struct _link_query {
    mapper_link *query;
} link_query;

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
            // only strings (bytes in py3) are valid
            if (prop->length > 1) {
                char **str_to = (char**)prop->value;
                for (i = 0; i < prop->length; i++) {
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
                char **str_to = (char**)&prop->value;
                *str_to = strdup(PyBytes_AsString(from));
#else
                if (!PyString_Check(from))
                    return 1;
                char **str_to = (char**)&prop->value;
                *str_to = strdup(PyString_AsString(from));
#endif
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
  if (PyInt_Check(v) || PyLong_Check(v) || PyBool_Check(v)) {
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
    else if (PyString_Check(v)
             || PyUnicode_Check(v)
#if PY_MAJOR_VERSION >= 3
             || PyBytes_Check(v)
#endif
             ) {
        if (*c == 0)
            *c = 's';
        else if (*c != 's' && *c != 'c')
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
#if PY_MAJOR_VERSION >= 3
                    PyList_SetItem(v, i, PyBytes_FromString(vect[i]));
#else
                    PyList_SetItem(v, i, PyString_FromString(vect[i]));
#endif
            }
            else
#if PY_MAJOR_VERSION >= 3
                v = PyBytes_FromString((char*)prop->value);
#else
                v = PyString_FromString((char*)prop->value);
#endif
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
                for (i=0; i<prop->length; i++) {
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
#define link link__

/* Wrapper for callback back to python when a mapper_signal handler is
 * called. */
static void signal_handler_py(mapper_signal sig, mapper_id id,
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
    PyObject *py_tt = SWIG_NewPointerObj(SWIG_as_voidptr(tt),
                                         SWIGTYPE_p__timetag, 0);
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
                arglist = Py_BuildValue("(OLOO)", py_sig, id, valuelist, py_tt);
            }
            else
                arglist = Py_BuildValue("(OLiO)", py_sig, id, *(int*)value,
                                        py_tt);
        }
        else if (type == 'f') {
            if (length > 1 || count > 1) {
                float *vfloat = (float*)value;
                valuelist = PyList_New(length * count);
                for (i=0; i<length * count; i++) {
                    PyObject *o = Py_BuildValue("f", vfloat[i]);
                    PyList_SET_ITEM(valuelist, i, o);
                }
                arglist = Py_BuildValue("(OLOO)", py_sig, id, valuelist, py_tt);
            }
            else
                arglist = Py_BuildValue("(OLfO)", py_sig, id, *(float*)value,
                                        py_tt);
        }
    }
    else {
        arglist = Py_BuildValue("(OiOO)", py_sig, id, Py_None, py_tt);
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
static void instance_event_handler_py(mapper_signal sig, mapper_id id,
                                      mapper_instance_event event,
                                      mapper_timetag_t *tt)
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
    PyObject **callbacks = (PyObject**)mapper_signal_user_data(sig);
    result = PyEval_CallObject(callbacks[1], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a device map handler is called. */
static void device_link_handler_py(mapper_device dev, mapper_link link,
                                   mapper_record_event event)
{
    PyEval_RestoreThread(_save);

    PyObject *py_link = SWIG_NewPointerObj(SWIG_as_voidptr(link),
                                           SWIGTYPE_p__link, 0);

    PyObject *arglist = Py_BuildValue("Oi", py_link, event);
    if (!arglist) {
        printf("[mapper] Could not build arglist (device_link_handler_py).\n");
        return;
    }
    PyObject **callbacks = mapper_device_user_data(dev);
    if (!callbacks || !callbacks[0]) {
        printf("[mapper] Could not retrieve callback (device_map_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject(callbacks[0], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a device map handler is called. */
static void device_map_handler_py(mapper_device dev, mapper_map map,
                                  mapper_record_event event)
{
    PyEval_RestoreThread(_save);

    PyObject *py_map = SWIG_NewPointerObj(SWIG_as_voidptr(map),
                                          SWIGTYPE_p__map, 0);

    PyObject *arglist = Py_BuildValue("Oi", py_map, event);
    if (!arglist) {
        printf("[mapper] Could not build arglist (device_map_handler_py).\n");
        return;
    }
    PyObject **callbacks = mapper_device_user_data(dev);
    if (!callbacks || !callbacks[1]) {
        printf("[mapper] Could not retrieve callback (device_map_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject(callbacks[1], arglist);
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
static void database_device_handler_py(mapper_database db, mapper_device dev,
                                       mapper_record_event event,
                                       const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_dev = SWIG_NewPointerObj(SWIG_as_voidptr(dev),
                                          SWIGTYPE_p__device, 0);

    PyObject *arglist = Py_BuildValue("(Oi)", py_dev, event);
    if (!arglist) {
        printf("[mapper] Could not build arglist (database_device_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_database_device handler
 * is called. */
static void database_link_handler_py(mapper_database db, mapper_link link,
                                     mapper_record_event event,
                                     const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_link = SWIG_NewPointerObj(SWIG_as_voidptr(link),
                                           SWIGTYPE_p__link, 0);

    PyObject *arglist = Py_BuildValue("(Oi)", py_link, event);
    if (!arglist) {
        printf("[mapper] Could not build arglist (database_link_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_database_signal handler
 * is called. */
static void database_signal_handler_py(mapper_database db, mapper_signal sig,
                                       mapper_record_event event,
                                       const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_sig = SWIG_NewPointerObj(SWIG_as_voidptr(sig),
                                          SWIGTYPE_p__signal, 0);

    PyObject *arglist = Py_BuildValue("(Oi)", py_sig, event);
    if (!arglist) {
        printf("[mapper] Could not build arglist (database_signal_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_database_map handler
 * is called. */
static void database_map_handler_py(mapper_database db, mapper_map map,
                                    mapper_record_event event,
                                    const void *user)
{
    PyEval_RestoreThread(_save);

    PyObject *py_map = SWIG_NewPointerObj(SWIG_as_voidptr(map),
                                          SWIGTYPE_p__map, 0);

    PyObject *arglist = Py_BuildValue("(Oi)", py_map, event);
    if (!arglist) {
        printf("[mapper] Could not build arglist (database_map_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

static mapper_signal add_signal_internal(mapper_device dev, mapper_direction dir,
                                         int num_instances, const char *name,
                                         int length, const char type,
                                         const char *unit,
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
    mapper_signal sig = mapper_device_add_signal(dev, dir, num_instances, name,
                                                 length, type, unit, pmn, pmx,
                                                 h, callbacks);
    if (pmn_coerced)
        free(pmn);
    if (pmx_coerced)
        free(pmx);
    return sig;
}

%}

/*! Possible object types for subscriptions. */
%constant int OBJ_NONE                  = MAPPER_OBJ_NONE;
%constant int OBJ_DEVICES               = MAPPER_OBJ_DEVICES;
%constant int OBJ_INPUT_SIGNALS         = MAPPER_OBJ_INPUT_SIGNALS;
%constant int OBJ_OUTPUT_SIGNALS        = MAPPER_OBJ_OUTPUT_SIGNALS;
%constant int OBJ_SIGNALS               = MAPPER_OBJ_SIGNALS;
%constant int OBJ_INCOMING_LINKS        = MAPPER_OBJ_INCOMING_LINKS;
%constant int OBJ_OUTGOING_LINKS        = MAPPER_OBJ_OUTGOING_LINKS;
%constant int OBJ_LINKS                 = MAPPER_OBJ_LINKS;
%constant int OBJ_INCOMING_MAPS         = MAPPER_OBJ_INCOMING_MAPS;
%constant int OBJ_OUTGOING_MAPS         = MAPPER_OBJ_OUTGOING_MAPS;
%constant int OBJ_MAPS                  = MAPPER_OBJ_MAPS;
%constant int OBJ_ALL                   = MAPPER_OBJ_ALL;

/*! Possible operations for composing database queries. */
%constant int OP_DOES_NOT_EXIST         = MAPPER_OP_DOES_NOT_EXIST;
%constant int OP_EQUAL                  = MAPPER_OP_EQUAL;
%constant int OP_EXISTS                 = MAPPER_OP_EXISTS;
%constant int OP_GREATER_THAN           = MAPPER_OP_GREATER_THAN;
%constant int OP_GREATER_THAN_OR_EQUAL  = MAPPER_OP_GREATER_THAN_OR_EQUAL;
%constant int OP_LESS_THAN              = MAPPER_OP_LESS_THAN;
%constant int OP_LESS_THAN_OR_EQUAL     = MAPPER_OP_LESS_THAN_OR_EQUAL;
%constant int OP_NOT_EQUAL              = MAPPER_OP_NOT_EQUAL;

/*! Describes what happens when the range boundaries are exceeded. */
%constant int BOUND_UNDEFINED           = MAPPER_BOUND_UNDEFINED;
%constant int BOUND_NONE                = MAPPER_BOUND_NONE;
%constant int BOUND_MUTE                = MAPPER_BOUND_MUTE;
%constant int BOUND_CLAMP               = MAPPER_BOUND_CLAMP;
%constant int BOUND_FOLD                = MAPPER_BOUND_FOLD;
%constant int BOUND_WRAP                = MAPPER_BOUND_WRAP;

/*! Describes the map modes. */
%constant int MODE_LINEAR               = MAPPER_MODE_LINEAR;
%constant int MODE_EXPRESSION           = MAPPER_MODE_EXPRESSION;

/*! Describes the possible locations for map stream processing. */
%constant int LOC_SOURCE                = MAPPER_LOC_SOURCE;
%constant int LOC_DESTINATION           = MAPPER_LOC_DESTINATION;

/*! The set of possible directions for a signal or mapping slot. */
%constant int DIR_ANY                   = MAPPER_DIR_ANY;
%constant int DIR_INCOMING              = MAPPER_DIR_INCOMING;
%constant int DIR_OUTGOING              = MAPPER_DIR_OUTGOING;

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

/*! The set of possible events for a database record, used to inform callbacks
 *  of what is happening to a record. */
%constant int ADDED                     = MAPPER_ADDED;
%constant int MODIFIED                  = MAPPER_MODIFIED;
%constant int REMOVED                   = MAPPER_REMOVED;
%constant int EXPIRED                   = MAPPER_EXPIRED;

%constant char* version                 = PACKAGE_VERSION;

typedef struct _device {} device;
typedef struct _link {} link;
typedef struct _signal {} signal;
typedef struct _map {} map;
typedef struct _slot {} slot;
typedef struct _database {} database;
typedef struct _network {} network;
typedef struct _timetag {} timetag;

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
    device_query *join(device_query *d) {
        if (!d || !d->query)
            return $self;
        // need to use a copy of query
        mapper_device *copy = mapper_device_query_copy(d->query);
        $self->query = mapper_device_query_union($self->query, copy);
        return $self;
    }
    device_query *intersect(device_query *d) {
        if (!d || !d->query)
            return $self;
        // need to use a copy of query
        mapper_device *copy = mapper_device_query_copy(d->query);
        $self->query = mapper_device_query_intersection($self->query, copy);
        return $self;
    }
    device_query *subtract(device_query *d) {
        if (!d || !d->query)
            return $self;
        // need to use a copy of query
        mapper_device *copy = mapper_device_query_copy(d->query);
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
        PyObject **callbacks = mapper_device_user_data((mapper_device)$self);
        if (callbacks) {
            if (callbacks[0])
                Py_XDECREF(callbacks[0]);
            if (callbacks[1])
                Py_XDECREF(callbacks[1]);
            free(callbacks);
        }
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
    signal *add_signal(mapper_direction dir, int num_instances,
                       const char *name, int length=1,
                       const char type='f', const char *unit=0,
                       property_value minimum=0, property_value maximum=0,
                       PyObject *PyFunc=0)
    {
        return (signal*)add_signal_internal((mapper_device)$self, dir,
                                            num_instances, name,
                                            length, type, unit, minimum,
                                            maximum, PyFunc);
    }
    signal *add_input_signal(const char *name, int length=1,
                             const char type='f', const char *unit=0,
                             property_value minimum=0, property_value maximum=0,
                             PyObject *PyFunc=0)
    {
        return (signal*)add_signal_internal((mapper_device)$self,
                                            MAPPER_DIR_INCOMING, 1, name, length,
                                            type, unit, minimum, maximum,
                                            PyFunc);
    }
    signal *add_output_signal(const char *name, int length=1, const char type='f',
                              const char *unit=0, property_value minimum=0,
                              property_value maximum=0, PyObject *PyFunc=0)
    {
        return (signal*)add_signal_internal((mapper_device)$self,
                                            MAPPER_DIR_OUTGOING, 1, name, length,
                                            type, unit, minimum, maximum,
                                            PyFunc);
    }
    device *remove_signal(signal *sig) {
        mapper_signal msig = (mapper_signal)sig;
        if (msig->user_data) {
            PyObject **callbacks = msig->user_data;
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

    // link and map callbacks
    device *set_link_callback(PyObject *PyFunc=0) {
        void *h = 0;
        PyObject **callbacks = mapper_device_user_data((mapper_device)$self);
        if (!callbacks) {
            callbacks = calloc(1, 2 * sizeof(PyObject*));
            mapper_device_set_user_data((mapper_device)$self, callbacks);
        }
        else {
            if (callbacks[0] == PyFunc)
                return $self;
            if (callbacks[0])
                Py_XDECREF(callbacks[0]);
        }
        if (PyFunc) {
            Py_XINCREF(PyFunc);
            callbacks[0] = PyFunc;
            h = device_link_handler_py;
        }
        else
            callbacks[0] = 0;
        mapper_device_set_link_callback((mapper_device)$self, h);
        return $self;
    }
    device *set_map_callback(PyObject *PyFunc=0) {
        void *h = 0;
        PyObject **callbacks = mapper_device_user_data((mapper_device)$self);
        if (!callbacks) {
            callbacks = calloc(1, 2 * sizeof(PyObject*));
            mapper_device_set_user_data((mapper_device)$self, callbacks);
        }
        else {
            if (callbacks[1] == PyFunc)
                return $self;
            if (callbacks[1])
                Py_XDECREF(callbacks[1]);
        }
        if (PyFunc) {
            Py_XINCREF(PyFunc);
            callbacks[1] = PyFunc;
            h = device_map_handler_py;
        }
        else
            callbacks[1] = 0;
        mapper_device_set_map_callback((mapper_device)$self, h);
        return $self;
    }

    // queue management
    timetag *start_queue(timetag *py_tt=0) {
        if (py_tt) {
            mapper_timetag_t *tt = (mapper_timetag_t*)py_tt;
            mapper_device_start_queue((mapper_device)$self, *tt);
            return py_tt;
        }
        else {
            mapper_timetag_t *tt = (mapper_timetag_t*)malloc(sizeof(mapper_timetag_t));
            mapper_timetag_now(tt);
            mapper_device_start_queue((mapper_device)$self, *tt);
            return (timetag*)tt;
        }
    }
    device *send_queue(timetag *py_tt) {
        mapper_timetag_t *tt = (mapper_timetag_t*)py_tt;
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
    booltype get_is_ready() {
        return mapper_device_ready((mapper_device)$self);
    }
    const char *get_name() {
        return mapper_device_name((mapper_device)$self);
    }
    int get_num_signals(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_device_num_signals((mapper_device)$self, dir);
    }
    int get_num_links(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_device_num_links((mapper_device)$self, dir);
    }
    int get_num_maps(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_device_num_maps((mapper_device)$self, dir);
    }
    unsigned int get_ordinal() {
        return mapper_device_ordinal((mapper_device)$self);
    }
    maybeInt get_port() {
        int *pi = 0, port = mapper_device_port((mapper_device)$self);
        if (port) {
            pi = malloc(sizeof(int));
            *pi = port;
        }
        return pi;
    }
    timetag *get_synced() {
        mapper_timetag_t *tt = (mapper_timetag_t*)malloc(sizeof(mapper_timetag_t));
        mapper_device_synced((mapper_device)$self, tt);
        return (timetag*)tt;
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
    device *set_description(const char *description) {
        mapper_device_set_description((mapper_device)$self, description);
        return $self;
    }
    device *set_property(const char *key, property_value val=0,
                         booltype publish=1) {
        if (!key || strcmp(key, "user_data")==0)
            return $self;;
        if (val)
            mapper_device_set_property((mapper_device)$self, key, val->length,
                                       val->type, val->value, publish);
        else
            mapper_device_remove_property((mapper_device)$self, key);
        return $self;
    }
    device *remove_property(const char *key) {
        if (key && strcmp(key, "user_data"))
            mapper_device_remove_property((mapper_device)$self, key);
        return $self;
    }

    device *clear_staged_properties() {
        mapper_device_clear_staged_properties((mapper_device)$self);
        return $self;
    }
    device *push() {
        mapper_device_push((mapper_device)$self);
        return $self;
    }

    // signal getters
    signal *signal(mapper_id id) {
        return (signal*)mapper_device_signal_by_id((mapper_device)$self, id);
    }
    signal *signal(const char *name) {
        return (signal*)mapper_device_signal_by_name((mapper_device)$self, name);
    }
    signal_query *signals(mapper_direction dir=MAPPER_DIR_ANY) {
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = mapper_device_signals((mapper_device)$self, dir);
        return ret;
    }

    // link getters
    link_query *links(mapper_direction dir=MAPPER_DIR_ANY) {
        link_query *ret = malloc(sizeof(struct _link_query));
        ret->query = mapper_device_links((mapper_device)$self, dir);
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
        num_signals = property(get_num_signals)
        num_links = property(get_num_links)
        num_maps = property(get_num_maps)
        num_properties = property(get_num_properties)
        ordinal = property(get_ordinal)
        port = property(get_port)
        ready = property(get_is_ready)
        synced = property(get_synced)
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

typedef struct _link_query {
    mapper_link *query;
} link_query;

%exception _link_query::next {
    assert(!my_error);
    $action
    if (my_error) {
        my_error = 0;
        PyErr_SetString(PyExc_StopIteration, "End of list");
        return NULL;
    }
}

%extend _link_query {
    _link_query(const link_query *orig) {
        struct _link_query *l = malloc(sizeof(struct _link_query));
        l->query = mapper_link_query_copy(orig->query);
        return l;
    }
    ~_link_query() {
        mapper_link_query_done($self->query);
        free($self);
    }
    struct _link_query *__iter__() {
        return $self;
    }
    link *next() {
        mapper_link result = 0;
        if ($self->query) {
            result = *($self->query);
            $self->query = mapper_link_query_next($self->query);
        }
        if (result)
            return (link*)result;
        my_error = 1;
        return NULL;
    }
    link_query *join(link_query *l) {
        if (!l || !l->query)
            return $self;
        // need to use a copy of query
        mapper_link *copy = mapper_link_query_copy(l->query);
        $self->query = mapper_link_query_union($self->query, copy);
        return $self;
    }
    link_query *intersect(link_query *l) {
        if (!l || !l->query)
            return $self;
        // need to use a copy of query
        mapper_link *copy = mapper_link_query_copy(l->query);
        $self->query = mapper_link_query_intersection($self->query, copy);
        return $self;
    }
    link_query *subtract(link_query *l) {
        if (!l || !l->query)
            return $self;
        // need to use a copy of query
        mapper_link *copy = mapper_link_query_copy(l->query);
        $self->query = mapper_link_query_difference($self->query, copy);
        return $self;
    }
}

%extend _link {
    // functions
    /* Note, these functions return memory which is _not_ owned by Python.
     * Correspondingly, the SWIG default is to set thisown to False, which is
     * correct for this case. */
    link *clear_staged_properties() {
        mapper_link_clear_staged_properties((mapper_link)$self);
        return $self;
    }
    link *push() {
        mapper_link_push((mapper_link)$self);
        return $self;
    }

    // property getters
    int get_num_properties() {
        return mapper_link_num_properties((mapper_link)$self);
    }
    mapper_id get_id() {
        return mapper_link_id((mapper_link)$self);
    }
    int get_num_maps() {
        return mapper_link_num_maps((mapper_link)$self);
    }
    property_value get_property(const char *key) {
        mapper_link link = (mapper_link)$self;
        int length;
        char type;
        const void *value;
        if (!mapper_link_property(link, key, &length, &type, &value)) {
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
        mapper_link link = (mapper_link)$self;
        const char *name;
        int length;
        char type;
        const void *value;
        if (!mapper_link_property_index(link, index, &name, &length, &type,
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
    link *set_property(const char *key, property_value val=0,
                       booltype publish=1) {
        if (!key || strcmp(key, "user_data")==0)
            return $self;;
        if (val)
            mapper_link_set_property((mapper_link)$self, key, val->length,
                                     val->type, val->value, publish);
        else
            mapper_link_remove_property((mapper_link)$self, key);
        return $self;
    }
    link *remove_property(const char *key) {
        if (key && strcmp(key, "user_data"))
            mapper_link_remove_property((mapper_link)$self, key);
        return $self;
    }

    // device getters
    // TODO: return devices as tuple instead
    device *device(int index = 0) {
        return (device*)mapper_link_device((mapper_link)$self, index);
    }

    // map getters
    map_query *maps() {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_link_maps((mapper_link)$self);
        return ret;
    }
    %pythoncode {
        id = property(get_id)
        num_maps = property(get_num_maps)
        num_properties = property(get_num_properties)
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
    signal_query *join(signal_query *s) {
        if (!s || !s->query)
            return $self;
        // need to use a copy of query
        mapper_signal *copy = mapper_signal_query_copy(s->query);
        $self->query = mapper_signal_query_union($self->query, copy);
        return $self;
    }
    signal_query *intersect(signal_query *s) {
        if (!s || !s->query)
            return $self;
        // need to use a copy of query
        mapper_signal *copy = mapper_signal_query_copy(s->query);
        $self->query = mapper_signal_query_intersection($self->query, copy);
        return $self;
    }
    signal_query *subtract(signal_query *s) {
        if (!s || !s->query)
            return $self;
        // need to use a copy of query
        mapper_signal *copy = mapper_signal_query_copy(s->query);
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
    int query_remotes(timetag *tt=0) {
        return mapper_signal_query_remotes((mapper_signal)$self,
                                           tt ? *(mapper_timetag_t*)tt : MAPPER_NOW);
    }
    signal *release_instance(int id, timetag *tt=0) {
        mapper_signal_instance_release((mapper_signal)$self, id,
                                       tt ? *(mapper_timetag_t*)tt : MAPPER_NOW);
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
                                                  flags);
        return $self;
    }
    signal *set_callback(PyObject *PyFunc=0) {
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
        mapper_signal_set_user_data((mapper_signal)$self, callbacks);
        mapper_signal_set_callback((mapper_signal)$self, h);
        return $self;
    }
    signal *update(property_value val=0, timetag *tt=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_update(sig, 0, 1,
                                 tt ? *(mapper_timetag_t*)tt : MAPPER_NOW);
            return $self;;
        }
        else if ( val->length < sig->length ||
                 (val->length % sig->length) != 0) {
            printf("Signal update requires multiples of %i values.\n",
                   sig->length);
            return $self;
        }
        int count = val->length / sig->length;
        if (coerce_prop(val, sig->type)) {
            printf("update: type mismatch\n");
            return self;
        }
        mapper_signal_update(sig, val->value, count,
                             tt ? *(mapper_timetag_t*)tt : MAPPER_NOW);
        return $self;
    }
    signal *update_instance(int id, property_value val=0, timetag *tt=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_update(sig, 0, 1,
                                 tt ? *(mapper_timetag_t*)tt : MAPPER_NOW);
            return $self;;
        }
        else if (val->length < sig->length ||
                 (val->length % sig->length) != 0) {
            printf("Signal update requires multiples of %i values.\n",
                   sig->length);
            return self;
        }
        int count = val->length / sig->length;
        if (coerce_prop(val, sig->type)) {
            printf("update: type mismatch\n");
            return self;
        }
        mapper_signal_instance_update(sig, id, val->value, count,
                                      tt ? *(mapper_timetag_t*)tt : MAPPER_NOW);
        return $self;
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
    mapper_signal_group get_group() {
        return mapper_signal_signal_group((mapper_signal)$self);
    }
    mapper_id get_id() {
        return mapper_signal_id((mapper_signal)$self);
    }
    booltype get_is_local() {
        return mapper_signal_is_local((mapper_signal)$self);
    }
    int get_instance_stealing_mode()
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
    signal *set_description(const char *description) {
        mapper_signal_set_description((mapper_signal)$self, description);
        return $self;
    }
    signal *set_group(mapper_signal_group group) {
        mapper_signal_set_group((mapper_signal)$self, group);
        return $self;
    }
    signal *set_instance_stealing_mode(int mode) {
        mapper_signal_set_instance_stealing_mode((mapper_signal)$self, mode);
        return $self;
    }
    signal *set_maximum(property_value val=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_set_maximum((mapper_signal)$self, 0);
            return $self;
        }
        else if (sig->length != val->length) {
            printf("set_maximum: value length must be %i\n", sig->length);
            return $self;
        }
        else if (coerce_prop(val, sig->type)) {
            printf("set_maximum: value type mismatch\n");
            return $self;
        }
        mapper_signal_set_maximum((mapper_signal)$self, val->value);
        return $self;
    }
    signal *set_minimum(property_value val=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            mapper_signal_set_minimum((mapper_signal)$self, 0);
            return $self;
        }
        else if (sig->length != val->length) {
            printf("set_minimum: value length must be %i\n", sig->length);
            return $self;
        }
        else if (coerce_prop(val, sig->type)) {
            printf("set_minimum: value type mismatch\n");
            return $self;
        }
        mapper_signal_set_minimum((mapper_signal)$self, val->value);
        return $self;
    }
    signal *set_rate(float rate) {
        mapper_signal_set_rate((mapper_signal)$self, rate);
        return $self;
    }
    signal *set_unit(const char *unit) {
        mapper_signal_set_unit((mapper_signal)$self, unit);
        return $self;
    }
    signal *set_property(const char *key, property_value val=0,
                         booltype publish=1) {
        if (!key || strcmp(key, "user_data")==0)
            return $self;
        if (val)
            mapper_signal_set_property((mapper_signal)$self, key, val->length,
                                       val->type, val->value, publish);
        else
            mapper_signal_remove_property((mapper_signal)$self, key);
        return $self;
    }
    signal *remove_property(const char *key) {
        if (key && strcmp(key, "user_data"))
            mapper_signal_remove_property((mapper_signal)$self, key);
        return $self;
    }

    signal *clear_staged_properties() {
        mapper_signal_clear_staged_properties((mapper_signal)$self);
        return $self;
    }
    signal *push() {
        mapper_signal_push((mapper_signal)$self);
        return $self;
    }

    map_query *maps(mapper_direction dir=MAPPER_DIR_ANY) {
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = mapper_signal_maps((mapper_signal)$self, dir);
        return ret;
    }

    %pythoncode {
        description = property(get_description, set_description)
        direction = property(get_direction)
        group = property(get_group, set_group)
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
    map_query *join(map_query *m) {
        if (!m || !m->query)
            return $self;
        // need to use a copy of query
        mapper_map *copy = mapper_map_query_copy(m->query);
        $self->query = mapper_map_query_union($self->query, copy);
        return $self;
    }
    map_query *intersect(map_query *m) {
        if (!m || !m->query)
            return $self;
        // need to use a copy of query
        mapper_map *copy = mapper_map_query_copy(m->query);
        $self->query = mapper_map_query_intersection($self->query, copy);
        return $self;
    }
    map_query *subtract(map_query *m) {
        if (!m || !m->query)
            return $self;
        // need to use a copy of query
        mapper_map *copy = mapper_map_query_copy(m->query);
        $self->query = mapper_map_query_difference($self->query, copy);
        return $self;
    }
}

%extend _map {
    _map(signal *src, signal *dst) {
        mapper_signal msig_src = (mapper_signal)src;
        mapper_signal msig_dst = (mapper_signal)dst;
        return (map*)mapper_map_new(1, &msig_src, 1, &msig_dst);
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

    // slot getters
    // TODO: return generator for source slot iterable
    slot *source(int index=0) {
        return (slot*)mapper_map_slot((mapper_map)$self, MAPPER_LOC_SOURCE, index);
    }
    slot *destination(int index=0) {
        return (slot*)mapper_map_slot((mapper_map)$self, MAPPER_LOC_DESTINATION, 0);
    }
    slot *slot(signal *sig) {
        return (slot*)mapper_map_slot_by_signal((mapper_map)$self,
                                                (mapper_signal)sig);
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
    int get_mode() {
        return mapper_map_mode((mapper_map)$self);
    }
    booltype get_muted() {
        return mapper_map_muted((mapper_map)$self);
    }
    booltype get_ready() {
        return mapper_map_ready((mapper_map)$self);
    }
    int get_num_sources() {
        return mapper_map_num_sources((mapper_map)$self);
    }
    int get_num_destinations() {
        return mapper_map_num_destinations((mapper_map)$self);
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
    map *set_description(const char *description) {
        mapper_map_set_description((mapper_map)$self, description);
        return $self;
    }
    map *set_expression(const char *expression) {
        mapper_map_set_expression((mapper_map)$self, expression);
        return $self;
    }
    map *set_mode(int mode) {
        mapper_map_set_mode((mapper_map)$self, mode);
        return $self;
    }
    map *set_muted(booltype muted) {
        mapper_map_set_muted((mapper_map)$self, muted);
        return $self;
    }
    map *set_process_location(mapper_location loc) {
        mapper_map_set_process_location((mapper_map)$self, loc);
        return $self;
    }
    map *set_property(const char *key, property_value val=0,
                      booltype publish=1) {
        if (!key || strcmp(key, "user_data")==0)
            return $self;
        if (val)
            mapper_map_set_property((mapper_map)$self, key, val->length,
                                    val->type, val->value, publish);
        else
            mapper_map_remove_property((mapper_map)$self, key);
        return $self;
    }
    map *remove_property(const char *key) {
        if (key && strcmp(key, "user_data"))
            mapper_map_remove_property((mapper_map)$self, key);
        return $self;
    }

    // property management
    map *clear_staged_properties() {
        mapper_map_clear_staged_properties((mapper_map)$self);
        return $self;
    }
    map *push() {
        mapper_map_push((mapper_map)$self);
        return $self;
    }

    %pythoncode {
        description = property(get_description, set_description)
        expression = property(get_expression, set_expression)
        id = property(get_id)
        mode = property(get_mode, set_mode)
        muted = property(get_muted, set_muted)
        num_properties = property(get_num_properties)
        num_sources = property(get_num_sources)
        num_destinations = property(get_num_destinations)
        process_location = property(get_process_location, set_process_location)
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
    int get_bound_max() {
        return mapper_slot_bound_max((mapper_slot)$self);
    }
    int get_bound_min() {
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
    booltype get_use_instances() {
        return mapper_slot_use_instances((mapper_slot)$self);
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
    slot *set_bound_max(int action) {
        mapper_slot_set_bound_max((mapper_slot)$self, action);
        return $self;
    }
    slot *set_bound_min(int action) {
        mapper_slot_set_bound_min((mapper_slot)$self, action);
        return $self;
    }
    slot *set_calibrating(booltype calibrating) {
        mapper_slot_set_calibrating((mapper_slot)$self, calibrating);
        return $self;
    }
    slot *set_causes_update(booltype causes_update) {
        mapper_slot_set_causes_update((mapper_slot)$self, causes_update);
        return $self;
    }
    slot *set_maximum(property_value val=0) {
        if (!val)
            mapper_slot_set_maximum((mapper_slot)$self, 0, 0, 0);
        else
            mapper_slot_set_maximum((mapper_slot)$self, val->length, val->type,
                                    val->value);
        return $self;
    }
    slot *set_minimum(property_value val=0) {
        if (!val)
            mapper_slot_set_minimum((mapper_slot)$self, 0, 0, 0);
        else
            mapper_slot_set_minimum((mapper_slot)$self,val->length, val->type,
                                    val->value);
        return $self;
    }
    slot *set_property(const char *key, property_value val=0,
                       booltype publish=1) {
        if (!key || strcmp(key, "user_data")==0)
            return $self;
        if (val)
            mapper_slot_set_property((mapper_slot)$self, key, val->length,
                                     val->type, val->value, publish);
        else
            mapper_slot_remove_property((mapper_slot)$self, key);
        return $self;
    }
    slot *set_use_instances(booltype use_instances) {
        mapper_slot_set_use_instances((mapper_slot)$self, use_instances);
        return $self;
    }
    slot *remove_property(const char *key) {
        if (key && strcmp(key, "user_data"))
            mapper_slot_remove_property((mapper_slot)$self, key);
        return $self;
    }
    slot *clear_staged_properties() {
        mapper_slot_clear_staged_properties((mapper_slot)$self);
        return $self;
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
        use_instances = property(get_use_instances, set_use_instances)
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
    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mapper_database_poll((mapper_database)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }
    database *subscribe(device *dev, int subscribe_flags=0, int timeout=-1) {
        mapper_database_subscribe((mapper_database)$self, (mapper_device)dev,
                                  subscribe_flags, timeout);
        return $self;
    }
    database *subscribe(int subscribe_flags=0, int timeout=-1) {
        mapper_database_subscribe((mapper_database)$self, 0, subscribe_flags,
                                  timeout);
        return $self;
    }
    database *unsubscribe(device *dev=0) {
        mapper_database_unsubscribe((mapper_database)$self, (mapper_device)dev);
        return $self;
    }
    database *request_devices() {
        mapper_database_request_devices((mapper_database)$self);
        return $self;
    }
    database *flush(int timeout=-1, int quiet=1) {
        mapper_database_flush((mapper_database)$self, timeout, quiet);
        return $self;
    }
    network *network() {
        return (network*)mapper_database_network((mapper_database)$self);
    }
    int timeout() {
        return mapper_database_timeout((mapper_database)$self);
    }
    database *set_timeout(int timeout) {
        mapper_database_set_timeout((mapper_database)$self, timeout);
        return $self;
    }
    database *add_device_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_database_add_device_callback((mapper_database)$self,
                                            database_device_handler_py, PyFunc);
        return $self;
    }
    database *remove_device_callback(PyObject *PyFunc) {
        mapper_database_remove_device_callback((mapper_database)$self,
                                               database_device_handler_py,
                                               PyFunc);
        Py_XDECREF(PyFunc);
        return $self;
    }
    database *add_link_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_database_add_link_callback((mapper_database)$self,
                                          database_link_handler_py, PyFunc);
        return $self;
    }
    database *remove_link_callback(PyObject *PyFunc) {
        mapper_database_remove_link_callback((mapper_database)$self,
                                             database_link_handler_py,
                                             PyFunc);
        Py_XDECREF(PyFunc);
        return $self;
    }
    database *add_signal_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_database_add_signal_callback((mapper_database)$self,
                                            database_signal_handler_py, PyFunc);
        return $self;
    }
    database *remove_signal_callback(PyObject *PyFunc) {
        mapper_database_remove_signal_callback((mapper_database)$self,
                                               database_signal_handler_py,
                                               PyFunc);
        Py_XDECREF(PyFunc);
        return $self;
    }
    database *add_map_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_database_add_map_callback((mapper_database)$self,
                                         database_map_handler_py, PyFunc);
        return $self;
    }
    database *remove_map_callback(PyObject *PyFunc) {
        mapper_database_remove_map_callback((mapper_database)$self,
                                            database_map_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
        return $self;
    }
    int get_num_devices() {
        return mapper_database_num_devices((mapper_database)$self);
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
    device_query *devices(const char *name) {
        mapper_device *devs;
        devs = mapper_database_devices_by_name((mapper_database)$self, name);
        device_query *ret = malloc(sizeof(struct _device_query));
        ret->query = devs;
        return ret;
    }
    device_query *devices_by_property(named_property prop=0,
                                      mapper_op op=MAPPER_OP_EQUAL) {
        device_query *ret = malloc(sizeof(struct _device_query));
        if (prop && prop->name) {
            property_value val = &prop->value;
            ret->query = mapper_database_devices_by_property((mapper_database)$self,
                                                             prop->name, val->length,
                                                             val->type, val->value, op);
        }
        return ret;
    }
    int get_num_links() {
        return mapper_database_num_links((mapper_database)$self);
    }
    mapper_link link(mapper_id id) {
        return mapper_database_link_by_id((mapper_database)$self, id);
    }
    link_query *links() {
        mapper_link *links;
        links = mapper_database_links((mapper_database)$self);
        link_query *ret = malloc(sizeof(struct _link_query));
        ret->query = links;
        return ret;
    }
    link_query *links_by_property(named_property prop=0,
                                  mapper_op op=MAPPER_OP_EQUAL) {
        link_query *ret = malloc(sizeof(struct _link_query));
        if (prop && prop->name) {
            property_value val = &prop->value;
            ret->query = mapper_database_links_by_property((mapper_database)$self,
                                                           prop->name, val->length,
                                                           val->type, val->value, op);
        }
        return ret;
    }
    int get_num_signals(mapper_direction dir=MAPPER_DIR_ANY) {
        return mapper_database_num_signals((mapper_database)$self, dir);
    }
    mapper_signal signal(mapper_id id) {
        return mapper_database_signal_by_id((mapper_database)$self, id);
    }
    signal_query *signals(mapper_direction dir=MAPPER_DIR_ANY) {
        mapper_signal *sigs;
        sigs = mapper_database_signals((mapper_database)$self, dir);
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = sigs;
        return ret;
    }
    signal_query *signals(const char *name) {
        mapper_signal *sigs;
        sigs = mapper_database_signals_by_name((mapper_database)$self, name);
        signal_query *ret = malloc(sizeof(struct _signal_query));
        ret->query = sigs;
        return ret;
    }
    signal_query *signals_by_property(named_property prop=0,
                                      mapper_op op=MAPPER_OP_EQUAL) {
        signal_query *ret = calloc(1, sizeof(struct _signal_query));
        if (prop && prop->name) {
            property_value val = &prop->value;
            ret->query = mapper_database_signals_by_property((mapper_database)$self,
                                                             prop->name, val->length,
                                                             val->type, val->value, op);
        }
        return ret;
    }
    int get_num_maps() {
        return mapper_database_num_maps((mapper_database)$self);
    }
    mapper_map map(mapper_id id) {
        return mapper_database_map_by_id((mapper_database)$self, id);
    }
    map_query *maps() {
        mapper_map *maps = mapper_database_maps((mapper_database)$self);
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = maps;
        return ret;
    }
    map_query *maps_by_property(named_property prop=0,
                                mapper_op op=MAPPER_OP_EQUAL) {
        map_query *ret = calloc(1, sizeof(struct _map_query));
        if (prop && prop->name) {
            property_value val = &prop->value;
            ret->query = mapper_database_maps_by_property((mapper_database)$self,
                                                          prop->name, val->length,
                                                          val->type, val->value, op);
        }
        return ret;
    }
    map_query *maps_by_scope(device *dev) {
        mapper_map *maps = mapper_database_maps_by_scope((mapper_database)$self,
                                                         (mapper_device)dev);
        map_query *ret = malloc(sizeof(struct _map_query));
        ret->query = maps;
        return ret;
    }
    map_query *maps_by_slot_property(named_property prop=0,
                                     mapper_op op=MAPPER_OP_EQUAL) {
        map_query *ret = malloc(sizeof(struct _map_query));
        if (prop && prop->name) {
            property_value val = &prop->value;
            ret->query = mapper_database_maps_by_slot_property((mapper_database)$self,
                                                               prop->name, val->length,
                                                               val->type, val->value, op);
        }
        return ret;
    }
    %pythoncode {
        num_devices = property(get_num_devices)
        num_signals = property(get_num_signals)
        num_links = property(get_num_links)
        num_maps = property(get_num_maps)
    }
}

%extend _network {
    _network(const char *iface=0, const char *ip=0, int port=7570) {
        return (network*)mapper_network_new(iface, ip, port);
    }
    ~_network() {
        mapper_network_free((mapper_network)$self);
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
    int get_port() {
        return mapper_network_port((mapper_network)$self);
    }
//    network *send_message() {
//        mapper_network_send_message();
//        return $self;
//    }
    %pythoncode {
        group = property(get_group)
        ip4 = property(get_ip4)
        interface = property(get_interface)
        port = property(get_port)
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

%extend _timetag {
    _timetag() {
        mapper_timetag_t *tt = (mapper_timetag_t*)malloc(sizeof(mapper_timetag_t));
        mapper_timetag_now(tt);
        return (timetag*)tt;
    }
    _timetag(double val = 0) {
        mapper_timetag_t *tt = malloc(sizeof(mapper_timetag_t));
        mapper_timetag_set_double(tt, val);
        return (timetag*)tt;
    }
    ~_timetag() {
        free((mapper_timetag_t*)$self);
    }
    timetag *now() {
        mapper_timetag_now((mapper_timetag_t*)$self);
        return $self;
    }
    double get_double() {
        return mapper_timetag_double(*(mapper_timetag_t*)$self);
    }
    timetag *__add__(timetag *addend) {
        mapper_timetag_t *tt = malloc(sizeof(mapper_timetag_t));
        mapper_timetag_copy(tt, *(mapper_timetag_t*)$self);
        mapper_timetag_add(tt, *(mapper_timetag_t*)addend);
        return (timetag*)tt;
    }
    timetag *__add__(double addend) {
        mapper_timetag_t *tt = malloc(sizeof(mapper_timetag_t));
        mapper_timetag_copy(tt, *(mapper_timetag_t*)$self);
        mapper_timetag_add_double(tt, addend);
        return (timetag*)tt;
    }
    timetag *__iadd__(timetag *addend) {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_add(tt, *(mapper_timetag_t*)addend);
        return $self;
    }
    timetag *__iadd__(double addend) {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_add_double(tt, addend);
        return $self;
    }
    double __radd__(double val) {
        return val + mapper_timetag_double(*(mapper_timetag_t*)$self);
    }
    timetag *__sub__(timetag *subtrahend) {
        mapper_timetag_t *tt = malloc(sizeof(mapper_timetag_t));
        mapper_timetag_copy(tt, *(mapper_timetag_t*)$self);
        mapper_timetag_subtract(tt, *(mapper_timetag_t*)subtrahend);
        return (timetag*)tt;
    }
    timetag *__sub__(double subtrahend) {
        mapper_timetag_t *tt = malloc(sizeof(mapper_timetag_t));
        mapper_timetag_copy(tt, *(mapper_timetag_t*)$self);
        mapper_timetag_add_double(tt, -subtrahend);
        return (timetag*)tt;
    }
    timetag *__isub__(timetag *subtrahend) {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_subtract(tt, *(mapper_timetag_t*)subtrahend);
        return $self;
    }
    timetag *__isub__(double subtrahend) {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_add_double(tt, -subtrahend);
        return $self;
    }
    double __rsub__(double val) {
        return val - mapper_timetag_double(*(mapper_timetag_t*)$self);
    }
    timetag *__mul__(double multiplicand) {
        mapper_timetag_t *tt = malloc(sizeof(mapper_timetag_t));
        mapper_timetag_copy(tt, *(mapper_timetag_t*)$self);
        mapper_timetag_multiply(tt, multiplicand);
        return (timetag*)tt;
    }
    timetag *__imul__(double multiplicand) {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_multiply(tt, multiplicand);
        return $self;
    }
    double __rmul__(double val) {
        return val + mapper_timetag_double(*(mapper_timetag_t*)$self);
    }
    timetag *__div__(double divisor) {
        mapper_timetag_t *tt = malloc(sizeof(mapper_timetag_t));
        mapper_timetag_copy(tt, *(mapper_timetag_t*)$self);
        mapper_timetag_multiply(tt, 1/divisor);
        return (timetag*)tt;
    }
    timetag *__idiv__(double divisor) {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_multiply(tt, 1/divisor);
        return $self;
    }
    double __rdiv__(double val) {
        return val / mapper_timetag_double(*(mapper_timetag_t*)$self);
    }

    booltype __lt__(timetag *rhs) {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_t *rhs_tt = (mapper_timetag_t*)rhs;
        return (tt->sec < rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac < rhs_tt->frac));
    }
    booltype __lt__(double val) {
        return mapper_timetag_double(*(mapper_timetag_t*)$self) < val;
    }
    booltype __le__(timetag *rhs)
    {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_t *rhs_tt = (mapper_timetag_t*)rhs;
        return (tt->sec < rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac <= rhs_tt->frac));
    }
    booltype __le__(double val)
    {
        return mapper_timetag_double(*(mapper_timetag_t*)$self) <= val;
    }
    booltype __eq__(timetag *rhs)
    {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_t *rhs_tt = (mapper_timetag_t*)rhs;
        return (tt->sec == rhs_tt->sec && tt->frac == rhs_tt->frac);
    }
    booltype __eq__(double val)
    {
        return mapper_timetag_double(*(mapper_timetag_t*)$self) == val;
    }
    booltype __ge__(timetag *rhs)
    {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_t *rhs_tt = (mapper_timetag_t*)rhs;
        return (tt->sec > rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac >= rhs_tt->frac));
    }
    booltype __ge__(double val)
    {
        return mapper_timetag_double(*(mapper_timetag_t*)$self) >= val;
    }
    booltype __gt__(timetag *rhs)
    {
        mapper_timetag_t *tt = (mapper_timetag_t*)$self;
        mapper_timetag_t *rhs_tt = (mapper_timetag_t*)rhs;
        return (tt->sec > rhs_tt->sec
                || (tt->sec == rhs_tt->sec && tt->frac > rhs_tt->frac));
    }
    booltype __gt__(double val)
    {
        return mapper_timetag_double(*(mapper_timetag_t*)$self) > val;
    }
}
