# txn

C++23 serialization framework for converting typed C++ values to and
from format-specific value objects. `txn` is format-agnostic, pure at
the API boundary, and built on `std::expected` rather than exceptions.
MIT License.

## What it is for

`txn` is the thin conversion layer between your domain types and a
value container such as TOML, JSON, or a custom in-memory tree:

- `from_value<T>(v)` deserializes into typed C++ values
- `to_value<V>(obj)` serializes back into the chosen value type
- aggregate structs can use auto-reflection
- manual descriptors override auto-reflection when field names or shape differ

The library does not own a file format. It works with any value type
that satisfies the `ValueLike` concept.

## Install

### exon

```toml
[dependencies]
"github.com/misut/txn" = "0.6.2"
```

`txn` depends on
[cppx](https://github.com/misut/cppx) for aggregate reflection. With
`exon`, that dependency is resolved automatically.

### CMake

```cmake
include(FetchContent)
FetchContent_Declare(txn
    GIT_REPOSITORY https://github.com/misut/txn.git
    GIT_TAG v0.6.2
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(txn)
target_link_libraries(your_target PRIVATE txn)
```

## Core API

| API | Purpose |
| --- | --- |
| `txn::from_value<T>(v)` | Convert a value object into `T`, returning `std::expected<T, txn::ConversionError>`. |
| `txn::to_value<V>(obj)` | Convert a C++ object into the chosen value type `V`. |
| `txn::ConversionError` | Carries `path` and `message` for conversion failures. |
| `txn::field` | Declares one mapped field in a manual descriptor. |
| `txn::describe` | Builds a struct descriptor from `txn::field(...)` entries. |
| `txn_describe(T*)` | User-defined override for custom keys or non-auto-reflectable types. |
| `txn::ValueLike` | Concept describing the required shape of a value container. |

## Quick Start

```cpp
import txn;
import std;

struct Config {
    std::string host;
    int port;
    std::optional<bool> debug;
};

auto result = txn::from_value<Config>(some_value);
if (!result) {
    std::println(std::cerr, "{}: {}",
                 result.error().path,
                 result.error().message);
    return 1;
}

auto cfg = *result;
auto serialized = txn::to_value<SomeValue>(cfg);
```

## `ValueLike` Concept

Your value type must provide the capabilities `txn` reads from:

```cpp
v.is_string()   v.as_string()    // std::string_view
v.is_integer()  v.as_integer()   // std::int64_t
v.is_float()    v.as_float()     // double
v.is_bool()     v.as_bool()      // bool
v.is_array()    v.as_array()     // iterable / indexable sequence
v.is_table()    v.as_table()     // map-like string-keyed object
```

No registration or inheritance is required.

## Supported Mappings

| C++ Type | Mapping |
| --- | --- |
| `std::string` | string |
| `bool` | bool |
| integral types | integer with range check |
| floating-point types | float, and integer inputs are accepted for float targets |
| `std::optional<T>` | missing key becomes `std::nullopt` |
| `std::vector<T>` | array |
| `std::map<std::string, V>` | table |
| aggregate structs | table via auto-reflection or `txn_describe` |

## Auto-Reflection and Manual Descriptors

If a type is reflectable through `cppx.reflect` and does not provide
`txn_describe`, `txn` uses field names automatically.

```cpp
struct Server {
    std::string host;
    int port;
    std::optional<bool> debug;
};
```

For custom keys or non-aggregate types, provide `txn_describe`:

```cpp
struct Event {
    std::string userId;
    int ts;
};

inline auto txn_describe(Event*) {
    return txn::describe<Event>(
        txn::field(&Event::userId, "user_id"),
        txn::field(&Event::ts, "timestamp"));
}
```

If both are available, `txn_describe` takes precedence over
auto-reflection.

## Error Handling

Conversion failures return `txn::ConversionError` with a precise path
and message.

```cpp
auto result = txn::from_value<Config>(value);
if (!result) {
    auto const& e = result.error();
    // e.path    -> "server.port"
    // e.message -> "expected integer, got string"
}
```

Nested containers use dotted paths and array indices, so errors stay
actionable in larger documents.

## Serialization Behavior

- absent `std::optional<T>` fields are omitted during serialization
- nested structs, vectors, and string-keyed maps are recursively converted
- manual descriptors control the external key names used for both read and write

## Works With

- [tomlcpp](https://github.com/misut/tomlcpp), whose `toml::Value`
  satisfies `ValueLike`
- custom value trees used in tests and application code, as long as they satisfy `ValueLike`

## Build

```sh
mise install
intron install
intron exec -- exon test
```

`intron install` reads this repo's `.intron.toml`, so the same default
flow provisions MSVC on Windows and LLVM on macOS/Linux.

If you want to activate the toolchain environment in the current shell
instead of using `intron exec`, run `eval "$(intron env)"` on POSIX
shells or `Invoke-Expression ((intron env) -join "`n")` on Windows
PowerShell, then invoke `exon test` manually.

## Notes and limits

- `txn` is format-agnostic; parsing text formats is outside its scope.
- Auto-reflection inherits the current `cppx.reflect` aggregate limits.
- Missing required keys produce errors; optional fields may be absent.
- The public API is pure: conversions return values or `std::expected` and do not perform I/O.

## Repository

- Remote: `git@github.com:misut/txn.git`
- Default branch: `main`
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/)
