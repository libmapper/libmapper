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

mpr.mpr_obj_set_prop.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, c_void_p, c_int]
mpr.mpr_obj_set_prop.restype = c_int

mpr.mpr_list_filter.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, c_void_p, c_int]
mpr.mpr_list_filter.restype = c_void_p

mpr.mpr_dev_get_sigs.argtypes = [c_void_p, c_int]
mpr.mpr_dev_get_sigs.restype = c_void_p

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
    STEALING         = 0x2300
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

class Time:
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
        if isinstance(val, float):
            mpr.mpr_time_set_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_set_dbl.restype = None
            mpr.mpr_time_set_dbl(byref(self.value), val)
        else:
            mpr.mpr_time_set.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_set.restype = None
            if isinstance(val, Time):
                mpr.mpr_time_set(byref(self.value), val.value)
            else:
                mpr.mpr_time_set(byref(self.value), c_longlong(val))
        return self

    def now(self):
        # 1 << 32 == MPR_NOW
        return self.set(1 << 32)

    def get_double(self):
        mpr.mpr_time_as_dbl.argtypes = [c_longlong]
        mpr.mpr_time_as_dbl.restype = c_double
        return mpr.mpr_time_as_dbl(self.value)

    def __add__(self, addend):
        result = Time(self)
        if isinstance(addend, Time):
            mpr.mpr_time_add.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_add.restype = None
            mpr.mpr_time_add(byref(result.value), addend.value)
        elif isinstance(addend, float):
            mpr.mpr_time_add_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_add_dbl.restype = None
            mpr.mpr_time_add_dbl(byref(result.value), addend)
        else:
            print("Time.add() : incompatible type:", type(addend))
        return result

    def __iadd__(self, addend):
        if isinstance(addend, int):
            mpr.mpr_time_add.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_add.restype = None
            mpr.mpr_time_add(byref(self.value), addend)
        elif isinstance(addend, float):
            mpr.mpr_time_add_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_add_dbl.restype = None
            mpr.mpr_time_add_dbl(byref(self.value), addend)
        else:
            print("Time.iadd() : incompatible type:", type(addend))
        return self

    def __radd__(self, val):
        return val + self.get_double()

    def __sub__(self, subtrahend):
        result = Time(self)
        if isinstance(subtrahend, Time):
            mpr.mpr_time_sub.argtypes = [c_void_p, c_longlong]
            mpr.mpr_time_sub.restype = None
            mpr.mpr_time_sub(byref(result.value), subtrahend.value)
        else:
            mpr.mpr_time_add_dbl.argtypes = [c_void_p, c_double]
            mpr.mpr_time_add_dbl.restype = None
            mpr.mpr_time_add_dbl(byref(result.value), -subtrahend)
        return result

    def __isub__(self, subtrahend):
        if isinstance(subtrahend, Time):
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
        result = Time(self)
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
        result = Time(self)
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

    def __lt__(self, rhs):
        if isinstance(rhs, Time):
            return self.get_double() < rhs.get_double()
        else:
            return self.get_double() < rhs

    def __le__(self, rhs):
        if isinstance(rhs, Time):
            return self.get_double() <= rhs.get_double()
        else:
            return self.get_double() <= rhs

    def __eq__(self, rhs):
        if isinstance(rhs, Time):
            return self.value.value == rhs.value.value
        else:
            return self.get_double() == rhs

    def __ge__(self, rhs):
        if isinstance(rhs, Time):
            return self.get_double() >= rhs.get_double()
        else:
            return self.get_double() >= rhs

    def __gt__(self, rhs):
        if isinstance(rhs, Time):
            return self.get_double() > rhs.get_double()
        else:
            return self.get_double() > rhs

class MprObject:
    pass

