from ctypes import *
from enum import IntFlag, Enum
import weakref, sys

# need different library extensions for Linux, Windows
cdll.LoadLibrary("libmapper.dylib")
mpr = CDLL("libmapper.dylib")

# configuration of Py_IncRef and Py_DecRef
_c_inc_ref = pythonapi.Py_IncRef
_c_inc_ref.argtypes = [py_object]
_c_dec_ref = pythonapi.Py_DecRef
_c_dec_ref.argtypes = [py_object]

mpr.mpr_obj_get_prop_by_idx.argtypes = [c_void_p, c_int, c_void_p, c_void_p, c_void_p, c_void_p, c_void_p]
mpr.mpr_obj_get_prop_by_idx.restype = c_int

mpr.mpr_obj_get_prop_as_int32.argtypes = [c_void_p, c_int, c_char_p]
mpr.mpr_obj_get_prop_as_int32.restype = c_int
mpr.mpr_obj_get_prop_as_flt.argtypes = [c_void_p, c_int, c_char_p]
mpr.mpr_obj_get_prop_as_flt.restype = c_float
mpr.mpr_obj_get_prop_as_str.argtypes = [c_void_p, c_int, c_char_p]
mpr.mpr_obj_get_prop_as_str.restype = c_char_p
mpr.mpr_obj_get_prop_as_ptr.argtypes = [c_void_p, c_int, c_char_p]
mpr.mpr_obj_get_prop_as_ptr.restype = c_void_p

# signal object should be signal type not c_void_p, or we can translate
SIG_HANDLER = CFUNCTYPE(None, c_void_p, c_int, c_longlong, c_int, c_char, c_void_p, c_void_p)

class Direction(IntFlag):
    INCOMING   = 1
    OUTGOING   = 2
    ANY        = 3
    BOTH       = 4

class Location(IntFlag):
    SOURCE      = 1
    DESTINATION = 2
    ANY         = 3

class Operator(Enum):
    DOES_NOT_EXIST          = 0x01
    EQUAL                   = 0x02
    EXISTS                  = 0x03
    GREATER_THAN            = 0x04
    GREATER_THAN_OR_EQUAL   = 0x05
    LESS_THAN               = 0x06
    LESS_THAN_OR_EQUAL      = 0x07
    NOT_EQUAL               = 0x08
    ALL                     = 0x10
    ANY                     = 0x20
    NONE                    = 0x40

class Property(Enum):
    UNKNOWN          = 0x0000
    BUNDLE           = 0x0100
    DEVICE           = 0x0300
    DIRECTION        = 0x0400
    EPHEMERAL        = 0x0500
    EXPRESSION       = 0x0600
    HOST             = 0x0700
    ID               = 0x0800
    IS_LOCAL         = 0x0A00
    JITTER           = 0x0B00
    LENGTH           = 0x0C00
    LIBVERSION       = 0x0D00
    LINKED           = 0x0E00
    MAX              = 0x0F00
    MIN              = 0x1000
    MUTED            = 0x1100
    NAME             = 0x1200
    NUM_INSTANCES    = 0x1300
    NUM_MAPS         = 0x1400
    NUM_MAPS_IN      = 0x1500
    NUM_MAPS_OUT     = 0x1600
    NUM_SIGNALS_IN   = 0x1700
    NUM_SIGNALS_OUT  = 0x1800
    ORDINAL          = 0x1900
    PERIOD           = 0x1A00
    PORT             = 0x1B00
    PROCESS_LOCATION = 0x1C00
    PROTOCOL         = 0x1D00
    RATE             = 0x1E00
    SCOPE            = 0x1F00
    SIGNAL           = 0x2000
    STATUS           = 0x2200
    STEAL_MODE       = 0x2300
    SYNCED           = 0x2400
    TYPE             = 0x2500
    UNIT             = 0x2600
    USE_INSTANCES    = 0x2700
    VERSION          = 0x2800
    EXTRA            = 0x2900

class Protocol(Enum):
    UDP = 1
    TCP = 2

class Status(Enum):
    UNDEFINED   = 0x00
    EXPIRED     = 0x01
    STAGED      = 0x02
    READY       = 0x3E
    ACTIVE      = 0X7E
    RESERVED    = 0X80
    ALL         = 0xFF

class Stealing(Enum):
    NONE    = 0
    OLDEST  = 1
    NEWEST  = 2

class Type(IntFlag):
    UNKNOWN    = 0x00
    DEVICE     = 0x01
    SIGNAL_IN  = 0x02
    SIGNAL_OUT = 0x04
    SIGNAL     = 0x06
    MAP_IN     = 0x08
    MAP_OUT    = 0x10
    MAP        = 0x18
    OBJECT     = 0x1F
    LIST       = 0x40
    GRAPH      = 0x41
    BOOLEAN    = 0x62
    TYPE       = 0x63
    DOUBLE     = 0x64
    FLOAT      = 0x66
    INT64      = 0x68
    INT32      = 0x69
    STRING     = 0x73
    TIME       = 0x74
    POINTER    = 0x76
    NULL       = 0x4E

