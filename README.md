# txn

C++23 serialization framework. Format-agnostic, pure, and
wasm-clean. MIT License.

## Design principles

- **Pure by default.** Every public function returns a value; no
  exceptions across the API boundary, no I/O, no global state.
  `from_value` returns `std::expected<T, ConversionError>`;
  `to_value` returns the serialized value directly.
- **Format-agnostic via concepts.** The `ValueLike` concept is
  capability injection — txn never knows about JSON or TOML
  directly, callers pass any satisfying type.
- **Monadic errors.** `std::expected<T, ConversionError>`
  everywhere. Chain with `and_then` / `transform` / `or_else`.
- **wasm-clean.** No `-fno-exceptions` carve-outs. The same API
  works identically on native and wasm32-wasi.

## Installation

### exon

```toml
[dependencies]
"github.com/misut/txn" = "0.5.0"
```

txn depends on [cppx](https://github.com/misut/cppx) for
aggregate reflection. exon resolves this automatically via
FetchContent.

### CMake (without exon)

```cmake
include(FetchContent)
FetchContent_Declare(txn
    GIT_REPOSITORY https://github.com/misut/txn.git
    GIT_TAG v0.5.0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(txn)
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

// Deserialize — returns std::expected
auto result = txn::from_value<Config>(some_value);
if (!result) {
    std::println(std::cerr, "{}: {}",
                 result.error().path, result.error().message);
    return 1;
}
auto cfg = *result;

// Serialize — returns the value directly
auto val = txn::to_value<SomeValue>(cfg);
```

Aggregate structs are reflected automatically — field names are
inferred via [cppx.reflect](https://github.com/misut/cppx). For
custom keys, provide a manual `txn_describe()` (see below).

## ValueLike Concept

txn works with any value type that satisfies the `ValueLike`
concept:

```cpp
v.is_string()   v.as_string()    // std::string_view
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

Aggregate structs are serialized automatically using field names
as keys. Powered by
[cppx.reflect](https://github.com/misut/cppx) (up to 16 fields).

Supported compilers: Clang (`__PRETTY_FUNCTION__`) and MSVC
(`__FUNCSIG__`).

Limitations: aggregate types only (no base classes, no reference
members, default-constructible members required). Nested
aggregates may need manual `txn_describe` due to brace elision.

## Custom Keys

For custom key names, provide a `txn_describe()` overload. It
takes precedence over auto-reflection:

```cpp
struct Event { std::string userId; int ts; };
inline auto txn_describe(Event*) {
    return txn::describe<Event>(
        txn::field(&Event::userId, "user_id"),
        txn::field(&Event::ts, "timestamp"));
}
```

Use this also when auto-reflection cannot apply (non-aggregate
types, nested aggregates, etc.).

## Error Handling

`from_value` returns `std::expected<T, ConversionError>`.
`ConversionError` carries a dotted path to the failed field and
a human-readable message:

```cpp
auto result = txn::from_value<Config>(value);
if (!result) {
    auto const& e = result.error();
    // e.path    → "server.port"
    // e.message → "expected integer, got string"
    std::println(std::cerr, "{}: {}", e.path, e.message);
}
```

For users who prefer throwing ergonomics, a one-liner at the
call site suffices:

```cpp
auto cfg = txn::from_value<Config>(value).value();  // throws bad_expected_access on error
```

## Works With

- [tomlcpp](https://github.com/misut/tomlcpp) — `toml::Value`
  satisfies `ValueLike` out of the box

## Repository

- Remote: `git@github.com:misut/txn.git`
- Default branch: `main`
- Commits follow [Conventional Commits](https://www.conventionalcommits.org/).