class MprObjectList:
    def __init__(self, ref):
        mpr.mpr_list_get_cpy.argtypes = [c_void_p]
        mpr.mpr_list_get_cpy.restype = c_void_p
        self._list = mpr.mpr_list_get_cpy(ref)

    def __del__(self):
        if self._list:
            mpr.mpr_list_free.argtypes = [c_void_p]
            mpr.mpr_list_free.restype = None
            mpr.mpr_list_free(self._list)
            self._list = None

    def __iter__(self):
        return self

    @staticmethod
    def _objectify(ptr):
        # get type of libmapper object
        mpr.mpr_obj_get_type.argtypes = [c_void_p]
        mpr.mpr_obj_get_type.restype = c_byte
        _type = mpr.mpr_obj_get_type(ptr)
        if _type == Type.DEVICE:
            return Device(None, None, ptr)
        elif _type == Type.SIGNAL:
            return Signal(ptr)
        elif _type == Type.MAP:
            return Map(None, None, ptr)
        else:
            print("list error: object is not a Device, Signal, or Map")
            return None

    def next(self):
        if self._list:
            # self._list is the address of result, need to dereference
            result = cast(self._list, POINTER(c_void_p)).contents.value
            mpr.mpr_list_get_next.argtypes = [c_void_p]
            mpr.mpr_list_get_next.restype = c_void_p
            self._list = mpr.mpr_list_get_next(self._list)
            return MprObjectList._objectify(result)
        else:
            raise StopIteration

    def filter(self, key_or_idx, val, op = Operator.EQUAL):
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
            print("list.filter() : bad index type", _type)
            return self

        _type = type(op)
        if _type is Operator:
            op = c_int(op.value)
        elif _type is int:
            op = c_int(op)
        else:
            print("list.filter() : bad operator type", _type)
            return self

        _type = type(val)
        if _type is int:
            self._list = mpr.mpr_list_filter(self._list, idx, key, 1, Type.INT32, byref(val), op)
        elif _type is float:
            self._list = mpr.mpr_list_filter(self._list, idx, key, 1, Type.FLOAT, byref(val), op)
        elif _type is str:
            self._list = mpr.mpr_list_filter(self._list, idx, key, 1, Type.STRING, c_char_p(val.encode('utf-8')), op)
        else:
            print("list.filter() : unhandled filter value type", _type)
        return self

    def join(self, rhs):
        if not isinstance(rhs, MprObjectList):
            return self
        if rhs._list is None:
            return self
        # need to use a copy of rhs list
        cpy = mpr.mpr_list_get_cpy(rhs._list)
        mpr.mpr_list_get_union.argtypes = [c_void_p, c_void_p]
        mpr.mpr_list_get_union.restype = c_void_p
        self._list = mpr.mpr_list_get_union(self._list, cpy)
        return self

    def intersect(self, rhs):
        if not isinstance(rhs, MprObjectList):
            return self
        if rhs._list is None:
            return self
        # need to use a copy of list
        cpy = mpr.mpr_list_get_cpy(rhs._list)
        mpr.mpr_list_get_isect.argtypes = [c_void_p, c_void_p]
        mpr.mpr_list_get_isect.restype = c_void_p
        self._list = mpr.mpr_list_get_isect(self._list, cpy)
        return self

    def subtract(self, rhs):
        if not isinstance(rhs, MprObjectList):
            return self
        if rhs._list is None:
            return self
        # need to use a copy of list
        cpy = mpr.mpr_list_get_cpy(rhs._list)
        mpr.mpr_list_get_diff.argtypes = [c_void_p, c_void_p]
        mpr.mpr_list_get_diff.restype = c_void_p
        self._list = mpr.mpr_list_get_diff(self._list, cpy)
        return self

    def __getitem__(self, index):
        # python lists allow a negative index
        if index < 0:
            mpr.mpr_list_get_size.argtypes = [c_void_p]
            mpr.mpr_list_get_size.restype = c_int
            index += mpr.mpr_list_get_size(self._list)
        if index > 0:
            mpr.mpr_list_get_idx.argtypes = [c_void_p, c_uint]
            mpr.mpr_list_get_idx.restype = c_void_p
            ret = mpr.mpr_list_get_idx(self._list, index)
            if ret:
                return MprObjectList._objectify(ret)
        raise IndexError
        return None

    def __next__(self):
        return self.next()

    def __len__(self):
        mpr.mpr_list_get_size.argtypes = [c_void_p]
        mpr.mpr_list_get_size.restype = c_int
        return mpr.mpr_list_get_size(self._list)

    def print(self):
        mpr.mpr_list_print.argtypes = [c_void_p]
        mpr.mpr_list_print.restype = None
        mpr.mpr_list_print(self._list)
        return self