class time:
    mpr.mpr_time_set.argtypes = [c_void_p, c_longlong]
    mpr.mpr_time_set.restype = None

    def __init__(self, ref = None):
        self.value = c_longlong()
        if ref == None:
            # 1 << 32 == MPR_NOW
            self.set(1 << 32)
        else:
            self.set(ref)

    def set(self, val):
        if type(val) is float:
            mpr.mpr_time_set_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_set_dbl.restype = None
            mpr.mpr_time_set_dbl(byref(self.value), val)
        else:
            mpr.mpr_time_set.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_set.restype = None
            _val = c_longlong()
            _val.value = val
            mpr.mpr_time_set(byref(self.value), _val)
        return self

    def now(self):
        # 1 << 32 == MPR_NOW
        return self.set(1 << 32)

    def get_double(self):
        mpr.mpr_time_as_dbl.argtypes = [c_void_p]
        mpr.mpr_time_as_dbl.restype = c_double
        return mpr.mpr_time_as_dbl(self.value)

    def __add__(self, addend):
        result = c_longlong()
        result.set(self)
        if type(addend) is time:
            mpr.mpr_time_add.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_add.restype = None
            mpr.mpr_time_add(byref(result.value), addend.value)
        elif type(addend) is float:
            mpr.mpr_time_add_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_add_dbl.restype = None
            mpr.mpr_time_add_dbl(byref(result.time), addend)
        else:
            print("time.add() incompatible type:", type(addend))
        return result

    def __iadd__(self, addend):
        if type(addend) is int:
            mpr.mpr_time_add.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_add.restype = None
            mpr.mpr_time_add(byref(self.value), addend)
        elif type(addend) is float:
            mpr.mpr_time_add_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_add_dbl.restype = None
            mpr.mpr_time_add_dbl(byref(self.value), addend)
        else:
            print("time.add() incompatible type:", type(addend))
        return self

    def __radd__(self, val):
        return val + self.get_double()

    def __sub__(self, subtrahend):
        result = c_longlong()
        result.set(self)
        if type(subtrahend) is time:
            mpr.mpr_time_sub.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_sub.restype = None
            mpr.mpr_time_sub(byref(result.value), subtrahend.time)
        else:
            mpr.mpr_time_add_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_add_dbl.restype = None
            mpr.mpr_time_add_dbl(byref(result.value), -subtrahend)
        return result

    def __isub__(self, subtrahend):
        if type(subtrahend) is time:
            mpr.mpr_time_sub.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_sub.restype = None
            mpr.mpr_time_sub(byref(self.value), subtrahend)
        else:
            mpr.mpr_time_add_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_add_dbl.restype = None
            mpr.mpr_time_add_dbl(byref(self.value), -subtrahend)
        return self

    def __rsub__(self, val):
        return val - self.get_double()

    def __mul__(self, multiplicand):
        result = c_longlong()
        result.set(self)
        mpr.mpr_time_mul.argtypes = [c_void_p, c_double]
        mpr.mpr_time_mul.restype = None
        mpr.mpr_time_mul(byref(result.value), multiplicand)
        return result

    def __imul__(self, multiplicand):
        mpr.mpr_time_mul.argtypes = [c_void_p, c_double]
        mpr.mpr_time_mul.restype = None
        mpr.mpr_time_mul(byref(self.value), multiplicand)
        return self

    def __rmul__(self, val):
        return val * self.get_double()

    def __div__(self, divisor):
        result = c_longlong()
        result.set(self)
        mpr.mpr_time_mul.argtypes = [c_void_p, c_double]
        mpr.mpr_time_mul.restype = None
        mpr.mpr_time_mul(byref(result.value), 1/divisor)
        return result

    def __idiv__(self, divisor):
        mpr.mpr_time_mul.argtypes = [c_void_p, c_double]
        mpr.mpr_time_mul.restype = None
        mpr.mpr_time_mul(byref(self.value), 1/divisor)
        return self

    def __rdiv__(self, val):
        return val / self.get_double()
    # TODO: __lt__, __le__, __eq__, __ge__, __gt__

class mprobject:
    pass

class _mprobject_list:
    def __init__(self, ref):
        mpr.mpr_list_get_cpy.argtypes = [c_void_p]
        mpr.mpr_list_get_cpy.restype = c_void_p
        self._list = c_void_p(mpr.mpr_list_get_cpy(ref))
        print("list.__init__()", ref, " -> ", self._list)

#        mpr.mpr_list_free.argtypes = [c_void_p]
#        self._finalizer = weakref.finalize(self, mpr.mpr_list_free, self._list)

    def __del__(self):
        print("list.__del__()")
        if self._list:
            print("  trying to free list", self._list)
            mpr.mpr_list_free.argtypes = [c_void_p]
            mpr.mpr_list_free.restype = None
            mpr.mpr_list_free(self._list)
            self._list = None

    def __iter__(self):
#        print("list.__iter__()")
        return self

    def next(self):
#        print("list.next()", self._list)
        if self._list:
            # self._list is the address of result, need to dereference
            result = cast(self._list, POINTER(c_void_p)).contents

            mpr.mpr_list_get_next.argtypes = [c_void_p]
            mpr.mpr_list_get_next.restype = c_void_p
            self._list = mpr.mpr_list_get_next(self._list)

            # get type of libmapper object
            mpr.mpr_obj_get_type.argtypes = [c_void_p]
            mpr.mpr_obj_get_type.restype = c_byte
            _type = mpr.mpr_obj_get_type(result)
            if _type == Type.DEVICE:
                print("list.__next__ returning device", result)
                return device(None, None, result)
            elif _type == Type.SIGNAL:
                print("list.__next__ returning signal", result)
                return signal(result)
            elif _type == Type.MAP:
                print("list.__next__ returning map", result)
                return map(None, None, result)
            else:
                print("list.__next__ returning mprobject", result)
                return mprobject(result)
        else:
            raise StopIteration

    def filter(self, key_or_idx, val, op = Operator.EQUAL):
