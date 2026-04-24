export module txn;
import std;
import cppx.reflect;

export namespace txn {

// --- Error ---

struct ConversionError {
    std::string path;
    std::string message;
};

// --- Deserialization mode ---

// Strict (default): every non-optional field in a struct must be present
// in the source value, otherwise a "missing required key" error is
// returned.
//
// Partial: missing non-optional fields are tolerated; the struct retains
// whatever default value the type's C++ default-constructor produced.
// Useful for loading overlay configs on top of hard-coded defaults.
enum class Mode { Strict, Partial };

// --- Customization-point tag ---
//
// `txn::tag<T>` is the disambiguator for the ADL-based `txn_from_value`
// customization point. A type that wants to accept a shape other than
// the default reflected one defines an overload like:
//
//   namespace mylib {
//       template<txn::ValueLike V>
//       auto txn_from_value(txn::tag<Color>, V const& v,
//                           std::string const& path)
//           -> std::optional<std::expected<Color, txn::ConversionError>>;
//   }
//
// Semantics:
//   - nullopt                       → hook declines; fall back to the
//                                     default reflected path.
//   - expected<T>{value}            → hook handled the input; use value.
//   - expected<T>{unexpected(err)}  → hook tried but failed; propagate
//                                     the error immediately.
template<typename T>
struct tag {};

// --- Type traits ---

template<typename>
inline constexpr bool always_false = false;

template<typename T>
struct is_optional : std::false_type {};
template<typename T>
struct is_optional<std::optional<T>> : std::true_type {};
template<typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

template<typename T>
struct optional_inner;
template<typename T>
struct optional_inner<std::optional<T>> { using type = T; };
template<typename T>
using optional_inner_t = typename optional_inner<T>::type;

template<typename T>
struct is_vector : std::false_type {};
template<typename T>
struct is_vector<std::vector<T>> : std::true_type {};
template<typename T>
inline constexpr bool is_vector_v = is_vector<T>::value;

template<typename T>
struct vector_inner;
template<typename T>
struct vector_inner<std::vector<T>> { using type = T; };
template<typename T>
using vector_inner_t = typename vector_inner<T>::type;

template<typename T>
struct is_map : std::false_type {};
template<typename K, typename V>
struct is_map<std::map<K, V>> : std::true_type {};
template<typename T>
inline constexpr bool is_map_v = is_map<T>::value;

template<typename T>
struct map_value_inner;
template<typename K, typename V>
struct map_value_inner<std::map<K, V>> { using type = V; };
template<typename T>
using map_value_inner_t = typename map_value_inner<T>::type;

// --- Field / Struct descriptors ---

template<typename S, typename M>
struct FieldDescriptor {
    M S::* ptr;
    std::string_view key;
};

template<typename S, typename M>
constexpr auto field(M S::* ptr, std::string_view key) -> FieldDescriptor<S, M> {
    return {ptr, key};
}

template<typename S, typename... Fs>
struct StructDescriptor {
    std::tuple<Fs...> fields;
};

template<typename S, typename... Fs>
constexpr auto describe(Fs... fields) -> StructDescriptor<S, Fs...> {
    return {std::tuple{fields...}};
}

// --- Concepts ---

template<typename T>
concept Describable = requires {
    { txn_describe(static_cast<T*>(nullptr)) };
};

// Auto-reflection applies only when the user has not provided a manual
// txn_describe() override. Describable wins to keep custom key names.
template<typename T>
concept AutoReflectable = cppx::reflect::Reflectable<T> && !Describable<T>;

template<typename V>
concept ValueLike = requires(V const& v) {
    { v.is_string() } -> std::convertible_to<bool>;
    { v.is_integer() } -> std::convertible_to<bool>;
    { v.is_float() } -> std::convertible_to<bool>;
    { v.is_bool() } -> std::convertible_to<bool>;
    { v.is_array() } -> std::convertible_to<bool>;
    { v.is_table() } -> std::convertible_to<bool>;
    { v.as_string() } -> std::convertible_to<std::string_view>;
    { v.as_integer() } -> std::convertible_to<std::int64_t>;
    { v.as_float() } -> std::convertible_to<double>;
    { v.as_bool() } -> std::convertible_to<bool>;
};

// Detects whether a custom `txn_from_value(tag<T>, V, path)` hook
// exists for T. Found via ADL, so the overload must live in a namespace
// associated with T (usually the same namespace T is declared in).
template<typename T, typename V>
concept HasCustomParser = requires(V const& v, std::string const& p) {
    { txn_from_value(tag<T>{}, v, p) }
        -> std::same_as<std::optional<std::expected<T, ConversionError>>>;
};

// --- Forward declarations ---

template<typename T, ValueLike V>
auto from_value(V const& v) -> std::expected<T, ConversionError>;

template<typename T, ValueLike V>
auto from_value(V const& v, Mode mode) -> std::expected<T, ConversionError>;

template<typename T, ValueLike V>
auto from_value(V const& v, std::string const& path)
    -> std::expected<T, ConversionError>;

template<typename T, ValueLike V>
auto from_value(V const& v, std::string const& path, Mode mode)
    -> std::expected<T, ConversionError>;

template<ValueLike V, typename T>
V to_value(T const& obj);

} // namespace txn