class MprObject:
    def __init__(self, ref):
        self._obj = ref

    def get_num_properties(self):
        mpr.mpr_obj_get_num_props.argtypes = [c_void_p]
        mpr.mpr_obj_get_num_props.restype = c_int
        return mpr.mpr_obj_get_num_props(self._obj)
    num_properties = property(get_num_properties)

    def graph(self):
        mpr.mpr_obj_get_graph.argtypes = [c_void_p]
        mpr.mpr_obj_get_graph.restype = c_void_p
        return Graph(None, mpr.mpr_obj_get_graph(self._obj))

    def set_property(self, key_or_idx, val, publish=1):
        mpr.mpr_obj_set_prop.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, c_void_p, c_int]
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
                print("object.set_property() : key 0x0200 (DATA) is protected")
                return self
            _idx.value = key_or_idx
        else:
            print("object.set_property() : bad key type", _type)
            return self

        _type = type(val)
        if _type is list:
            _len = len(val)
            _type = type(val[0])
            # TODO: check if types are homogeneous
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
        elif _type is bool:
            _type = c_char(Type.BOOLEAN.value)
            if _len == 1:
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, byref(c_int(val)), publish)
            else:
                int_array = (c_int * _len)()
                int_array[:] = [ int(x) for x in val ]
                mpr.mpr_obj_set_prop(self._obj, _idx, _key, _len, _type, int_array, publish)
        else:
            print("object.set_property() : unhandled type", _type)
        return self

    def get_property(self, key_or_idx):
        _len, _type, _val, _pub = c_int(), c_char(), c_void_p(), c_int()
        if isinstance(key_or_idx, str):
            _key = key_or_idx.encode('utf-8')
            mpr.mpr_obj_get_prop_by_key.argtypes = [c_void_p, c_char_p, c_void_p, c_void_p, c_void_p, c_void_p]
            mpr.mpr_obj_get_prop_by_key.restype = c_int
            prop = mpr.mpr_obj_get_prop_by_key(self._obj, _key, byref(_len), byref(_type), byref(_val), byref(_pub))
        elif isinstance(key_or_idx, int) or isinstance(key_or_idx, Property):
            if isinstance(key_or_idx, Property):
                _idx = c_int(key_or_idx.value)
            else:
                _idx = c_int(key_or_idx)
            _key = c_char_p()
            prop = mpr.mpr_obj_get_prop_by_idx(self._obj, _idx, byref(_key), byref(_len),
                                               byref(_type), byref(_val), byref(_pub))
        else:
            return None
        if prop == 0 or prop == 0x0200:
            return None
        prop = Property(prop)

        _type = _type.value
        _len = _len.value
        val = None
        if _val.value == None:
            val = None
        elif _type == b's':
            if _len == 1:
                val = string_at(cast(_val, c_char_p)).decode('utf-8')
            else:
                _val = cast(_val, POINTER(c_char_p))
                val = [_val[i].decode('utf-8') for i in range(_len)]
        elif _type == b'b':
            _val = cast(_val, POINTER(c_int))
            if _len == 1:
                val = _val[0] != 0
            else:
                val = [(_val[i] != 0) for i in range(_len)]
        elif _type == b'i':
            _val = cast(_val, POINTER(c_int))
            if _len == 1:
                val = _val[0]

                # translate some values into Enums
                if prop == Property.DIRECTION:
                    val = Direction(val)
                elif prop == Property.PROCESS_LOCATION:
                    val = Location(val)
                elif prop == Property.PROTOCOL:
                    val = Protocol(val)
                elif prop == Property.STATUS:
                    val = Status(val)
                elif prop == Property.STEALING:
                    val = Stealing(val)
            else:
                val = [_val[i] for i in range(_len)]
        elif _type == b'h':
            _val = cast(_val, POINTER(c_longlong))
            if _len == 1:
                val = _val[0]
            else:
                val = [_val[i] for i in range(_len)]
        elif _type == b'f':
            _val = cast(_val, POINTER(c_float))
            if _len == 1:
                val = _val[0]
            else:
                val = [_val[i] for i in range(_len)]
        elif _type == b'd':
            _val = cast(_val, POINTER(c_double))
            if _len == 1:
                val = _val[0]
            else:
                val = [_val[i] for i in range(_len)]
        elif _type == b'\x01': # device
            if _len != 1:
                print("object.get_property() : can't handle device array type")
                return None
            elif _val.value == None:
                val = None
            else:
                val = Device(None, None, _val.value)
        elif _type == b'@': # list
            if _len != 1:
                print("object.get_property() : can't handle list array type")
                return None
            elif _val.value == None:
                val = None
            else:
                val = MprObjectList(_val.value);
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
                    print("object.get_property() : unhandled char type", val)
                    val = Type.UNKNOWN
            else:
                val = [_val[i] for i in range(_len)]
        elif _type == b't':
            _val = cast(_val, POINTER(c_longlong))
            if _len == 1:
                val = Time(_val[0])
            else:
                val = [Time(_val[i]) for i in range(_len)]
        else:
            print("object.get_property() : can't handle prop type", _type)
            return None

        # TODO: if key_or_idx is str we can reuse it instead of decoding
        if isinstance(key_or_idx, str):
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
        return self.get_property(key)

    def __setitem__(self, key, val):
        self.set_property(key, val)
        return self

    def remove_property(self, key_or_idx):
        mpr.mpr_obj_remove_prop.argtypes = [c_void_p, c_int, c_char_p]
        mpr.mpr_obj_remove_prop.restype = c_int
        if isinstance(key_or_idx, str):
            mpr.mpr_obj_remove_prop(self._obj, Property.UNKNOWN.value, key_or_idx.encode('utf-8'))
        elif isinstance(key_or_idx, int):
            mpr.mpr_obj_remove_prop(self._obj, key_or_idx, None)
        elif isinstance(key_or_idx, Property):
            mpr.mpr_obj_remove_prop(self._obj, key_or_idx.value, None)
        else:
            print("object.remove_property() : bad key or index type")
        return self

    def __nonzero__(self):
        return False if self.this is None else True

    def __eq__(self, rhs):
        return rhs != None and self['id'] == rhs['id']

    def type(self):
        mpr.mpr_obj_get_type.argtypes = [c_void_p]
        mpr.mpr_obj_get_type.restype = c_byte
        _type = int(mpr.mpr_obj_get_type(self._obj))
        return Type(_type)

    def print(self, staged = 0):
        mpr.mpr_obj_print.argtypes = [c_void_p, c_int]
        mpr.mpr_obj_print(self._obj, staged)
        return self

    def push(self):
        mpr.mpr_obj_push.argtypes = [c_void_p]
        mpr.mpr_obj_push.restype = None
        if self._obj:
            mpr.mpr_obj_push(self._obj)
        return self