#        print("list.filter", key_or_idx, val, op)
        mpr.mpr_list_filter.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, c_void_p, c_int]
        mpr.mpr_list_filter.restype = c_void_p
        key, idx = c_char_p(), c_int()
        _type = type(key_or_idx)
        if _type is str:
            key.value = key_or_idx.encode('utf-8')
            idx.value = Property.UNKNOWN.value
        elif _type is Property:
            idx.value = key_or_idx.value
        elif _type is int:
            idx.value = key_or_idx
        else:
            print("bad index type for list.filter():", _type)
            return self

        _type = type(op)
        if _type is Operator:
            op = c_int(op.value)
        elif _type is int:
            op = c_int(op)
        else:
            print("bad operator type for list.filter():", _type)
            return self

        _type = type(val)
        if _type is int:
            self._list = mpr.mpr_list_filter(self._list, idx, key, 1, Type.INT32, byref(val), op)
        elif _type is float:
            self._list = mpr.mpr_list_filter(self._list, idx, key, 1, Type.FLOAT, byref(val), op)
        elif _type is str:
            self._list = mpr.mpr_list_filter(self._list, idx, key, 1, Type.STRING, c_char_p(val.encode('utf-8')), op)
        else:
            print("unhandled filter value type", _type)
        return self

    def join(self, rhs):
#        print("list.join()")
        if type(rhs) is not _mprobject_list:
            return self
        if d._list is None:
            return self
        # need to use a copy of list
        cpy = mpr.mpr_list_get_cpy(rhs._list)
        mpr.mpr_list_get_union.argtypes = [c_void_p, c_void_p]
        mpr.mpr_list_get_union.restype = c_void_p
        self._list = mpr.mpr_list_get_union(self._list, cpy)
        return self

    def intersect(self, rhs):
        print("intersecting", self._list, rhs._list);
#        print("list:intersect()")
        if type(rhs) is not _mprobject_list:
            return self
        if rhs._list is None:
            return self
        # need to use a copy of list
        cpy = c_void_p(mpr.mpr_list_get_cpy(rhs._list))
        mpr.mpr_list_get_isect.argtypes = [c_void_p, c_void_p]
        mpr.mpr_list_get_isect.restype = c_void_p
        self._list = mpr.mpr_list_get_isect(self._list, cpy)
        return self

    def subtract(self, rhs):
#        print("list.subtract()")
        if type(rhs) is not _mprobject_list:
            return self
        if rhs._list is None:
            return self
        # need to use a copy of list
        cpy = mpr.mpr_list_get_cpy(rhs._list)
        mpr.mpr_list_get_diff.argtypes = [c_void_p, c_void_p]
        mpr.mpr_list_get_diff.restype = c_void_p
        self._list = mpr.mpr_list_get_diff(self._list, cpy)
        return self

#    def filter(const char *key, propval val=0, mpr_op op=MPR_OP_EQ):
#        if (key && val && $self->list) {
#            $self->list = mpr_list_filter($self->list, MPR_PROP_UNKNOWN, key,
#                                          val->len, val->type, val->val, op);
#        }
#        return $self;
#    }
#    def filter(int prop, propval val=0, mpr_op op=MPR_OP_EQ):
#        if (prop && val && $self->list) {
#            $self->list = mpr_list_filter($self->list, prop, NULL, val->len,
#                                          val->type, val->val, op);
#        }
#        return $self;
#    }

    def __getitem__(self, index):
#        print("list.__getitem__()")
        # python lists allow negative indexes
        if index < 0:
            mpr.mpr_list_get_size.argtypes = [c_void_p]
            mpr.mpr_list_get_size.restype = c_int
            index += mpr.mpr_list_get_size(self._list)
        if index > 0:
            mpr.mpr_list_get_idx.argtypes = [c_void_p, c_uint]
            mpr.mpr_list_get_idx.restype = c_void_p
            obj = mpr.mpr_list_get_idx(self._list, index)
            if obj:
                return obj
        raise IndexError
        return None

    def __next__(self):
#        print("list.__next__()")
        return self.next()

    def __len__(self):
#        print("list.__len__()")
        mpr.mpr_list_get_size.argtypes = [c_void_p]
        mpr.mpr_list_get_size.restype = c_int
        return mpr.mpr_list_get_size(self._list)

    def print(self):
        mpr.mpr_list_print.argtypes = [c_void_p]
        mpr.mpr_list_print.restype = None
        mpr.mpr_list_print(self._list)
        return self

