namespace Mapper.NET;

[Flags]
public enum Type
{
    Device = 0x01, //!< Devices only.
    SignalIn = 0x02, //!< Input signals.
    SignalOut = 0x04, //!< Output signals.
    Signal = 0x06, //!< All signals.
    MapIn = 0x08, //!< Incoming maps.
    MapOut = 0x10, //!< Outgoing maps.
    Map = 0x18, //!< All maps.
    Object = 0x1F, //!< All objects: devices, signals, and maps
    List = 0x40, //!< object query.
    Graph = 0x41, //!< Graph.
    Boolean = 'b', /* 0x62 */ //!< Boolean value.
    Type = 'c', /* 0x63 */ //!< libmapper data type.
    Double = 'd', /* 0x64 */ //!< 64-bit float.
    Float = 'f', /* 0x66 */ //!< 32-bit float.
    Int64 = 'h', /* 0x68 */ //!< 64-bit integer.
    Int32 = 'i', /* 0x69 */ //!< 32-bit integer.
    String = 's', /* 0x73 */ //!< String.
    Time = 't', /* 0x74 */ //!< 64-bit NTP timestamp.
    Pointer = 'v', /* 0x76 */ //!< pointer.
    Null = 'N' /* 0x4E */ //!< NULL value.
}

public enum Operator
{
    DoesNotExist = 0x01, //!< Property does not exist.
    IsEqual = 0x02, //!< Property value == query value.
    Exists = 0x03, //!< Property exists for this entity.
    IsGreaterThan = 0x04, //!< Property value > query value.
    IsGreaterThanOrEqual = 0x05, //!< Property value >= query value
    IsLessThan = 0x06, //!< Property value < query value
    IsLessThanOrEqual = 0x07, //!< Property value <= query value
    IsNotEqual = 0x08, //!< Property value != query value
    All = 0x10, //!< Applies to all elements of value
    Any = 0x20 //!< Applies to any element of value
}

public enum Property
{
    Bundle = 0x0100,
    Data = 0x0200,
    Device = 0x0300,
    Direction = 0x0400,
    Ephemeral = 0x0500,
    Expression = 0x0600,
    Host = 0x0700,
    Id = 0x0800,
    IsLocal = 0x0900,
    Jitter = 0x0A00,
    Length = 0x0B00,
    LibVersion = 0x0C00,
    Linked = 0x0D00,
    Max = 0x0E00,
    Min = 0x0F00,
    Muted = 0x1000,
    Name = 0x1100,
    NumInstances = 0x1200,
    NumMaps = 0x1300,
    NumMapsIn = 0x1400,
    NumMapsOut = 0x1500,
    NumSigsIn = 0x1600,
    NumSigsOut = 0x1700,
    Ordinal = 0x1800,
    Period = 0x1900,
    Port = 0x1A00,
    ProcessingLocation = 0x1B00,
    Protocol = 0x1C00,
    Rate = 0x1D00,
    Scope = 0x1E00,
    Signal = 0x1F00,
    Status = 0x2100,
    Stealing = 0x2200,
    Synced = 0x2300,
    Type = 0x2400,
    Unit = 0x2500,
    UseInstances = 0x2600,
    Version = 0x2700
}