class InstanceId(c_longlong):
    def from_param(p):
        return p
    def __str__(self):
        print(self.value);
        return str(self.value)

c_sig_cb_type = CFUNCTYPE(None, c_void_p, c_int, c_longlong, c_int, c_char, c_void_p, c_longlong)

mpr.mpr_obj_get_prop_as_ptr.argtypes = [c_void_p, c_int, c_char_p]
mpr.mpr_obj_get_prop_as_ptr.restype = c_void_p

@CFUNCTYPE(None, c_void_p, c_int, c_longlong, c_int, c_char, c_void_p, c_longlong)
def signal_cb_py(_sig, _evt, _inst, _len, _type, _val, _time):
    data = mpr.mpr_obj_get_prop_as_ptr(_sig, 0x0200, None) # MPR_PROP_DATA
    cb = cast(data, py_sig_cb_type)

    if cb == None:
        print("error: couldn't retrieve signal callback")
        return

    if _val == None:
        val = None
    elif _type == b'i':
        _val = cast(_val, POINTER(c_int))
        if _len == 1:
            val = _val[0]
        else:
            val = [_val[i] for i in range(_len)]
    elif _type == b'f':
        _val = cast(_val, POINTER(c_float))
        if _len == 1:
            val = _val[0]
        else:
            val = [_val[i] for i in range(_len)]
    elif _type == b'd':
        _val = cast(_val, POINTER(c_double))
        if _len == 1:
            val = _val[0]
        else:
            val = [_val[i] for i in range(_len)]
    else:
        print("sig_cb_py : unknown signal type", _type)
        return

    # TODO: check if cb was registered with signal or instances
    cb(Signal(_sig), Signal.Event(_evt), _inst, val, Time(_time))