class mprobject:
    def __init__(self, ref):
        self._obj = ref
        print("mprobject.__init__", self, self._obj)

    def get_num_properties(self):
        mpr.mpr_obj_get_num_props.argtypes = [c_void_p]
        mpr.mpr_obj_get_num_props.restype = c_int
        return mpr.mpr_obj_get_num_props(self._obj)
    num_properties = property(get_num_properties)

    def graph(self):
        mpr.mpr_obj_get_graph.argtypes = [c_void_p]
        mpr.mpr_obj_get_graph.restype = c_void_p
        return graph(None, c_void_p(mpr.mpr_obj_get_graph(self._obj)))

    def set_property(self, key_or_idx, val, publish=1):
        mpr.mpr_obj_set_prop.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, c_void_p, c_int]
        mpr.mpr_obj_set_prop.restype = c_int

        _key, _idx, _val, _pub = c_char_p(), c_int(), c_void_p(), c_int()
        _len = 1
        _type = type(key_or_idx)
        if _type is str:
            _key.value = key_or_idx.encode('utf-8')
            _idx.value = Property.UNKNOWN.value
        elif _type is Property:
            _idx.value = key_or_idx.value
        elif _type is int:
            if key_or_idx == 0x0200:
                print("key 0x0200 (DATA) is protected")
                return self
            _idx.value = key_or_idx
        else:
            print('disallowed key type', _type)
            return self

        _type = type(val)
        if _type is list:
            _len = len(val)
            _type = type(val[0])
            # need to check if types are homogeneous
        if _type is str:
            _type = c_char(Type.STRING.value)
            if _len == 1:
                _val = c_char_p(val.encode('utf-8'))
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, _val, publish)
            else:
                str_array = (c_char_p * _len)()
                str_array[:] = [x.encode('utf-8') for x in val]
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, str_array, publish)
        elif _type is int:
            _type = c_char(Type.INT32.value)
            if _len == 1:
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, byref(c_int(val)), publish)
            else:
                int_array = (c_int * _len)()
                int_array[:] = [ int(x) for x in val ]
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, int_array, publish)
        elif _type is float:
            _type = c_char(Type.FLOAT.value)
            if _len == 1:
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, byref(c_float(val)), publish)
            else:
                float_array = (c_float * _len)()
                float_array[:] = [ float(x) for x in val ]
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, float_array, publish)
        elif _type is Direction or _type is Location or _type is Protocol or _type is Stealing:
            _type = c_char(Type.INT32.value)
            if _len == 1:
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, byref(c_int(val.value)), publish)
            else:
                int_array = (c_int * _len)()
                int_array[:] = [ x.value for x in val ]
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, int_array, publish)

        else:
            print('obj.set_property: unhandled type', _type)
        return self

    def get_property(self, key_or_idx):
        _len, _type, _val, _pub = c_int(), c_char(), c_void_p(), c_int()
        if type(key_or_idx) is str:
            _key = key_or_idx.encode('utf-8')
            mpr.mpr_obj_get_prop_by_key.argtypes = [c_void_p, c_char_p, c_void_p, c_void_p, c_void_p, c_void_p]
            mpr.mpr_obj_get_prop_by_key.restype = c_int
            prop = mpr.mpr_obj_get_prop_by_key(self._obj, _key, byref(_len), byref(_type), byref(_val), byref(_pub))
        elif type(key_or_idx) is int or type(key_or_idx) is Property:
            if type(key_or_idx) is Property:
                _idx = c_int(key_or_idx.value)
            else:
                _idx = c_int(key_or_idx)
            _key = c_char_p()
            prop = mpr.mpr_obj_get_prop_by_idx(self._obj, _idx, byref(_key), byref(_len), byref(_type), byref(_val), byref(_pub))
        else:
            return None
        if prop == 0 or prop == 0x0200:
            return None
        prop = Property(prop)

        _type = _type.value
        _len = _len.value
        print("  ", key_or_idx, "--> key", string_at(_key).decode("utf-8"), "type", _type, "len", _len, "val", _val)
        val = None
        if _val.value == None:
            val = _val
        elif _type == Type.STRING or _type == b's':
            if _len == 1:
#                print("processing string property value", _val)
                val = string_at(cast(_val, c_char_p)).decode('utf-8')
            else:
                val = []
                for i in range(_len):
                    _val = cast(_val, POINTER(c_char_p))
                    val.append(_val[i])
        elif _type == Type.BOOLEAN or _type == b'b':
            _val = cast(_val, POINTER(c_int))
            if _len == 1:
                val = _val[0] != 0
            else:
                val = []
                for i in range(_len):
                    val[i].append(_val[i] != 0)
        elif _type == Type.INT32 or _type == b'i':
            _val = cast(_val, POINTER(c_int))
            if _len == 1:
                val = _val[0]
            else:
                val = []
                for i in range(_len):
                    val.append(_val[i])
            # translate some values into Enums
            if _len == 1:
                if prop == Property.DIRECTION:
                    val = Direction(val)
                elif prop == Property.PROCESS_LOCATION:
                    val = Location(val)
                elif prop == Property.PROTOCOL:
                    val = Protocol(val)
                elif prop == Property.STATUS:
                    val = Status(val)
                elif prop == Property.STEAL_MODE:
                    val = Stealing(val)
        elif _type == Type.INT64 or _type == b'h':
            _val = cast(_val, POINTER(c_longlong))
            if _len == 1:
                val = _val[0]
            else:
                val = []
                for i in range(_len):
                    val.append(_val[i])
        elif _type == Type.FLOAT or _type == b'f':
            _val = cast(_val, POINTER(c_float))
            if _len == 1:
                val = _val[0]
            else:
                val = []
                for i in range(_len):
                    val.append(_val[i])
        elif _type == Type.DOUBLE or _type == b'd':
            _val = cast(_val, POINTER(c_double))
            if _len == 1:
                val = _val[0]
            else:
                val = []
                for i in range(_len):
                    val.append(_val[i])
        elif _type == b'\x01': # device
            if _len != 1:
                print("can't handle device array type")
                return None
            elif _val.value == None:
                val = None
            else:
                val = device(None, None, _val)
        elif _type == b'@': # list
            if _len != 1:
                print("can't handle list array type")
                return None
            elif _val.value == None:
                val = None
            else:
                val = _mprobject_list(_val);
        elif _type == b'c':
            _val = cast(_val, POINTER(c_char))
            if _len == 1:
                val = _val[0]
                if val == b'f':
                    val = Type.FLOAT
                elif val == b'i':
                    val = Type.INT32
                elif val == b'd':
                    val = Type.DOUBLE
                else:
                    print("unhandled char type", val)
                    val = Type.UNKNOWN
            else:
#                val = []
#                for i in range(_len):
#                    val.append(_val[i])
                val = [_val[i] for i in range(_len)]
        else:
            print("can't handle prop type", _type)
            return None

        # TODO: if key_or_idx is str we can reuse it instead of decoding
        if type(key_or_idx) is str:
            return val
        else:
            return {string_at(_key).decode("utf-8") : val}

    def get_properties(self):
        props = {}
        for i in range(self.num_properties):
            prop = self.get_property(i)
            if prop:
                props.update(prop)
        return props

    def __propgetter(self):
        obj = self
        props = self.get_properties()
        class propsetter(dict):
            __getitem__ = props.__getitem__
            def __setitem__(self, key, val):
                props[key] = val
                obj.set_property(key, val)
        return propsetter(self.get_properties())
    properties = property(__propgetter)

    def set_properties(self, props):
        [self.set_property(k, props[k]) for k in props]
        return self

    def __getitem__(self, key):