// --- Internal: error propagation helpers ---

// Early-return on std::expected error from inside convert_value.
// Function/monadic helpers can't issue an outer return, so this must be a macro.
// Variadic so commas in template arguments don't break the call.
#define TXN_TRY(var, ...)                                                   \
    auto _txn_##var = (__VA_ARGS__);                                        \
    if (!_txn_##var)                                                        \
        return std::unexpected(std::move(_txn_##var.error()));              \
    auto&& var = *std::move(_txn_##var)

namespace txn::detail {

// Apply each callable in order; stop at the first one that returns an error.
template<typename... Fs>
auto for_each_until_error(Fs&&... fs)
    -> std::expected<void, ConversionError>
{
    std::expected<void, ConversionError> result;
    (void)((result = fs(), result.has_value()) && ...);
    return result;
}

template<typename V>
std::string type_name_of(V const& v) {
    if (v.is_string()) return "string";
    if (v.is_integer()) return "integer";
    if (v.is_float()) return "float";
    if (v.is_bool()) return "bool";
    if (v.is_array()) return "array";
    if (v.is_table()) return "table";
    return "unknown";
}

template<typename T, ValueLike V>
auto convert_value(V const& v, std::string const& path, Mode mode)
    -> std::expected<T, ConversionError>
{
    // Customization point: if the type defines txn_from_value(tag<T>,
    // value, path), consult it first. The hook can handle alternative
    // source shapes (e.g. Color accepting "#rrggbb" in addition to
    // {r,g,b,a}) and decline (nullopt) to fall through to the default
    // reflection path for other shapes.
    if constexpr (HasCustomParser<T, V>) {
        auto hook = txn_from_value(tag<T>{}, v, path);
        if (hook.has_value()) {
            // The hook took responsibility for this value — either a
            // successful parse or an explicit error.
            return *std::move(hook);
        }
        // nullopt: hook declined; fall through to the built-in path.
    }

    if constexpr (std::is_same_v<T, std::string>) {
        if (!v.is_string())
            return std::unexpected(ConversionError{path,
                std::format("expected string, got {}", type_name_of(v))});
        return std::string{v.as_string()};

    } else if constexpr (std::is_same_v<T, bool>) {
        if (!v.is_bool())
            return std::unexpected(ConversionError{path,
                std::format("expected bool, got {}", type_name_of(v))});
        return v.as_bool();

    } else if constexpr (std::is_integral_v<T>) {
        if (!v.is_integer())
            return std::unexpected(ConversionError{path,
                std::format("expected integer, got {}", type_name_of(v))});
        auto val = v.as_integer();
        if (val < std::numeric_limits<T>::min() || val > std::numeric_limits<T>::max())
            return std::unexpected(ConversionError{path,
                std::format("value {} out of range for target type", val)});
        return static_cast<T>(val);

    } else if constexpr (std::is_floating_point_v<T>) {
        // Accept both float and integer — JSON doesn't distinguish between
        // "16" and "16.0" (both are "number"), and as_float() on ValueLike
        // implementations already handles the int→double conversion.
        if (!v.is_float() && !v.is_integer())
            return std::unexpected(ConversionError{path,
                std::format("expected float, got {}", type_name_of(v))});
        return static_cast<T>(v.as_float());

    } else if constexpr (is_optional_v<T>) {
        using Inner = optional_inner_t<T>;
        TXN_TRY(inner, convert_value<Inner, V>(v, path, mode));
        return std::optional<Inner>{std::move(inner)};

    } else if constexpr (is_vector_v<T>) {
        if (!v.is_array())
            return std::unexpected(ConversionError{path,
                std::format("expected array, got {}", type_name_of(v))});
        using Elem = vector_inner_t<T>;
        auto const& arr = v.as_array();
        std::vector<Elem> result;
        result.reserve(arr.size());
        for (std::size_t i = 0; i < arr.size(); ++i) {
            auto elem_path = std::format("{}[{}]", path, i);
            TXN_TRY(elem, convert_value<Elem, V>(arr[i], elem_path, mode));
            result.push_back(std::move(elem));
        }
        return result;

    } else if constexpr (is_map_v<T>) {
        if (!v.is_table())
            return std::unexpected(ConversionError{path,
                std::format("expected table, got {}", type_name_of(v))});
        using Val = map_value_inner_t<T>;
        T result;
        for (auto const& [k, val] : v.as_table()) {
            auto entry_path = path.empty() ? k : std::format("{}.{}", path, k);
            TXN_TRY(elem, convert_value<Val, V>(val, entry_path, mode));
            result.emplace(k, std::move(elem));
        }
        return result;

    } else if constexpr (Describable<T>) {
        if (!v.is_table())
            return std::unexpected(ConversionError{path,
                std::format("expected table, got {}", type_name_of(v))});
        auto desc = txn_describe(static_cast<T*>(nullptr));
        T result{};
        auto const& table = v.as_table();
        auto step = std::apply([&](auto const&... fds) {
            return for_each_until_error([&]() -> std::expected<void, ConversionError> {
                auto full_path = path.empty()
                    ? std::string{fds.key}
                    : std::format("{}.{}", path, fds.key);
                using M = std::decay_t<decltype(result.*(fds.ptr))>;
                auto it = table.find(std::string{fds.key});
                if constexpr (is_optional_v<M>) {
                    if (it == table.end()) {
                        result.*(fds.ptr) = std::nullopt;
                    } else {
                        using Inner = optional_inner_t<M>;
                        TXN_TRY(inner, convert_value<Inner, V>(it->second, full_path, mode));
                        result.*(fds.ptr) = std::move(inner);
                    }
                } else {
                    if (it == table.end()) {
                        if (mode == Mode::Partial) {
                            // Keep the default-initialized value on result{}.
                            return {};
                        }
                        return std::unexpected(ConversionError{full_path, "missing required key"});
                    }
                    TXN_TRY(value, convert_value<M, V>(it->second, full_path, mode));
                    result.*(fds.ptr) = std::move(value);
                }
                return {};
            }...);
        }, desc.fields);
        if (!step) return std::unexpected(std::move(step.error()));
        return result;

    } else if constexpr (AutoReflectable<T>) {
        if (!v.is_table())
            return std::unexpected(ConversionError{path,
                std::format("expected table, got {}", type_name_of(v))});
        auto const& table = v.as_table();
        T result{};
        constexpr auto N = cppx::reflect::tuple_size_v<T>;
        auto step = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            return for_each_until_error([&]() -> std::expected<void, ConversionError> {
                constexpr auto key = cppx::reflect::name_of<T, Is>();
                auto full_path = path.empty()
                    ? std::string{key}
                    : std::format("{}.{}", path, key);
                using M = std::decay_t<decltype(cppx::reflect::get<Is>(result))>;
                auto it = table.find(std::string{key});
                if constexpr (is_optional_v<M>) {
                    if (it == table.end()) {
                        cppx::reflect::get<Is>(result) = std::nullopt;
                    } else {
                        using Inner = optional_inner_t<M>;
                        TXN_TRY(inner, convert_value<Inner, V>(it->second, full_path, mode));
                        cppx::reflect::get<Is>(result) = std::move(inner);
                    }
                } else {
                    if (it == table.end()) {
                        if (mode == Mode::Partial) {
                            // Keep the default-initialized value on result{}.
                            return {};
                        }
                        return std::unexpected(ConversionError{full_path, "missing required key"});
                    }
                    TXN_TRY(value, convert_value<M, V>(it->second, full_path, mode));
                    cppx::reflect::get<Is>(result) = std::move(value);
                }
                return {};
            }...);
        }(std::make_index_sequence<N>{});
        if (!step) return std::unexpected(std::move(step.error()));
        return result;

    } else {
        static_assert(always_false<T>, "type is not deserializable");
    }
}

template<ValueLike V, typename T>
V serialize_value(T const& obj) {
    if constexpr (std::is_same_v<T, std::string>) {
        return V{obj};

    } else if constexpr (std::is_same_v<T, bool>) {
        return V{obj};

    } else if constexpr (std::is_integral_v<T>) {
        return V{static_cast<std::int64_t>(obj)};

    } else if constexpr (std::is_floating_point_v<T>) {
        return V{static_cast<double>(obj)};

    } else if constexpr (is_optional_v<T>) {
        using Inner = optional_inner_t<T>;
        if (obj.has_value())
            return serialize_value<V, Inner>(*obj);
        return V{};

    } else if constexpr (is_vector_v<T>) {
        using Arr = std::decay_t<decltype(std::declval<V>().as_array())>;
        Arr arr;
        arr.reserve(obj.size());
        using Elem = vector_inner_t<T>;
        for (auto const& elem : obj)
            arr.push_back(serialize_value<V, Elem>(elem));
        return V{std::move(arr)};

    } else if constexpr (is_map_v<T>) {
        using Tbl = std::decay_t<decltype(std::declval<V>().as_table())>;
        Tbl tbl;
        using Val = map_value_inner_t<T>;
        for (auto const& [k, val] : obj)
            tbl.emplace(k, serialize_value<V, Val>(val));
        return V{std::move(tbl)};

    } else if constexpr (Describable<T>) {
        using Tbl = std::decay_t<decltype(std::declval<V>().as_table())>;
        Tbl tbl;
        auto desc = txn_describe(static_cast<T*>(nullptr));
        std::apply([&](auto const&... fds) {
            (([&] {
                using M = std::decay_t<decltype(obj.*(fds.ptr))>;
                if constexpr (is_optional_v<M>) {
                    auto const& val = obj.*(fds.ptr);
                    if (val.has_value()) {
                        using Inner = optional_inner_t<M>;
                        tbl.emplace(std::string{fds.key},
                            serialize_value<V, Inner>(*val));
                    }
                } else {
                    tbl.emplace(std::string{fds.key},
                        serialize_value<V, M>(obj.*(fds.ptr)));
                }
            }()), ...);
        }, desc.fields);
        return V{std::move(tbl)};

    } else if constexpr (AutoReflectable<T>) {
        using Tbl = std::decay_t<decltype(std::declval<V>().as_table())>;
        Tbl tbl;
        constexpr auto N = cppx::reflect::tuple_size_v<T>;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            (([&] {
                constexpr auto key = cppx::reflect::name_of<T, Is>();
                using M = std::decay_t<decltype(cppx::reflect::get<Is>(obj))>;
                if constexpr (is_optional_v<M>) {
                    auto const& val = cppx::reflect::get<Is>(obj);
                    if (val.has_value()) {
                        using Inner = optional_inner_t<M>;
                        tbl.emplace(std::string{key},
                            serialize_value<V, Inner>(*val));
                    }
                } else {
                    tbl.emplace(std::string{key},
                        serialize_value<V, M>(cppx::reflect::get<Is>(obj)));
                }
            }()), ...);
        }(std::make_index_sequence<N>{});
        return V{std::move(tbl)};

    } else {
        static_assert(always_false<T>, "type is not serializable");
    }
}

} // namespace txn::detail

export namespace txn {

// --- Public API ---

template<typename T, ValueLike V>
auto from_value(V const& v) -> std::expected<T, ConversionError> {
    return detail::convert_value<T, V>(v, "", Mode::Strict);
}

template<typename T, ValueLike V>
auto from_value(V const& v, Mode mode) -> std::expected<T, ConversionError> {
    return detail::convert_value<T, V>(v, "", mode);
}

template<typename T, ValueLike V>
auto from_value(V const& v, std::string const& path)
    -> std::expected<T, ConversionError>
{
    return detail::convert_value<T, V>(v, path, Mode::Strict);
}

template<typename T, ValueLike V>
auto from_value(V const& v, std::string const& path, Mode mode)
    -> std::expected<T, ConversionError>
{
    return detail::convert_value<T, V>(v, path, mode);
}

template<ValueLike V, typename T>
V to_value(T const& obj) {
    return detail::serialize_value<V, T>(obj);
}

} // namespace txn