class Signal(MprObject):
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
        self.id = 0
        self.callback = None

#    def __repr__(self):
#        return 'Signal: {}'.format(self[Property.NAME])

    def set_callback(self, callback, events=Event.ALL):

        if callback:
            _c_inc_ref(callback)

        callback = py_sig_cb_type(callback)

        mpr.mpr_obj_set_prop.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, py_sig_cb_type, c_int]
        mpr.mpr_obj_set_prop(self._obj, 0x0200, None, 1, Type.POINTER.value, callback, 0)

        mpr.mpr_sig_set_cb.argtypes = [c_void_p, c_sig_cb_type, c_int]
        mpr.mpr_sig_set_cb.restype = None
        mpr.mpr_sig_set_cb(self._obj, signal_cb_py, events.value)
        return self

    def set_value(self, value):
        mpr.mpr_sig_set_value.argtypes = [c_void_p, c_longlong, c_int, c_char, c_void_p]
        mpr.mpr_sig_set_value.restype = None

        if value == None:
            mpr.mpr_sig_set_value(self._obj, self.id, 0, MPR_INT32, None)
        elif isinstance(value, list):
            _type = type(value[0])
            _len = len(value)
            if _type is int:
                int_array = (c_int * _len)()
                int_array[:] = [ int(x) for x in value ]
                mpr.mpr_sig_set_value(self._obj, self.id, _len, Type.INT32.value, int_array)
            elif _type is float:
                float_array = (c_float * _len)()
                float_array[:] = [ float(x) for x in value ]
                mpr.mpr_sig_set_value(self._obj, self.id, _len, Type.FLOAT.value, float_array)
        else:
            _type = type(value)
            if _type is int:
                mpr.mpr_sig_set_value(self._obj, self.id, 1, Type.INT32.value, byref(c_int(int(value))))
            elif _type is float:
                mpr.mpr_sig_set_value(self._obj, self.id, 1, Type.FLOAT.value, byref(c_float(float(value))))
        return self

    def get_value(self):
        mpr.mpr_sig_get_value.argtypes = [c_void_p, c_longlong, c_void_p]
        mpr.mpr_sig_get_value.restype = c_void_p
        _time = Time()
        _val = mpr.mpr_sig_get_value(self._obj, self.id, byref(_time.value))
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

    def reserve_instances(self, arg):
        mpr.mpr_sig_reserve_inst.argtypes = [c_void_p, c_int, c_void_p, c_void_p]
        if isinstance(arg, int):
            count = mpr.mpr_sig_reserve_inst(self._obj, arg, None, None)
        elif isinstance(arg, list):
            _len = len(arg)
            array = (c_longlong * _len)()
            array[:] = [ int(x) for x in arg ]
            count = mpr.mpr_sig_reserve_inst(self._obj, _len, array, None)
        print("reserved", count, "instances")
        return self

    def instance(self, id):
        return SignalInstance(self._obj, id)

    def num_instances(self, status = Status.ALL):
        mpr.mpr_sig_get_num_inst.argtypes = [c_void_p, c_int]
        if not isinstance(status, Status):
            status = Status(status)
        return mpr.mpr_sig_get_num_inst(self._obj, status.value)

    def instance_id(self, idx, status = Status.ALL):
        mpr.mpr_sig_get_inst_id.argtypes = [c_void_p, c_int, c_int]
        mpr.mpr_sig_get_inst_id.restype = c_longlong # InstanceId
        if not isinstance(status, Status):
            status = Status(status)
        foo = mpr.mpr_sig_get_inst_id(self._obj, idx, status.value)
        return foo

    def device(self):
        mpr.mpr_sig_get_dev.argtypes = [c_void_p]
        mpr.mpr_sig_get_dev.restype = c_void_p
        device = mpr.mpr_sig_get_dev(self._obj)
        return Device(None, None, device)

    def maps(self, direction = Direction.ANY):
        mpr.mpr_sig_get_maps.argtypes = [c_void_p, c_int]
        mpr.mpr_sig_get_maps.restype = c_void_p
        return MprObjectList(mpr.mpr_sig_get_maps(self._obj, direction))