#        print("__getitem__", key)
        return self.get_property(key)

    def __setitem__(self, key, val):
        print("__setitem__", key, val)
        self.set_property(key, val)
        return self

    def remove_property(self, key_or_idx):
        mpr.mpr_obj_remove_prop.argtypes = [c_void_p, c_int, c_char_p]
        mpr.mpr_obj_remove_prop.restype = c_int
        if type(key_or_idx) is str:
            mpr.mpr_obj_remove_prop(self._obj, Property.UNKNOWN.value, key_or_idx.encode('utf-8'))
        elif type(key_or_idx) is int:
            mpr.mpr_obj_remove_prop(self._obj, key_or_idx, None)
        elif type(key_or_idx) is Property:
            mpr.mpr_obj_remove_prop(self._obj, key_or_idx.value, None)
        else:
            print("bad key or index type")
        return self

    def __nonzero__(self):
        return False if self.this is None else True

    def __eq__(self, rhs):
        return rhs != None and self['id'] == rhs['id']

    def type(self):
        mpr.mpr_obj_get_type.argtypes = [c_void_p]
        mpr.mpr_obj_get_type.restype = c_byte
        _type = int(mpr.mpr_obj_get_type(self._obj))
        print("type got:", _type)
        return Type(_type)

    def print(self, staged = 0):
        mpr.mpr_obj_print.argtypes = [c_void_p, c_int]
        mpr.mpr_obj_print(self._obj, staged)
        return self

    def push(self):
        mpr.mpr_obj_push.argtypes = [c_void_p]
        mpr.mpr_obj_push.restype = None
        mpr.mpr_obj_push(self._obj)
        return self

signal_cb_py_proto = CFUNCTYPE(None, c_void_p, c_int, c_longlong, c_int, c_char, c_void_p, c_longlong)

@CFUNCTYPE(None, c_void_p, c_int, c_longlong, c_int, c_char, c_void_p, c_longlong)
def signal_cb_py(_sig, _evt, _inst, _len, _type, _val, _time):
    print("signal callback!")
    f = mpr.mpr_obj_get_prop_as_ptr(sig, 0x0200, None) # MPR_PROP_DATA
    if f == None:
        print("callback function pointer is None");
        return

    if type == b'i':
        _val = cast(_val, POINTER(c_int))
        if _len == 1:
            val = _val[0]
        else:
            val = [_val[i] for i in range(_len)]
    elif type == b'f':
        _val = cast(_val, POINTER(c_float))
        if _len == 1:
            val = _val[0]
        else:
            val = [_val[i] for i in range(_len)]
    elif type == b'd':
        _val = cast(_val, POINTER(c_double))
        if _len == 1:
            val = _val[0]
        else:
            val = [_val[i] for i in range(_len)]
    else:
        print('unknown signal type', type)
        return

    # TODO: check if cb was registered with signal or instances
    print("calling signal callback")
    f(signal(_sig), signal.Event(_evt), int(_inst), val, time(_time))

class signal(mprobject):
    class Event(IntFlag):
        NONE        = 0x00
        INST_NEW    = 0x01
        REL_UPSTRM  = 0x02
        REL_DNSTRM  = 0x04
        INST_OFLW   = 0x08
        UPDATE      = 0x10
        ALL         = 0x1F

    def __init__(self, sigptr = None):
        self._obj = sigptr
        print("signal.__init__()", self, self._obj, sys.getrefcount(self._obj))
        _c_inc_ref(self)
        _c_inc_ref(self._obj)

    def __del__(self):
        print("signal.__del__()", self, self._obj)

    def set_callback(self, callback, events=Event.ALL):
        print("setting signal callback!")
        mpr.mpr_obj_set_prop.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, c_void_p, c_int]
        mpr.mpr_obj_set_prop.restype = c_int
        mpr.mpr_obj_set_prop(self._obj, 0x0200, None, 1, Type.POINTER.value, signal_cb_py_proto(callback), 0)
        _c_inc_ref(callback)

        mpr.mpr_sig_set_cb.argtypes = [c_void_p, c_void_p, c_int]
        mpr.mpr_sig_set_cb.restype = None
        mpr.mpr_sig_set_cb(self._obj, byref(signal_cb_py), events.value)
        return self

    def set_value(self, value):
        mpr.mpr_sig_set_value.argtypes = [c_void_p, c_longlong, c_int, c_char, c_void_p]
        mpr.mpr_sig_set_value.restype = None

        if value == None:
            mpr.mpr_sig_set_value(self._obj, 0, 0, MPR_INT32, None)
        elif type(value) == list:
            _type = type(value[0])
            _len = len(value)
            if _type is int:
                int_array = (c_int * _len)()
                int_array[:] = [ int(x) for x in value ]
                mpr.mpr_sig_set_value(self._obj, 0, _len, Type.INT32.value, int_array)
            elif _type is float:
                float_array = (c_float * _len)()
                float_array[:] = [ float(x) for x in value ]
                mpr.mpr_sig_set_value(self._obj, 0, _len, Type.FLOAT.value, float_array)
        else:
            _type = type(value)
            if _type is int:
                mpr.mpr_sig_set_value(self._obj, 0, 1, Type.INT32.value, byref(c_int(int(value))))
            elif _type is float:
                mpr.mpr_sig_set_value(self._obj, 0, 1, Type.FLOAT.value, byref(c_float(float(value))))
        return self

    def get_value(self):
        mpr.mpr_sig_get_value.argtypes = [c_void_p, c_longlong, c_void_p]
        mpr.mpr_sig_get_value.restype = c_void_p
        _time = time()
        _val = mpr.mpr_sig_get_value(self._obj, 0, byref(_time.value))
        _type = mpr.mpr_obj_get_prop_as_int32(self._obj, Property.TYPE.value, None)
        _len = mpr.mpr_obj_get_prop_as_int32(self._obj, Property.LENGTH.value, None)

        if _type == Type.INT32.value:
            _val = cast(_val, POINTER(c_int))
        if _type == Type.FLOAT.value:
            _val = cast(_val, POINTER(c_float))
        if _type == Type.DOUBLE.value:
            _val = cast(_val, POINTER(c_double))
        if _len == 1:
            return [_val[0], _time]
        else:
            _val = [_val[i] for i in range(_len)]
            return [_val, _time]

    def device(self):
        print("signal.device()", self._obj)
        if self._obj == None or self._obj.value == None:
            return None
        mpr.mpr_sig_get_dev.argtypes = [c_void_p]
        mpr.mpr_sig_get_dev.restype = c_void_p
        dev = mpr.mpr_sig_get_dev(self._obj)
        return device(None, None, c_void_p(dev))

    def maps(self, direction = Direction.ANY):
        print("signal.maps()", self['name'], direction)
        mpr.mpr_sig_get_maps.argtypes = [c_void_p, c_int]
        mpr.mpr_sig_get_maps.restype = c_void_p
        return _mprobject_list(mpr.mpr_sig_get_maps(self._obj, direction))
