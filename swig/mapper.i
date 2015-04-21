%module mapper
%include "typemaps.i"
%typemap(in) PyObject *PyFunc {
    if ($input!=Py_None && !PyCallable_Check($input)) {
        PyErr_SetString(PyExc_TypeError, "Need a callable object!");
        return NULL;
    }
    $1 = $input;
 }
%typemap(in) (int num_int, int *argv) {
    int i;
    if (!PyList_Check($input)) {
        PyErr_SetString(PyExc_ValueError, "Expecting a list");
        return NULL;
    }
    $1 = PyList_Size($input);
    $2 = (int *) malloc($1*sizeof(int));
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
%typemap(typecheck) (int num_int, int *argv) {
    $1 = PyList_Check($input) ? 1 : 0;
}
%typemap(freearg) (int num_int, int *argv) {
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
%typemap(in) maybePropVal %{
    propval *val = alloca(sizeof(*val));
    if ($input == Py_None)
        $1 = 0;
    else {
        val->type = 0;
        check_type($input, &val->type, 1, 1);
        if (!val->type) {
            PyErr_SetString(PyExc_ValueError,
                            "Problem determining value type.");
            return NULL;
        }
        if (PyList_Check($input))
            val->length = PyList_Size($input);
        else
            val->length = 1;
        val->value = malloc(val->length * mapper_type_size(val->type));
        val->free_value = 1;
        if (py_to_prop($input, val->value, val->type, val->length)) {
            free(val->value);
            PyErr_SetString(PyExc_ValueError,
                            "Problem parsing property value.");
            return NULL;
        }
        $1 = val;
    }
%}
%typemap(out) maybePropVal {
    if ($1) {
        $result = prop_to_py($1->type, $1->length, $1->value);
        if ($result)
            free($1);
    }
    else {
        $result = Py_None;
        Py_INCREF($result);
    }
 }
%typemap(freearg) maybePropVal {
    if ($1) {
        maybePropVal prop = (maybePropVal)$1;
        if (prop->value && prop->free_value) {
            free(prop->value);
        }
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

%typemap(freearg) (int, mapper_db_connection_slot_t*) {
    if ($1) {
        if ($1->minimum)
            free($1->minimum);
        if ($1->maximum)
            free($1->maximum);
    }
}

%typemap(in) mapper_db_connection_t* %{
    mapper_db_connection_t *p = alloca(sizeof(mapper_db_connection_t));
    $1 = 0;
    if (PyDict_Check($input)) {
        memset(p, 0, sizeof(mapper_db_connection_t));
        PyObject *keys = PyDict_Keys($input);
        if (keys) {
            int i = PyList_GET_SIZE(keys), k;
            for (i=i-1; i>=0; --i) {
                PyObject *o = PyList_GetItem(keys, i);
                if (PyString_Check(o)) {
                    PyObject *v = PyDict_GetItem($input, o);
                    char *s = PyString_AsString(o);
                    if (strcmp(s, "expression")==0) {
                        if (PyString_Check(v)) {
                            p->expression = PyString_AsString(v);
                            p->flags |= CONNECTION_EXPRESSION;
                        }
                    }
                    else if (strcmp(s, "mode")==0) {
                        int ecode = SWIG_AsVal_int(v, &k);
                        if (SWIG_IsOK(ecode)) {
                            p->mode = k;
                            p->flags |= CONNECTION_MODE;
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
                            p->muted = k;
                            p->flags |= CONNECTION_MUTED;
                        }
                    }
                    else if (strcmp(s, "scope_names")==0) {
                        // could be array or single string
                        if (PyString_Check(v)) {
                            p->scope.size = 1;
                            char *scope = PyString_AsString(v);
                            p->scope.names = &scope;
                        }
                        else if (PyList_Check(v)) {
                            p->scope.size = PyList_Size(v);
                            p->scope.names = malloc(p->scope.size
                                                    * sizeof(char *));
                            for (k=0; k<p->scope.size; k++) {
                                PyObject *element = PySequence_GetItem(v, k);
                                if (!PyString_Check(element)) {
                                    p->scope.size = 0;
                                    for (--k; k>=0; k--)
                                        free(p->scope.names[k]);
                                    free(p->scope.names);
                                    break;
                                }
                                p->scope.names[k] =
                                    strdup(PyString_AsString(element));
                            }
                        }
                        if (p->scope.size > 0)
                            p->flags |= CONNECTION_SCOPE_NAMES;
                    }
                    else if (strcmp(s, "source")==0 || strcmp(s, "sources")==0) {
                        if (PyList_Check(v)) {
                            p->num_sources = PyList_Size(v);
                            p->sources = calloc(1, p->num_sources *
                                                sizeof(mapper_db_connection_slot_t));
                            for (k = 0; k < p->num_sources; k++) {
                                PyObject *element = PySequence_GetItem(v, k);
                                py_to_slot(element, &p->sources[k]);
                            }
                        }
                        else {
                            p->num_sources = 1;
                            p->sources = calloc(1, sizeof(mapper_db_connection_slot_t));
                            py_to_slot(v, &p->sources[0]);
                        }
                    }
                    else if (strcmp(s, "dest")==0 || strcmp(s, "destination")==0) {
                        py_to_slot(v, &p->destination);
                    }
                }
            }
            Py_DECREF(keys);
            $1 = p;
        }
    }
 %}

%typemap(freearg) mapper_db_connection_t* {
    if ($1) {
        if ($1->scope.size > 1) {
            int i;
            for (i=0; i<$1->scope.size; i++) {
                free($1->scope.names[i]);
            }
            free($1->scope.names);
        }
    }
}

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

static int py_to_prop(PyObject *from, void *to, char type, int length)
{
    // here we are assuming sufficient memory has already been allocated
    if (!from || !length)
        return 1;

    int i;
    PyObject *v = 0;
    if (length > 1)
        v = PyList_New(length);

    switch (type) {
        case 's':
        {
            // only strings are valid
            if (length > 1) {
                char **str_to = (char**)to;
                for (i=0; i<length; i++) {
                    PyObject *element = PySequence_GetItem(from, i);
                    if (!PyString_Check(element))
                        return 1;
                    str_to[i] = strdup(PyString_AsString(element));
                }
            }
            else {
                if (!PyString_Check(from))
                    return 1;
                char **str_to = (char**)to;
                *str_to = strdup(PyString_AsString(from));
            }
            break;
        }
        case 'c':
        {
            // only strings are valid
            char *char_to = (char*)to;
            if (length > 1) {
                for (i=0; i<length; i++) {
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
            int *int_to = (int*)to;
            if (length > 1) {
                for (i=0; i<length; i++) {
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
            float *float_to = (float*)to;
            if (length > 1) {
                for (i=0; i<length; i++) {
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

static void alloc_and_copy_maybe_vector(PyObject *v, char *type,
                                        void **value, int *length)
{
    // need to check type of arguments!
    int type_set = *type;
    if (check_type(v, type, type_set, 1)) {
        PyErr_SetString(PyExc_ValueError, "Type mismatch.");
        return;
    }
    if (*type == 0) {
        PyErr_SetString(PyExc_ValueError, "Error finding type.");
        return;
    }
    if (*value)
        free(*value);

    int obj_len = 1;
    if (PySequence_Check(v))
        obj_len = PySequence_Size(v);

    if (!*length)
        *length = obj_len;
    else if (obj_len != *length) {
        *value = 0;
        PyErr_SetString(PyExc_ValueError,
                        "Vector lengths don't match.");
        return;
    }

    *value = malloc(*length * mapper_type_size(*type));
    if (py_to_prop(v, *value, *type, *length)) {
        free(*value);
        *value = 0;
    }
}

static PyObject *prop_to_py(char type, int length, const void *value)
{
    if (!length)
        return 0;
    int i;
    PyObject *v = 0;
    if (length > 1)
        v = PyList_New(length);

    switch (type) {
        case 's':
        case 'S':
        {
            if (length > 1) {
                char **vect = (char**)value;
                for (i=0; i<length; i++)
                    PyList_SetItem(v, i, PyString_FromString(vect[i]));
            }
            else
                v = PyString_FromString((char*)value);
            break;
        }
        case 'c':
        {
            if (length > 1) {
                char *vect = (char*)value;
                for (i=0; i<length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("c", vect[i]));
            }
            else
                v = Py_BuildValue("c", *(char*)value);
            break;
        }
        case 'i':
        {
            if (length > 1) {
                int *vect = (int*)value;
                for (i=0; i<length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("i", vect[i]));
            }
            else
                v = Py_BuildValue("i", *(int*)value);
            break;
        }
        case 'h':
        {
            if (length > 1) {
                int64_t *vect = (int64_t*)value;
                for (i=0; i<length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("l", vect[i]));
            }
            else
                v = Py_BuildValue("l", *(int64_t*)value);
            break;
        }
        case 'f':
        {
            if (length > 1) {
                float *vect = (float*)value;
                for (i=0; i<length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("f", vect[i]));
            }
            else
                v = Py_BuildValue("f", *(float*)value);
            break;
        }
        case 'd':
        {
            if (length > 1) {
                double *vect = (double*)value;
                for (i=0; i<length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("d", vect[i]));
            }
            else
                v = Py_BuildValue("d", *(double*)value);
            break;
        }
        case 't':
        {
            mapper_timetag_t *vect = (mapper_timetag_t*)value;
            if (length > 1) {
                for (i=0; i<length; i++)
                    PyList_SetItem(v, i, Py_BuildValue("d",
                        mapper_timetag_get_double(vect[i])));
            }
            else
                v = Py_BuildValue("d", mapper_timetag_get_double(*vect));
            break;
        }
        default:
            return 0;
            break;
    }
    return v;
}

static PyObject *device_to_py(mapper_db_device_t *dev)
{
    if (!dev) return Py_None;
    PyObject *o = PyDict_New();
    if (!o)
        return Py_None;
    else {
        int i=0;
        const char *property;
        char type;
        const void *value;
        int length;
        while (!mapper_db_device_property_index(dev, i, &property,
                                                &type, &value, &length))
        {
            if (strcmp(property, "user_data")==0) {
                i++;
                continue;
            }
            PyObject *v = 0;
            if ((v = prop_to_py(type, length, value))) {
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
        char type;
        const void *value;
        int length;
        while (!mapper_db_signal_property_index(sig, i, &property,
                                                &type, &value, &length))
        {
            if (strcmp(property, "user_data")==0) {
                i++;
                continue;
            }
            PyObject *v = 0;
            if ((v = prop_to_py(type, length, value))) {
                PyDict_SetItemString(o, property, v);
                Py_DECREF(v);
            }
            i++;
        }
    }
    return o;
}

static PyObject *slot_to_py(mapper_db_connection_slot slot)
{
    if (!slot) return Py_None;
    PyObject *o = PyDict_New();
    if (!o)
        return Py_None;
    int i=0;
    const char *property;
    char type;
    const void *value;
    int length;
    while (!mapper_db_connection_slot_property_index(slot, i, &property,
                                                     &type, &value, &length)) {
        PyObject *v = 0;
        if ((v = prop_to_py(type, length, value))) {
            PyDict_SetItemString(o, property, v);
            Py_DECREF(v);
        }
        i++;
    }
    return o;
}

static PyObject *connection_to_py(mapper_db_connection_t *con)
{
    if (!con) return Py_None;
    PyObject *o = PyDict_New();
    if (!o)
        return Py_None;
    else {
        int i=0;
        const char *property;
        char type;
        const void *value;
        int length;
        while (!mapper_db_connection_property_index(con, i, &property,
                                                    &type, &value, &length)) {
            PyObject *v = 0;
            if ((v = prop_to_py(type, length, value))) {
                PyDict_SetItemString(o, property, v);
                Py_DECREF(v);
            }
            i++;
        }
        PyObject *srcs = 0;
        if (con->num_sources > 1) {
            srcs = PyList_New(con->num_sources);
            for (i = 0; i < con->num_sources; i++) {
                PyList_SetItem(srcs, i, slot_to_py(&con->sources[i]));
            }
        }
        else if (con->num_sources == 1)
            srcs = slot_to_py(&con->sources[0]);
        PyDict_SetItemString(o, "sources", srcs);
        PyDict_SetItemString(o, "destination", slot_to_py(&con->destination));
    }
    return o;
}

static void py_to_slot(PyObject *input, mapper_db_connection_slot slot) {
    if (!PyDict_Check(input)) {
        return;
    }
    PyObject *keys = PyDict_Keys(input);
    if (keys) {
        // first try to retrieve type if provided
        int i = PyList_GET_SIZE(keys);
        for (i=i-1; i>=0; --i) {
            PyObject *o = PyList_GetItem(keys, i);
            if (PyString_Check(o)) {
                PyObject *v = PyDict_GetItem(input, o);
                char *s = PyString_AsString(o);
                if (strcmp(s, "type")==0) {
                    if (PyString_Check(v))
                    slot->type = PyString_AsString(v)[0];
                }
            }
        }
        i = PyList_GET_SIZE(keys);
        for (i=i-1; i>=0; --i) {
            PyObject *o = PyList_GetItem(keys, i);
            if (PyString_Check(o)) {
                PyObject *v = PyDict_GetItem(input, o);
                char *s = PyString_AsString(o);
                if (strcmp(s, "min")==0 || strcmp(s, "minimum")==0) {
                    alloc_and_copy_maybe_vector(v, &slot->type, &slot->minimum,
                                                &slot->length);
                    if (slot->minimum) {
                        slot->flags |= CONNECTION_MIN_KNOWN;
                    }
                }
                else if (strcmp(s, "max")==0 || strcmp(s, "maximum")==0) {
                    alloc_and_copy_maybe_vector(v, &slot->type, &slot->maximum,
                                                &slot->length);
                    if (slot->maximum) {
                        slot->flags |= CONNECTION_MAX_KNOWN;
                    }
                }
                else if (strcmp(s, "cause_update")==0) {
                    int cause_update = -1;
                    if (v == Py_True)
                        cause_update = 1;
                    else if (v == Py_False)
                        cause_update = 0;
                    else if (PyInt_Check(v))
                        cause_update = PyInt_AsLong(v);
                    if (cause_update > -1) {
                        slot->cause_update = cause_update;
                        slot->flags |= CONNECTION_CAUSE_UPDATE;
                    }
                }
                else if (strcmp(s, "send_as_instance")==0) {
                    int send_as_instance = -1;
                    if (v == Py_True)
                        send_as_instance = 1;
                    else if (v == Py_False)
                        send_as_instance = 0;
                    else if (PyInt_Check(v))
                        send_as_instance = PyInt_AsLong(v);
                    if (send_as_instance > -1) {
                        slot->send_as_instance = send_as_instance;
                        slot->flags |= CONNECTION_SEND_AS_INSTANCE;
                    }
                }
                else if (strcmp(s, "bound_max")==0 && PyInt_Check(v)) {
                    int bound = PyInt_AsLong(v);
                    if (bound >= 0 && bound < N_MAPPER_BOUNDARY_ACTIONS) {
                        slot->bound_max = bound;
                        slot->flags |= CONNECTION_BOUND_MAX;
                    }
                }
                else if (strcmp(s, "bound_min")==0 && PyInt_Check(v)) {
                    int bound = PyInt_AsLong(v);
                    if (bound >= 0 && bound < N_MAPPER_BOUNDARY_ACTIONS) {
                        slot->bound_min = bound;
                        slot->flags |= CONNECTION_BOUND_MIN;
                    }
                }
            }
        }
        Py_DECREF(keys);
    }
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
                            int instance_id,
                            void *v,
                            int count,
                            mapper_timetag_t *tt)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist=0;
    PyObject *valuelist=0;
    PyObject *result=0;
    int i;

    PyObject *py_msig = SWIG_NewPointerObj(SWIG_as_voidptr(msig),
                                          SWIGTYPE_p__signal, 0);

    double timetag = mapper_timetag_get_double(*tt);

    if (v) {
        if (props->type == 'i') {
            int *vint = (int *)v;
            if (props->length > 1 || count > 1) {
                valuelist = PyList_New(props->length * count);
                for (i=0; i<props->length * count; i++) {
                    PyObject *o = Py_BuildValue("i", vint[i]);
                    PyList_SET_ITEM(valuelist, i, o);
                }
                arglist = Py_BuildValue("(OiOd)", py_msig, instance_id,
                                        valuelist, timetag);
            }
            else
                arglist = Py_BuildValue("(Oiid)", py_msig, instance_id,
                                        *(int*)v, timetag);
        }
        else if (props->type == 'f') {
            if (props->length > 1 || count > 1) {
                float *vfloat = (float *)v;
                valuelist = PyList_New(props->length * count);
                for (i=0; i<props->length * count; i++) {
                    PyObject *o = Py_BuildValue("f", vfloat[i]);
                    PyList_SET_ITEM(valuelist, i, o);
                }
                arglist = Py_BuildValue("(OiOd)", py_msig, instance_id,
                                        valuelist, timetag);
            }
            else
                arglist = Py_BuildValue("(Oifd)", py_msig, instance_id,
                                        *(float*)v, timetag);
        }
    }
    else {
        arglist = Py_BuildValue("(OiOd)", py_msig, instance_id,
                                Py_None, timetag);
    }
    if (!arglist) {
        printf("[mapper] Could not build arglist (msig_handler_py).\n");
        return;
    }
    PyObject **callbacks = (PyObject**)props->user_data;
    result = PyEval_CallObject(callbacks[0], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(valuelist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a mapper_signal_instance_event
 * handler is called. */
static void msig_instance_event_handler_py(struct _mapper_signal *msig,
                                           mapper_db_signal props,
                                           int instance_id,
                                           msig_instance_event_t event,
                                           mapper_timetag_t *tt)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist=0;
    PyObject *result=0;

    PyObject *py_msig = SWIG_NewPointerObj(SWIG_as_voidptr(msig),
                                          SWIGTYPE_p__signal, 0);

    unsigned long long int timetag = 0;
    if (tt) {
        timetag = tt->sec;
        timetag = (timetag << 32) + tt->frac;
    }

    arglist = Py_BuildValue("(OiiL)", py_msig, instance_id, event, timetag);
    if (!arglist) {
        printf("[mapper] Could not build arglist (msig_instance_event_handler_py).\n");
        return;
    }
    PyObject **callbacks = (PyObject**)props->user_data;
    result = PyEval_CallObject(callbacks[1], arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

/* Wrapper for callback back to python when a device connection handler is called. */
static void device_connection_handler_py(mapper_device dev,
                                         mapper_signal signal,
                                         mapper_db_connection connection,
                                         mapper_db_connection_slot slot,
                                         mapper_device_local_action_t action,
                                         void *user)
{
    PyEval_RestoreThread(_save);
    PyObject *arglist = Py_BuildValue("OOOi", device_to_py(&dev->props),
                                      signal_to_py(&signal->props),
                                      connection_to_py(connection), action);
    if (!arglist) {
        printf("[mapper] Could not build arglist (device_connection_handler_py).\n");
        return;
    }
    PyObject *result = PyEval_CallObject((PyObject*)user, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
    _save = PyEval_SaveThread();
}

typedef struct {
    char type;
    int length;
    void *value;
    int free_value;
} propval, *maybePropVal;

static int coerce_prop(maybePropVal val, char type)
{
    int i;
    if (!val)
        return 1;
    if (val->type == type)
        return 0;

    switch (type) {
        case 'i':
        {
            int *to = malloc(val->length * sizeof(int));
            if (val->type == 'f') {
                float *from = (float*)val->value;
                for (i=0; i<val->length; i++)
                    to[i] = (int)from[i];
            }
            else if (val->type == 'd') {
                double *from = (double*)val->value;
                for (i=0; i<val->length; i++)
                    to[i] = (int)from[i];
            }
            else {
                free(to);
                return 1;
            }
            if (val->free_value)
                free(val->value);
            val->value = to;
            val->free_value = 1;
            break;
        }
        case 'f':
        {
            float *to = malloc(val->length * sizeof(float));
            if (val->type == 'i') {
                int *from = (int*)val->value;
                for (i=0; i<val->length; i++)
                    to[i] = (float)from[i];
            }
            else if (val->type == 'd') {
                double *from = (double*)val->value;
                for (i=0; i<val->length; i++)
                    to[i] = (float)from[i];
            }
            else {
                free(to);
                return 1;
            }
            if (val->free_value)
                free(val->value);
            val->value = to;
            val->free_value = 1;
            break;
        }
        case 'd':
        {
            double *to = malloc(val->length * sizeof(double));
            if (val->type == 'i') {
                int *from = (int*)val->value;
                for (i=0; i<val->length; i++)
                    to[i] = (double)from[i];
            }
            else if (val->type == 'f') {
                float *from = (float*)val->value;
                for (i=0; i<val->length; i++)
                    to[i] = (double)from[i];
            }
            else {
                free(to);
                return 1;
            }
            if (val->free_value)
                free(val->value);
            val->value = to;
            val->free_value = 1;
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

%}

typedef enum _mapper_boundary_action {
    BA_UNDEFINED,   //!< Not yet defined
    BA_NONE,        //!< Value is passed through unchanged. This is the default.
    BA_MUTE,        //!< Value is muted.
    BA_CLAMP,       //!< Value is limited to the boundary.
    BA_FOLD,        //!< Value continues in opposite direction.
    BA_WRAP,        //!< Value appears as modulus offset at the opposite boundary.
    N_MAPPER_BOUNDARY_ACTIONS
} mapper_boundary_action;

/*! Describes the connection mode.
 *  @ingroup connectiondb */
typedef enum _mapper_mode_type {
    MO_UNDEFINED,    //!< Not yet defined
    MO_RAW,
    MO_LINEAR,       //!< Linear scaling
    MO_EXPRESSION,   //!< Expression
    N_MAPPER_MODE_TYPES
} mapper_mode_type;

/*! Describes the voice-stealing mode for instances.
 *  @ingroup connectiondb */
typedef enum _mapper_instance_allocation_type {
    IN_UNDEFINED,    //!< Not yet defined
    IN_STEAL_OLDEST, //!< Steal the oldest instance
    IN_STEAL_NEWEST, //!< Steal the newest instance
    N_MAPPER_INSTANCE_ALLOCATION_TYPES
} mapper_instance_allocation_type;

/*! The set of possible actions on an instance, used to register callbacks
 *  to inform them of what is happening. */
typedef enum {
    IN_NEW                  = 0x01, //!< New instance has been created.
    IN_UPSTREAM_RELEASE     = 0x02, //!< Instance released by upstream device.
    IN_DOWNSTREAM_RELEASE   = 0x04, //!< Instance released by downstream device.
    IN_OVERFLOW             = 0x08  //!< No instances left.
} msig_instance_event_t;

/*! The set of possible directions for a signal or connection slot. */
typedef enum {
    DI_OUTGOING = 0x01,
    DI_INCOMING = 0x02,
    DI_BOTH     = 0x03,
} mapper_direction_t;

/*! Possible monitor auto-subscribe settings. */
%constant int SUBSCRIBE_NONE            = 0x00;
%constant int SUBSCRIBE_DEVICE          = 0x01;
%constant int SUBSCRIBE_DEVICE_INPUTS   = 0x02;
%constant int SUBSCRIBE_DEVICE_OUTPUTS  = 0x04;
%constant int SUBSCRIBE_DEVICE_SIGNALS  = 0x06;
%constant int SUBSCRIBE_CONNECTIONS_IN  = 0x10;
%constant int SUBSCRIBE_CONNECTIONS_OUT = 0x20;
%constant int SUBSCRIBE_CONNECTIONS     = 0x30;
%constant int SUBSCRIBE_ALL             = 0xFF;

/*! The set of possible actions on a database record, used
 *  to inform callbacks of what is happening to a record. */
typedef enum {
    MDB_MODIFY,
    MDB_NEW,
    MDB_REMOVE,
    MDB_UNRESPONSIVE,
} mapper_db_action_t;

typedef enum {
    MDEV_LOCAL_ESTABLISHED,
    MDEV_LOCAL_MODIFIED,
    MDEV_LOCAL_DESTROYED,
} mapper_device_local_action_t;

typedef struct _device {} device;
typedef struct _signal {} signal;
typedef struct _monitor {} monitor;
typedef struct _db {} db;
typedef struct _admin {} admin;

%extend _device {
    _device(const char *name, int port=0, admin *DISOWN=0) {
        device *d = (device *)mdev_new(name, port, (mapper_admin) DISOWN);
        return d;
    }
    ~_device() {
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
                      const char *unit=0, maybePropVal minimum=0,
                      maybePropVal maximum=0, PyObject *PyFunc=0)
    {
        int i;
        void *h = 0;
        PyObject **callbacks = 0;
        if (PyFunc) {
            h = msig_handler_py;
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
        mapper_signal msig = mdev_add_input((mapper_device)$self, name,
                                            length, type, unit, pmn, pmx,
                                            h, callbacks);
        if (pmn_coerced)
            free(pmn);
        if (pmx_coerced)
            free(pmx);
        return (signal *)msig;
    }
    signal* add_output(const char *name, int length=1, const char type='f',
                       const char *unit=0, maybePropVal minimum=0,
                       maybePropVal maximum=0)
    {
        int i;
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
        mapper_signal msig = mdev_add_output((mapper_device)$self, name, length,
                                             type, unit, pmn, pmx);
        if (pmn_coerced)
            free(pmn);
        if (pmx_coerced)
            free(pmx);
        return (signal *)msig;
    }
    void remove_input(signal *sig) {
        mapper_signal msig = (mapper_signal)sig;
        if (msig->props.user_data) {
            PyObject **callbacks = msig->props.user_data;
            Py_XDECREF(callbacks[0]);
            Py_XDECREF(callbacks[1]);
            free(callbacks);
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
    int get_num_connections_in()
        { return mdev_num_connections_in((mapper_device)$self); }
    int get_num_connections_out()
        { return mdev_num_connections_out((mapper_device)$self); }
    signal *input(const char *name) {
        return (signal *)mdev_get_input_by_name((mapper_device)$self, name, 0);
    }
    signal *output(const char *name) {
        return (signal *)mdev_get_output_by_name((mapper_device)$self, name, 0);
    }
    signal *input(int index) {
        return (signal *)mdev_get_input_by_index((mapper_device)$self, index);
    }
    signal *output(int index) {
        return (signal *)mdev_get_output_by_index((mapper_device)$self, index);
    }
    mapper_db_device get_properties() {
        return mdev_properties((mapper_device)$self);
    }
    void set_property(const char *key, maybePropVal val=0) {
        if (val)
            mdev_set_property((mapper_device)$self, key, val->type,
                              val->value, val->length);
        else
            mdev_remove_property((mapper_device)$self, key);
    }
    void remove_property(const char *key) {
        mdev_remove_property((mapper_device)$self, key);
    }
    double now() {
        mapper_timetag_t tt;
        mdev_now((mapper_device)$self, &tt);
        return mapper_timetag_get_double(tt);
    }
    double start_queue(double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        mdev_start_queue((mapper_device)$self, tt);
        return mapper_timetag_get_double(tt);
    }
    void send_queue(double timetag) {
        mapper_timetag_t tt;
        mapper_timetag_set_double(&tt, timetag);
        mdev_send_queue((mapper_device)$self, tt);
    }
    void set_connection_callback(PyObject *PyFunc=0) {
        void *h = 0;
        if (PyFunc) {
            Py_XINCREF(PyFunc);
            h = device_connection_handler_py;
        }
        else
            Py_XDECREF(((mapper_device)$self)->connection_cb_userdata);
        mdev_set_connection_callback((mapper_device)$self, h, PyFunc);
    }
    %pythoncode {
        port = property(get_port)
        name = property(get_name)
        ip4 = property(get_ip4)
        interface = property(get_interface)
        ordinal = property(get_ordinal)
        num_inputs = property(get_num_inputs)
        num_outputs = property(get_num_outputs)
        num_connections_in = property(get_num_connections_in)
        num_connections_out = property(get_num_connections_out)
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

%extend _signal {
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
    void update(maybePropVal val=0, double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            msig_update(sig, 0, 1, tt);
            return;
        }
        else if ( val->length < sig->props.length ||
                 (val->length % sig->props.length) != 0) {
            printf("Signal update requires multiples of %i values.\n",
                   sig->props.length);
            return;
        }
        int count = val->length / sig->props.length;
        if (coerce_prop(val, sig->props.type)) {
            printf("update: type mismatch\n");
            return;
        }
        msig_update(sig, val->value, count, tt);
    }
    void reserve_instances(int num=1) {
        msig_reserve_instances((mapper_signal)$self, num, 0, 0);
    }
    void reserve_instances(int num_int, int *argv) {
        msig_reserve_instances((mapper_signal)$self, num_int, argv, 0);
    }
    void update_instance(int id, maybePropVal val=0, double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            msig_update(sig, 0, 1, tt);
            return;
        }
        else if (val->length < sig->props.length ||
                 (val->length % sig->props.length) != 0) {
            printf("Signal update requires multiples of %i values.\n",
                   sig->props.length);
            return;
        }
        int count = val->length / sig->props.length;
        if (coerce_prop(val, sig->props.type)) {
            printf("update: type mismatch\n");
            return;
        }
        msig_update_instance(sig, id, val->value, count, tt);
    }
    void release_instance(int id, double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        msig_release_instance((mapper_signal)$self, id, tt);
    }
    void remove_instance(int id) {
        msig_remove_instance((mapper_signal)$self, id);
    }
    int active_instance_id(int index) {
        return msig_active_instance_id((mapper_signal)$self, index);
    }
    int num_active_instances() {
        return msig_num_active_instances((mapper_signal)$self);
    }
    int num_reserved_instances() {
        return msig_num_reserved_instances((mapper_signal)$self);
    }
    void set_allocation_mode(mapper_instance_allocation_type mode) {
        msig_set_instance_allocation_mode((mapper_signal)$self, mode);
    }
    void set_instance_event_callback(PyObject *PyFunc=0, int flags=0) {
        mapper_signal_instance_event_handler *h = 0;
        mapper_signal msig = (mapper_signal)$self;
        PyObject **callbacks = (PyObject**)msig->props.user_data;
        if (PyFunc) {
            h = msig_instance_event_handler_py;
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
        msig_set_instance_event_callback((mapper_signal)$self, h,
                                         flags, callbacks);
    }
    void set_callback(PyObject *PyFunc=0) {
        mapper_signal_update_handler *h = 0;
        mapper_signal msig = (mapper_signal)$self;
        PyObject **callbacks = (PyObject**)msig->props.user_data;
        if (PyFunc) {
            h = msig_handler_py;
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
        msig_set_callback((mapper_signal)$self, h, callbacks);
    }
    int query_remotes(double timetag=0) {
        mapper_timetag_t tt = MAPPER_NOW;
        if (timetag)
            mapper_timetag_set_double(&tt, timetag);
        return msig_query_remotes((mapper_signal)$self, tt);
    }
    void set_minimum(maybePropVal val=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            msig_set_minimum((mapper_signal)$self, 0);
            return;
        }
        else if (sig->props.length != val->length) {
            printf("set_minimum: value length must be %i\n", sig->props.length);
            return;
        }
        else if (coerce_prop(val, sig->props.type)) {
            printf("set_minimum: value type mismatch\n");
            return;
        }
        msig_set_minimum((mapper_signal)$self, val->value);
    }
    void set_maximum(maybePropVal val=0) {
        mapper_signal sig = (mapper_signal)$self;
        if (!val) {
            msig_set_maximum((mapper_signal)$self, 0);
            return;
        }
        else if (sig->props.length != val->length) {
            printf("set_maximum: value length must be %i\n", sig->props.length);
            return;
        }
        else if (coerce_prop(val, sig->props.type)) {
            printf("set_maximum: value type mismatch\n");
            return;
        }
        msig_set_maximum((mapper_signal)$self, val->value);
    }
    maybePropVal get_minimum() {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.minimum) {
            maybePropVal prop = malloc(sizeof(maybePropVal));
            prop->type = sig->props.type;
            prop->length = sig->props.length;
            prop->value = sig->props.minimum;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    maybePropVal get_maximum() {
        mapper_signal sig = (mapper_signal)$self;
        if (sig->props.maximum) {
            maybePropVal prop = malloc(sizeof(maybePropVal));
            prop->type = sig->props.type;
            prop->length = sig->props.length;
            prop->value = sig->props.maximum;
            prop->free_value = 0;
            return prop;
        }
        return 0;
    }
    int get_length() { return ((mapper_signal)$self)->props.length; }
    char get_type() { return ((mapper_signal)$self)->props.type; }
    int get_direction() { return ((mapper_signal)$self)->props.direction; }
    const char* get_device_name() {
        return ((mapper_signal)$self)->props.device->name;
    }
    const char* get_unit() {
        return ((mapper_signal)$self)->props.unit;
    }
    mapper_db_signal get_properties() {
        return msig_properties((mapper_signal)$self);
    }
    void set_property(const char *key, maybePropVal val=0) {
        if (val)
            msig_set_property((mapper_signal)$self, key, val->type,
                              val->value, val->length);
        else
            msig_remove_property((mapper_signal)$self, key);
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
        direction = property(get_direction)
        device_name = property(get_device_name)
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

%extend _monitor {
    _monitor(admin *DISOWN=0, int autosubscribe_flags=0x00) {
        return (monitor *)mmon_new((mapper_admin) DISOWN, autosubscribe_flags);
    }
    ~_monitor() {
        mmon_free((mapper_monitor)$self);
    }
    int poll(int timeout=0) {
        _save = PyEval_SaveThread();
        int rc = mmon_poll((mapper_monitor)$self, timeout);
        PyEval_RestoreThread(_save);
        return rc;
    }
    db *get_db() {
        return (db *)mmon_get_db((mapper_monitor)$self);
    }
    void autosubscribe(int autosubscribe_flags) {
        mmon_autosubscribe((mapper_monitor)$self, autosubscribe_flags);
    }
    void subscribe(const char *name, int subscribe_flags=0, int timeout=0) {
        return mmon_subscribe((mapper_monitor)$self, name,
                              subscribe_flags, timeout);
    }
    void unsubscribe(const char *name) {
        return mmon_unsubscribe((mapper_monitor)$self, name);
    }
    void request_devices() {
        mmon_request_devices((mapper_monitor)$self);
    }
    void connect(int num_sources, const char **sources, const char *dest,
                 mapper_db_connection_t *properties=0) {
        if (!sources || !dest)
            return;
        if (properties) {
            mmon_connect_signals_by_name((mapper_monitor)$self, num_sources,
                                         sources, dest, properties);
        }
        else
            mmon_connect_signals_by_name((mapper_monitor)$self, num_sources,
                                         sources, dest, 0);
    }
    void modify_connection(int num_sources, const char **sources, const char *dest,
                           mapper_db_connection_t *properties) {
        if (!sources || !dest || !properties)
            return;
        mmon_modify_connection_by_signal_names((mapper_monitor)$self,
                                               num_sources, sources,
                                               dest, properties);
    }
    void disconnect(int num_sources, const char **sources, const char *dest) {
        if (!sources || !dest)
            return;
        mmon_disconnect_signals_by_name((mapper_monitor)$self, num_sources,
                                        sources, dest);
    }
    double now() {
        mapper_timetag_t tt;
        mmon_now((mapper_monitor)$self, &tt);
        return mapper_timetag_get_double(tt);
    }
    void flush(int timeout = ADMIN_TIMEOUT_SEC, int quiet = 1) {
        mmon_flush_db((mapper_monitor)$self, timeout, quiet);
    }
    %pythoncode {
        db = property(get_db)
    }
}

%extend _db {
    void add_device_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_db_add_device_callback((mapper_db)$self,
                                      device_db_handler_py, PyFunc);
    }
    void remove_device_callback(PyObject *PyFunc) {
        mapper_db_remove_device_callback((mapper_db)$self,
                                         device_db_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
    }
    void add_signal_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_db_add_signal_callback((mapper_db)$self,
                                      signal_db_handler_py, PyFunc);
    }
    void remove_signal_callback(PyObject *PyFunc) {
        mapper_db_remove_signal_callback((mapper_db)$self,
                                         signal_db_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
    }
    void add_connection_callback(PyObject *PyFunc) {
        Py_XINCREF(PyFunc);
        mapper_db_add_connection_callback((mapper_db)$self,
                                          connection_db_handler_py, PyFunc);
    }
    void remove_connection_callback(PyObject *PyFunc) {
        mapper_db_remove_connection_callback((mapper_db)$self,
                                             connection_db_handler_py, PyFunc);
        Py_XDECREF(PyFunc);
    }
    mapper_db_device device(const char *device_name) {
        return mapper_db_get_device_by_name((mapper_db)$self, device_name);
    }
    mapper_db_device_t **__get_devices() {
        return mapper_db_get_all_devices((mapper_db)$self);
    }
    mapper_db_device_t **__get_devices(const char *device) {
        return mapper_db_match_devices_by_name((mapper_db)$self, device);
    }
    mapper_db_device_t **device_next(long iterator) {
        return mapper_db_device_next((mapper_db_device_t**)iterator);
    }
    mapper_db_signal_t **__get_inputs() {
        return mapper_db_get_all_inputs((mapper_db)$self);
    }
    mapper_db_signal_t **__get_inputs(const char *device_name) {
        return mapper_db_get_inputs_by_device_name((mapper_db)$self,
                                                   device_name);
    }
    mapper_db_signal_t **__get_outputs() {
        return mapper_db_get_all_outputs((mapper_db)$self);
    }
    mapper_db_signal_t **__get_outputs(const char *device_name) {
        return mapper_db_get_outputs_by_device_name((mapper_db)$self,
                                                    device_name);
    }
    mapper_db_signal input(const char *device_name, const char *signal_name) {
        return mapper_db_get_input_by_device_and_signal_names((mapper_db)$self,
                                                              device_name,
                                                              signal_name);
    }
    mapper_db_signal output(const char *device_name, const char *signal_name) {
        return mapper_db_get_output_by_device_and_signal_names((mapper_db)$self,
                                                               device_name,
                                                               signal_name);
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
    mapper_db_connection_t **get_connections_by_src_signal_name(
        const char *src_signal) {
        return mapper_db_get_connections_by_src_signal_name((mapper_db)$self,
                                                            src_signal);
    }
    mapper_db_connection_t **get_connections_by_src_device_and_signal_names(
        const char *src_device, const char *src_signal) {
        return mapper_db_get_connections_by_src_device_and_signal_names(
            (mapper_db)$self, src_device, src_signal);
    }
    mapper_db_connection_t **get_connections_by_dest_signal_name(
        const char *dest_signal) {
        return mapper_db_get_connections_by_dest_signal_name((mapper_db)$self,
                                                             dest_signal);
    }
    mapper_db_connection_t **get_connections_by_dest_device_and_signal_names(
        const char *dest_device, const char *dest_signal) {
        return mapper_db_get_connections_by_dest_device_and_signal_names(
            (mapper_db)$self, dest_device, dest_signal);
    }
    mapper_db_connection_t **get_connections_by_device_and_signal_names(
        const char *src_device,  const char *src_signal,
        const char *dest_device, const char *dest_signal) {
        return mapper_db_get_connections_by_device_and_signal_names(
            (mapper_db)$self, 1, &src_device, &src_signal,
            dest_device, dest_signal);
    }
    mapper_db_connection_t **get_connections_by_device_and_signal_names(
        int num_sources, const char **src_devices,  const char **src_signals,
        const char *dest_device, const char *dest_signal) {
        return mapper_db_get_connections_by_device_and_signal_names(
            (mapper_db)$self, num_sources, src_devices, src_signals,
            dest_device, dest_signal);
    }
    mapper_db_connection connection_by_signal_full_names(
        const char *src_name, const char *dest_name) {
        return mapper_db_get_connection_by_signal_full_names(
            (mapper_db)$self, 1, &src_name, dest_name);
    }
    mapper_db_connection connection_by_signal_full_names(
        int num_sources, const char **src_names, const char *dest_name) {
        return mapper_db_get_connection_by_signal_full_names(
             (mapper_db)$self, num_sources, src_names, dest_name);
    }
    mapper_db_connection_t **get_connections_by_src_dest_device_names(
        const char *src_device_name, const char *dest_device_name) {
        return mapper_db_get_connections_by_src_dest_device_names(
            (mapper_db)$self, 1, &src_device_name, dest_device_name);
    }
    mapper_db_connection_t **get_connections_by_src_dest_device_names(
        int num_sources, const char **src_dev_names, const char *dest_dev_name) {
        return mapper_db_get_connections_by_src_dest_device_names(
            (mapper_db)$self, num_sources, src_dev_names, dest_dev_name);
    }
    mapper_db_connection connection_by_hash(int hash) {
        return mapper_db_get_connection_by_hash((mapper_db)$self, hash);
    }
    mapper_db_connection_t **connection_next(long iterator) {
        return mapper_db_connection_next((mapper_db_connection_t**)iterator);
    }
    %pythoncode {
        def make_iterator(first, next):
            def it(self, *args):
                (d, p) = first(self, *args)
                while p:
                    yield d
                    (d, p) = next(self, p)
            return it
        devices = make_iterator(__get_devices, device_next)
        inputs = make_iterator(__get_inputs, signal_next)
        outputs = make_iterator(__get_outputs, signal_next)
        match_inputs = make_iterator(__match_inputs_by_device_name, signal_next)
        match_outputs = make_iterator(__match_outputs_by_device_name, signal_next)
        connections = make_iterator(get_all_connections, connection_next)
        connections_by_device_name = make_iterator(get_connections_by_device_name,
                                                   connection_next)
        connections_by_src_signal_name = make_iterator(get_connections_by_src_signal_name,
                                                       connection_next)
        connections_by_src_device_and_signal_names = make_iterator(
            get_connections_by_src_device_and_signal_names, connection_next)
        connections_by_dest_signal_name = make_iterator(get_connections_by_dest_signal_name,
                                                connection_next)
        connections_by_dest_device_and_signal_names = make_iterator(
            get_connections_by_dest_device_and_signal_names, connection_next)
        connections_by_device_and_signal_names = make_iterator(
            get_connections_by_device_and_signal_names, connection_next)
        connections_by_src_dest_device_names = make_iterator(
            get_connections_by_src_dest_device_names, connection_next)
    }
}

%extend _admin {
    _admin(const char *iface=0, const char *ip=0, int port=7570) {
        return (admin *)mapper_admin_new(iface, ip, port);
    }
    ~_admin() {
        mapper_admin_free((mapper_admin)$self);
    }
    const char *libversion() {
        return mapper_admin_libversion((mapper_admin)$self);
    }
}