class SignalInstance(Signal):
    def __init__(self, sigptr, id):
        self._obj = sigptr
        self.id = id

    def release(self):
        mpr.mpr_sig_release_inst.argtypes = [c_void_p, InstanceId]
        mpr.mpr_sig_release_inst.restype = None
        mpr.mpr_sig_release_inst(self._obj, self.id)

py_sig_cb_type = CFUNCTYPE(None, py_object, py_object, c_longlong, py_object, py_object)

class Map(MprObject):
    def __init__(self, sources, destination, map_ptr = None):
        mpr.mpr_map_new.argtypes = [c_int, c_void_p, c_int, c_void_p]
        mpr.mpr_map_new.restype = c_void_p

        if sources == None and destination == None and map_ptr != None:
            self._obj = map_ptr
            return
        self._obj = None
        if not isinstance(destination, Signal):
            print("bad destination type")
            return
        if isinstance(sources, list):
            num_srcs = len(sources)
            array_type = c_void_p * num_srcs
            src_array = array_type()
            for i in range(num_srcs):
                if not isinstance(sources[i], Signal):
                    print("bad source type at array index", i)
                    return
                src_array[i] = sources[i]._obj
            self._obj = mpr.mpr_map_new(num_srcs, src_array, 1, byref(c_void_p(destination._obj)))
        elif isinstance(sources, Signal):
            num_srcs = 1
            self._obj = mpr.mpr_map_new(1, byref(c_void_p(sources._obj)), 1, byref(c_void_p(destination._obj)))
        else:
            print("bad source type")
            self._obj = None

    def release(self):
        mpr.mpr_map_release.argtypes = [c_void_p]
        mpr.mpr_map_release.restype = None
        mpr.mpr_map_release(self._obj)

    def signal(self, index, location):
        mpr.mpr_map_get_sig.argtypes = [c_void_p, c_int, c_int]
        mpr.mpr_map_get_sig.restype = c_void_p
        return Signal(mpr.mpr_map_get_sig(self._obj, index, location))

    def signals(self, location = Location.ANY):
        mpr.mpr_map_get_sigs.argtypes = [c_void_p, c_int]
        mpr.mpr_map_get_sigs.restype = c_void_p
        return MprObjectList(mpr.mpr_map_get_sigs(self._obj, location))

    def index(self, signal):
        if not isinstance(signal, Signal):
            print("map.index() : bad argument type", type(signal))
            return None
        mpr.mpr_map_get_sig_idx.argtypes = [c_void_p, c_void_p]
        mpr.mpr_map_get_sig_idx.restype = c_int
        idx = mpr.mpr_map_get_sig_idx(self._obj, signal._obj)
        return idx

    def get_is_ready(self):
        mpr.mpr_map_get_is_ready.argtypes = [c_void_p]
        mpr.mpr_map_get_is_ready.restype = c_int
        return 0 != mpr.mpr_map_get_is_ready(self._obj)
    ready = property(get_is_ready)

    def add_scope(self, device):
        if isinstance(device, Device):
            mpr.mpr_map_add_scope.argtypes = [c_void_p, c_void_p]
            mpr.mpr_map_add_scope.restype = None
            mpr.mpr_map_add_scope(self._obj, device._obj)
        return self

    def remove_scope(self, device):
        if isinstance(device, Device):
            mpr.mpr_map_remove_scope.argtypes = [c_void_p, c_void_p]
            mpr.mpr_map_remove_scope.restype = None
            mpr.mpr_map_remove_scope(self._obj, device._obj)
        return self

class Graph(MprObject):
    pass

graph_dev_cbs = []
graph_sig_cbs = []
graph_map_cbs = []