#    def set_callback()


class map(mprobject):
    def __init__(self, sources, destination, map_ptr = None):
        print("map.__init__", self, sources, destination, map_ptr)
        mpr.mpr_map_new.argtypes = [c_int, c_void_p, c_int, c_void_p]
        mpr.mpr_map_new.restype = c_void_p
        print("  new map chak 1")

        if map_ptr != None:
            print("  setting from map_ptr")
            self._obj = map_ptr
            return
        self._obj = None
        if type(destination) is not signal:
            print("bad destination type")
            return
        elif destination._obj == None or destination._obj.value == None:
            print("NULL destination object")
            return
        else:
            print("using dst signal", destination._obj)
            print("using dst signal", destination['name'])
        if type(sources) is list:
            print("setting from list source and signal destination")
            num_srcs = len(sources)
            print("  num_srcs=", num_srcs)
            array_type = c_void_p * num_srcs
            src_array = array_type()
            print("allocated c_void_p array", src_array)
            for i in range(num_srcs):
                if type(sources[i]) is not signal:
                    print("bad source type at array index", i)
                print("  adding src signal", i, sources[i]['name'], sources[i]._obj);
                src_array[i] = sources[i]._obj
            self._obj = mpr.mpr_map_new(num_srcs, src_array, 1, byref(destination._obj))
            print("received new map ptr", self._obj)
        elif type(sources) is signal:
            print("setting from signal source and signal destination")
            num_srcs = 1
            self._obj = mpr.mpr_map_new(1, byref(sources._obj), 1, byref(destination._obj))
        else:
            print("bad destination type")
            self._obj = None

    def __del__(self):
        print("map.__del__", self, self._obj)

    def release(self):
        print("release!")
        mpr.mpr_map_release.argtypes = [c_void_p]
        mpr.mpr_map_release.restype = None
        mpr.mpr_map_release(self._obj)

    def signal(self, index, location):
        mpr.mpr_map_get_sig.argtypes = [c_void_p, c_int, c_int]
        mpr.mpr_map_get_sig.restype = c_void_p
        return signal(mpr.mpr_map_get_sig(self._obj, index, location))

    def signals(self, location = Location.ANY):
        mpr.mpr_map_get_sigs.argtypes = [c_void_p, c_int]
        mpr.mpr_map_get_sigs.restype = c_void_p
        return _mprobject_list(mpr.mpr_map_get_sigs(self._obj, location))

    def index(self, sig):
        print("map.index()", sig['name'])
        if type(sig) is not signal:
            print("  bad argument type", type(sig))
            return -1
        mpr.mpr_map_get_sig_idx.argtypes = [c_void_p, c_void_p]
        mpr.mpr_map_get_sig_idx.restype = c_int
        idx = mpr.mpr_map_get_sig_idx(self._obj, sig._obj)
        print("got idx", idx)
        return idx

    def get_is_ready(self):
        print("map.get_is_ready()")
        mpr.mpr_map_get_is_ready.argtypes = [c_void_p]
        mpr.mpr_map_get_is_ready.restype = c_int
        return 0 != mpr.mpr_map_get_is_ready(self._obj)
    ready = property(get_is_ready)

    def add_scope(self, dev):
        if type(dev) is device:
            mpr.mpr_map_add_scope.argtypes = [c_void_p, c_void_p]
            mpr.mpr_map_add_scope.restype = None
            mpr.mpr_map_add_scope(self._obj, dev._obj)
        return self

    def remove_scope(self, dev):
        if type(dev) is device:
            mpr.mpr_map_remove_scope.argtypes = [c_void_p, c_void_p]
            mpr.mpr_map_remove_scope.restype = None
            mpr.mpr_map_remove_scope(self._obj, dev._obj)
        return self

class graph(mprobject):
    pass

graph_dev_cbs = []
graph_sig_cbs = []
graph_map_cbs = []

