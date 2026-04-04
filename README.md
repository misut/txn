# txn

A serialization framework for C++. Inspired by serde.

## Installation

### exon

```toml
[dependencies]
"github.com/misut/txn" = "0.1.0"
```

### CMake

```cmake
add_library(txn)
target_sources(txn PUBLIC FILE_SET CXX_MODULES FILES path/to/txn.cppm)
target_link_libraries(your_target PRIVATE txn)
```

## Quick Start

```cpp
import txn;
import std;

#include "txn_describe.h"

struct Config {
    std::string host;
    int port;
    std::optional<bool> debug;
};
TXN_DESCRIBE(Config, host, port, debug)
```

Use with any value type that satisfies `txn::ValueLike`:

```cpp
auto cfg = txn::from_value<Config>(some_value);
auto val = txn::to_value<SomeValue>(cfg);
```

## ValueLike Concept

txn works with any value type that provides these methods:

```cpp
v.is_string()   v.as_string()    // std::string
v.is_integer()  v.as_integer()   // std::int64_t
v.is_float()    v.as_float()     // double
v.is_bool()     v.as_bool()      // bool
v.is_array()    v.as_array()     // iterable of V
v.is_table()    v.as_table()     // map<string, V>
```

No inheritance or registration needed — just satisfy the concept.

## Supported Types

| C++ Type | Mapping |
|----------|---------|
| `std::string` | string |
| `bool`, integral types | integer (with range check) |
| `double`, `float` | float |
| `std::optional<T>` | absent key → `nullopt` |
| `std::vector<T>` | array |
| `std::map<std::string, V>` | table |
| Described structs | table (recursive) |

## TXN_DESCRIBE

Macro that generates a `txn_describe()` function for a struct:

```cpp
TXN_DESCRIBE(MyStruct, field1, field2, field3)
```

Or write it manually:

```cpp
auto txn_describe(MyStruct*) {
    return txn::describe<MyStruct>(
        txn::field(&MyStruct::field1, "field1"),
        txn::field(&MyStruct::field2, "custom_key")
    );
}
```

## Error Handling

`txn::ConversionError` includes a dotted path to the failed field:

```cpp
try {
    auto cfg = txn::from_value<Config>(value);
} catch (txn::ConversionError const& e) {
    // e.path() → "server.port"
    // e.what() → "server.port: expected integer, got string"
}
```

## Works With

- [tomlcpp](https://github.com/misut/tomlcpp) — `toml::Value` satisfies `ValueLike` out of the box

## License

MIT