@CFUNCTYPE(None, c_void_p, c_void_p, c_int, c_void_p)
def graph_cb_py(_graph, c_obj, evt, user):
    mpr.mpr_obj_get_type.argtypes = [c_void_p]
    mpr.mpr_obj_get_type.restype = c_byte
    _type = mpr.mpr_obj_get_type(c_obj)
    if _type == Type.DEVICE:
        for f in graph_dev_cbs:
            f(Type.DEVICE, Device(None, None, c_obj), Graph.Event(evt))
    elif _type == Type.SIGNAL:
        for f in graph_sig_cbs:
            f(Type.SIGNAL, Signal(c_obj), Graph.Event(evt))
    elif _type == Type.MAP:
        for f in graph_map_cbs:
            f(Type.MAP, Map(None, None, c_obj), Graph.Event(evt))

class Graph(MprObject):
    class Event(Enum):
        NEW      = 0
        MODIFIED = 1
        REMOVED  = 2
        EXPIRED  = 3

    def __init__(self, subscribe_flags = Type.OBJECT, ptr = None):
        if ptr != None:
            self._obj = ptr
        else:
            mpr.mpr_graph_new.argtypes = [c_int]
            mpr.mpr_graph_new.restype = c_void_p
            self._obj = mpr.mpr_graph_new(subscribe_flags.value)

#        self._finalizer = weakref.finalize(self, mpr_graph_free, self._obj)
        mpr.mpr_graph_add_cb.argtypes = [c_void_p, c_void_p, c_int, c_void_p]
        mpr.mpr_graph_add_cb(self._obj, graph_cb_py, Type.DEVICE | Type.SIGNAL | Type.MAP, None)

    def set_interface(self, iface):
        mpr.mpr_graph_set_interface.argtypes = [c_void_p, c_char_p]
        if isinstance(iface, str):
            mpr.mpr_graph_set_interface(self._obj, iface.encode('utf-8'))
        return self
    def get_interface(self):
        mpr.mpr_graph_get_interface.argtypes = [c_void_p]
        mpr.mpr_graph_get_interface.restype = c_char_p
        iface = mpr.mpr_graph_get_interface(self._obj)
        return string_at(iface).decode('utf-8')
    interface = property(get_interface, set_interface)

    def set_address(self, address, port):
        if isinstance(address, str) and isinstance(port, int):
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
        updated = False
        if types & Type.DEVICE:
            if func not in graph_dev_cbs:
                graph_dev_cbs.append(func)
                updated = True
        if types & Type.SIGNAL:
            if func not in graph_sig_cbs:
                graph_sig_cbs.append(func)
                updated = True
        if types & Type.MAP:
            if func not in graph_map_cbs:
                graph_map_cbs.append(func)
                updated = True
        if updated:
            _c_inc_ref(func)
        return self

    def remove_callback(self, func):
        pass

    def subscribe(self, device, flags, timeout):
        mpr.mpr_graph_subscribe.argtypes = [c_void_p, c_void_p, c_int, c_int]
        mpr.mpr_graph_subscribe.restype = None
        if device == None:
            mpr.mpr_graph_subscribe(self._obj, None, flags, timeout)
        elif isinstance(device, Device):
            mpr.mpr_graph_subscribe(self._obj, device._obj, flags, timeout)
        return self

    def unsubscribe(self, device):
        mpr.mpr_graph_unsubscribe.argtypes = [c_void_p, c_void_p]
        mpr.mpr_graph_unsubscribe.restype = None
        if isinstance(device, Device):
            mpr.mpr_graph_unsubscribe(self._obj, device._obj)
        return self

    def devices(self):
        mpr.mpr_graph_get_list.argtypes = [c_void_p, c_int]
        mpr.mpr_graph_get_list.restype = c_void_p
        list = mpr.mpr_graph_get_list(self._obj, Type.DEVICE)
        return MprObjectList(list)

    def signals(self):
        mpr.mpr_graph_get_list.argtypes = [c_void_p, c_int]
        mpr.mpr_graph_get_list.restype = c_void_p
        return MprObjectList(mpr.mpr_graph_get_list(self._obj, Type.SIGNAL))

    def maps(self):
        mpr.mpr_graph_get_list.argtypes = [c_void_p, c_int]
        mpr.mpr_graph_get_list.restype = c_void_p
        return MprObjectList(mpr.mpr_graph_get_list(self._obj, Type.MAP))

    def print(self, staged = 0):
        mpr.mpr_graph_print.argtypes = [c_void_p]
        mpr.mpr_graph_print(self._obj)
        return self