@CFUNCTYPE(None, c_void_p, c_void_p, c_int, c_void_p)
def graph_cb_py(_graph, c_obj, evt, user):
    mpr.mpr_obj_get_type.argtypes = [c_void_p]
    mpr.mpr_obj_get_type.restype = c_byte
    _type = mpr.mpr_obj_get_type(c_obj)
    print("graph_cb_py()", _type)
    if _type == Type.DEVICE:
        for f in graph_dev_cbs:
            f(Type.DEVICE, device(None, None, c_void_p(c_obj)), graph.Event(evt))
    elif _type == Type.SIGNAL:
        for f in graph_sig_cbs:
            f(Type.SIGNAL, signal(c_void_p(c_obj)), graph.Event(evt))
    elif _type == Type.MAP:
        for f in graph_map_cbs:
            f(Type.MAP, map(None, None, c_void_p(c_obj)), graph.Event(evt))

class graph(mprobject):
    class Event(Enum):
        NEW      = 0
        MODIFIED = 1
        REMOVED  = 2
        EXPIRED  = 3

    def __init__(self, subscribe_flags = Type.OBJECT, ptr = None):
        print("graph.__init__", subscribe_flags, ptr)
        if ptr != None:
            self._obj = ptr
            print("  (1) set _obj to", self._obj);
        else:
            mpr.mpr_graph_new.argtypes = [c_int]
            mpr.mpr_graph_new.restype = c_void_p
            self._obj = mpr.mpr_graph_new(subscribe_flags.value)
            print("  (2) set _obj to", self._obj);

#        self._finalizer = weakref.finalize(self, mpr_graph_free, self._obj)
        mpr.mpr_graph_add_cb.argtypes = [c_void_p, c_void_p, c_int, c_void_p]
        mpr.mpr_graph_add_cb(self._obj, graph_cb_py, Type.DEVICE | Type.SIGNAL | Type.MAP, None)

    def set_interface(self, iface):
        mpr.mpr_graph_set_interface.argtypes = [c_void_p, c_char_p]
        if type(iface) is str:
            mpr.mpr_graph_set_interface(self._obj, interface)
        return self
    def get_interface(self):
        mpr.mpr_graph_get_interface.argtypes = [c_void_p]
        mpr.mpr_graph_get_interface.restype = c_char_p
        return mpr.mpr_graph_get_interface(self._obj)
    interface = property(get_interface, set_interface)

    def set_address(self, address, port):
        if type(address) is str and type(port) is int:
            mpr.mpr_graph_set_address.argtypes = [c_void_p, c_char_p, c_int]
            mpr.mpr_graph_set_address.restype = None
            mpr.mpr_graph_set_address(self._obj, address.encode('utf-8'), port)
        return self
    def get_address(self):
        mpr.mpr_graph_get_address.argtypes = [c_void_p]
        mpr.mpr_graph_get_address.restype = c_char_p
        address = mpr.mpr_graph_get_address(self._obj)
        return string_at(address).decode('utf-8')
    address = property(get_address, set_address)

    def poll(self, timeout = 0):
        mpr.mpr_graph_poll.argtypes = [c_void_p, c_int]
        mpr.mpr_graph_poll(self._obj, timeout)
        return self

    def add_callback(self, func, types=Type.OBJECT):
        print("graph.add_callback()", func, types)
        if types & Type.DEVICE:
            print("  type is DEVICE")
            if func not in graph_dev_cbs:
                print("    adding cb func")
                graph_dev_cbs.append(func)
        if types & Type.SIGNAL:
            print("  type is SIGNAL")
            if func not in graph_sig_cbs:
                print("    adding cb func")
                graph_sig_cbs.append(func)
        if types & Type.MAP:
            print("  type is MAP")
            if func not in graph_map_cbs:
                print("    adding cb func")
                graph_map_cbs.append(func)
        return self

    def remove_callback(self, func):
        pass

    def subscribe(self, dev, flags, timeout):
        mpr.mpr_graph_subscribe.argtypes = [c_void_p, c_void_p, c_int, c_int]
        mpr.mpr_graph_subscribe.restype = None
        if device == None:
            mpr.mpr_graph_subscribe(self._obj, None, flags, timeout)
        elif type(dev) is device:
            mpr.mpr_graph_subscribe(self._obj, dev._obj, flags, timeout)
        return self

    def unsubscribe(self, device):
        mpr.mpr_graph_unsubscribe.argtypes = [c_void_p, c_void_p]
        mpr.mpr_graph_unsubscribe.restype = None
        if type(dev) is device:
            mpr.mpr_graph_unsubscribe(self._obj, dev._obj)
        return self

    def devices(self):
        mpr.mpr_graph_get_list.argtypes = [c_void_p, c_int]
        mpr.mpr_graph_get_list.restype = c_void_p
        list = mpr.mpr_graph_get_list(self._obj, Type.DEVICE)
        print("graph.devices() returning list", list)
        return _mprobject_list(list)

    def signals(self):
        mpr.mpr_graph_get_list.argtypes = [c_void_p, c_int]
        mpr.mpr_graph_get_list.restype = c_void_p
        return _mprobject_list(mpr.mpr_graph_get_list(self._obj, Type.SIGNAL))

    def maps(self):
        mpr.mpr_graph_get_list.argtypes = [c_void_p, c_int]
        mpr.mpr_graph_get_list.restype = c_void_p
        return _mprobject_list(mpr.mpr_graph_get_list(self._obj, Type.MAP))

class device(mprobject):
    def __init__(self, name, graph = None, devptr = None):
        mpr.mpr_dev_new.argtypes = [c_char_p, c_void_p]
        mpr.mpr_dev_new.restype = c_void_p

        if devptr != None:
            self._obj = devptr
        else:
            if graph:
                self._obj = c_void_p(mpr.mpr_dev_new(name, graph._obj))
