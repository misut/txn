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

struct Config {
    std::string host;
    int port;
    std::optional<bool> debug;
};

auto cfg = txn::from_value<Config>(some_value);
auto val = txn::to_value<SomeValue>(cfg);
```

Aggregate structs are reflected automatically — field names are inferred.
For custom keys, provide a manual `txn_describe()` (see below).

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
| Aggregate structs | table (auto-reflected field names) |

## Auto-Reflection

Aggregate structs are serialized automatically using field names as keys.
Powered by the internal `refl` module (Clang-only, up to 16 fields).

Limitations: aggregate types only (no base classes, no reference members,
default-constructible members required). Nested aggregates may need manual
`txn_describe` due to brace elision.

## Custom Keys

For custom key names, provide a `txn_describe()` overload. It takes
precedence over auto-reflection:

```cpp
struct Event { std::string userId; int ts; };
inline auto txn_describe(Event*) {
    return txn::describe<Event>(
        txn::field(&Event::userId, "user_id"),
        txn::field(&Event::ts, "timestamp"));
}
```

Or use the `TXN_DESCRIBE` macro when keys match field names:

```cpp
#include "txn_describe.h"
TXN_DESCRIBE(MyStruct, field1, field2, field3)
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