class Device(MprObject):
    def __init__(self, name, graph = None, devptr = None):
        mpr.mpr_dev_new.argtypes = [c_char_p, c_void_p]
        mpr.mpr_dev_new.restype = c_void_p

        if devptr != None:
            self._obj = devptr
        else:
            if graph:
                self._obj = mpr.mpr_dev_new(name, graph._obj)
            else:
                cname = c_char_p()
                cname.value = name.encode('utf-8')
                self._obj = mpr.mpr_dev_new(cname, None)

#    def __del__(self):
#        print("Device.__del__()", self, self._obj)
#        for s in self.signals():
#            self.remove_signal(s)

#    def __repr__(self):
#        return 'Device: {}'.format(self[Property.NAME])

    def poll(self, timeout = 0):
        mpr.mpr_dev_poll.argtypes = [c_void_p, c_int]
        mpr.mpr_dev_poll.restype = c_int
        return mpr.mpr_dev_poll(self._obj, timeout)

    def add_signal(self, dir, name, length=1, datatype=Type.FLOAT, unit=None, min=None, max=None,
                   num_int=None, callback=None, events=Signal.Event.ALL):
        mpr.mpr_sig_new.argtypes = [c_void_p, c_int, c_char_p, c_int, c_char, c_char_p, c_void_p,
                                    c_void_p, POINTER(c_int), c_void_p, c_int]
        mpr.mpr_sig_new.restype = c_void_p

        ptr = mpr.mpr_sig_new(self._obj, dir.value, name.encode('utf-8'), length,
                              datatype.value, None, None, None, None, None, Signal.Event.NONE)

        signal = Signal(ptr)
        if callback != None:
            signal.set_callback(callback, events)

        # TODO: set min, max, unit, instances
        _c_inc_ref(signal)
        return signal

    def remove_signal(self, signal):
        if isinstance(signal, Signal):
            data = mpr.mpr_obj_get_prop_as_ptr(self._obj, 0x0200, None) # MPR_PROP_DATA
            _c_dec_ref(data)
            if data.callback:
                _c_dec_ref(data.callback)

            mpr.mpr_sig_free.argtypes = [c_void_p]
            mpr.mpr_sig_free.restype = None
            mpr.mpr_sig_free(signal._obj)
        return self

    def signals(self, direction = Direction.ANY):
        return MprObjectList(mpr.mpr_dev_get_sigs(self._obj, direction))

    def maps(self, direction = Direction.ANY):
        mpr.mpr_dev_get_maps.argtypes = [c_void_p, c_int]
        mpr.mpr_dev_get_maps.restype = c_void_p
        return MprObjectList(mpr.mpr_dev_get_maps(self._obj, direction))

    def get_is_ready(self):
        mpr.mpr_dev_get_is_ready.argtypes = [c_void_p]
        mpr.mpr_dev_get_is_ready.restype = c_int
        return 0 != mpr.mpr_dev_get_is_ready(self._obj)
    ready = property(get_is_ready)

    def get_time(self):
        mpr.mpr_dev_get_time.argtypes = [c_void_p]
        mpr.mpr_dev_get_time.restype = c_longlong
        return Time(mpr.mpr_dev_get_time(self._obj))

    def set_time(self, val):
        mpr.mpr_dev_set_time.argtypes = [c_void_p, c_longlong]
        mpr.mpr_dev_set_time.restype = None
        if not isinstance(val, Time):
            val = Time(val)
        mpr.mpr_dev_set_time(self._obj, val.value)
        return self

    def update_maps(self):
        mpr.mpr_dev_update_maps.argtypes = [c_void_p]
        mpr.mpr_dev_update_maps.restype = None
        mpr.mpr_dev_update_maps(self._obj)
        return self