#            else:
#                self._obj = c_void_p(mpr.mpr_dev_new(name, None))
            else:
                cname = c_char_p()
                cname.value = name.encode('utf-8')
                self._obj = c_void_p(mpr.mpr_dev_new(cname, None))
        print("device.__init__()", self, name, graph, self._obj)

    def __del__(self):
        print("device.__del__()", self, self._obj)

    def poll(self, timeout = 0):
        mpr.mpr_dev_poll.argtypes = [c_void_p, c_int]
        mpr.mpr_dev_poll.restype = c_int
        return mpr.mpr_dev_poll(self._obj, timeout)

    def add_signal(self, dir, name, length=1, datatype=Type.FLOAT, unit=None, min=None, max=None,
                   num_int=None, callback=None, events=signal.Event.ALL):
        print("adding signal")
        mpr.mpr_sig_new.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, c_char_p, c_void_p,
                                    c_void_p, POINTER(c_int), c_void_p, c_int]
        mpr.mpr_sig_new.restype = c_void_p

        ptr = c_void_p(mpr.mpr_sig_new(self._obj, dir.value, name.encode('utf-8'), length,
                                       datatype.value, None, None, None, None, None,
                                       signal.Event.NONE))
        sig = signal(ptr)
        if callback != None:
            sig.set_callback(callback, events)
        return sig

#    def add_signal(self, name, length=1, type='f', unit=None, minimum=None, maximum=None, callback=0):
#        cb_wrap = SIGNAL_HANDLER(callback)
#        mpr.mpr_dev_add_input.argtypes = [c_void_p, c_char_p, c_int, c_char, c_char_p, c_void_p, c_void_p, c_void_p, c_void_p]
#        mpr.mpr_dev_add_input.restype = c_void_p
#        msig = c_void_p(mpr.mpr_dev_add_input(self.device, name, length, type, unit, None, None, cb_wrap, 0))
#        if not msig:
#            return None
#
#        if minimum:
#            if type == 'i':
#                mpr.msig_set_minimum(msig, byref(c_int(int(minimum))))
#            elif type == 'f':
#                mpr.msig_set_minimum(msig, byref(c_float(float(minimum))))
#            elif type == 'd':
#                mpr.msig_set_minimum(msig, byref(c_double(double(minimum))))
#        if maximum:
#            if type == 'i':
#                mpr.msig_set_maximum(msig, byref(c_int(int(maximum))))
#            elif type == 'f':
#                mpr.msig_set_maximum(msig, byref(c_float(float(maximum))))
#            elif type == 'd':
#                mpr.msig_set_maximum(msig, byref(c_double(double(maximum))))
#
#        pysig = signal(msig)
#        self.num_inputs = mpr.mpr_dev_num_inputs(self.device)
#        return pysig

#    def add_output(self, name, length=1, type='f', unit=0, minimum=None, maximum=None):
#        mpr.mpr_dev_add_output.argtypes = [c_void_p, c_char_p, c_int, c_char, c_char_p, c_void_p, c_void_p]
#        mpr.mpr_dev_add_output.restype = c_void_p
#        msig = c_void_p(mpr.mpr_dev_add_output(self.device, name, length, type, unit, None, None))
#        if not msig:
#            return None
#
#        if minimum:
#            if type == 'i':
#                mpr.msig_set_minimum(msig, byref(c_int(int(minimum))))
#            elif type == 'f':
#                mpr.msig_set_minimum(msig, byref(c_float(float(minimum))))
#            elif type == 'd':
#                mpr.msig_set_minimum(msig, byref(c_double(double(minimum))))
#        if maximum:
#            if type == 'i':
#                mpr.msig_set_maximum(msig, byref(c_int(int(maximum))))
#            elif type == 'f':
#                mpr.msig_set_maximum(msig, byref(c_float(float(maximum))))
#            elif type == 'd':
#                mpr.msig_set_maximum(msig, byref(c_double(double(maximum))))
#
#        pysig = signal(msig)
#        self.num_outputs = mpr.mpr_dev_num_outputs(self.device)
#        return pysig

    def remove_signal(self, signal):
        if type(signal) is signal:
            mpr.mpr_sig_free.argtypes = [c_void_p]
            mpr.mpr_sig_free.restype = None
            mpr.mpr_sig_free(signal._obj)
        return self

    def signals(self, direction = Direction.ANY):
        mpr.mpr_dev_get_sigs.argtypes = [c_void_p, c_int]
        mpr.mpr_dev_get_sigs.restype = c_void_p
        return _mprobject_list(mpr.mpr_dev_get_sigs(self._obj, direction))

    def maps(self, direction = Direction.ANY):
        mpr.mpr_dev_get_maps.argtypes = [c_void_p, c_int]
        mpr.mpr_dev_get_maps.restype = c_void_p
        return _mprobject_list(mpr.mpr_dev_get_maps(self._obj, direction))

    def get_is_ready(self):
        mpr.mpr_dev_get_is_ready.argtypes = [c_void_p]
        mpr.mpr_dev_get_is_ready.restype = c_int
        return 0 != mpr.mpr_dev_get_is_ready(self._obj)
    ready = property(get_is_ready)

    def get_time(self):
        mpr.mpr_dev_get_time.argtypes = [c_void_p]
        mpr.mpr_dev_get_time.restype = c_longlong
        return time(mpr.mpr_dev_get_time(self._obj))

    def set_time(self, val):
        mpr.mpr_dev_set_time.argtypes = [c_void_p, c_longlong]
        mpr.mpr_dev_set_time.restype = None
        if type(val) is not time:
            val = time(val)
        mpr.mpr_dev_set_time(self._obj, val.value)
        return self

    def update_maps(self):
        mpr.mpr_dev_update_maps.argtypes = [c_void_p]
        mpr.mpr_dev_update_maps.restype = None
        mpr.mpr_dev_update_maps(self._obj)
        return self
