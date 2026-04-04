export module txn;
import std;

export namespace txn {

// --- Error ---

class ConversionError : public std::runtime_error {
public:
    ConversionError(std::string path, std::string const& msg)
        : std::runtime_error(
              path.empty() ? msg : std::format("{}: {}", path, msg))
        , path_{std::move(path)} {}

    std::string const& path() const { return path_; }

private:
    std::string path_;
};

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

// --- Forward declarations ---

template<typename T, ValueLike V>
T from_value(V const& v);

template<typename T, ValueLike V>
T from_value(V const& v, std::string const& path);

template<ValueLike V, typename T>
V to_value(T const& obj);

// --- from_value implementation ---

namespace detail {

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
T convert_value(V const& v, std::string const& path) {
    if constexpr (std::is_same_v<T, std::string>) {
        if (!v.is_string())
            throw ConversionError(path,
                std::format("expected string, got {}", type_name_of(v)));
        return std::string{v.as_string()};

    } else if constexpr (std::is_same_v<T, bool>) {
        if (!v.is_bool())
            throw ConversionError(path,
                std::format("expected bool, got {}", type_name_of(v)));
        return v.as_bool();

    } else if constexpr (std::is_integral_v<T>) {
        if (!v.is_integer())
            throw ConversionError(path,
                std::format("expected integer, got {}", type_name_of(v)));
        auto val = v.as_integer();
        if (val < std::numeric_limits<T>::min() || val > std::numeric_limits<T>::max())
            throw ConversionError(path,
                std::format("value {} out of range for target type", val));
        return static_cast<T>(val);

    } else if constexpr (std::is_floating_point_v<T>) {
        if (!v.is_float())
            throw ConversionError(path,
                std::format("expected float, got {}", type_name_of(v)));
        return static_cast<T>(v.as_float());

    } else if constexpr (is_optional_v<T>) {
        using Inner = optional_inner_t<T>;
        return std::optional<Inner>{convert_value<Inner, V>(v, path)};

    } else if constexpr (is_vector_v<T>) {
        if (!v.is_array())
            throw ConversionError(path,
                std::format("expected array, got {}", type_name_of(v)));
        using Elem = vector_inner_t<T>;
        auto const& arr = v.as_array();
        std::vector<Elem> result;
        result.reserve(arr.size());
        for (std::size_t i = 0; i < arr.size(); ++i) {
            auto elem_path = std::format("{}[{}]", path, i);
            result.push_back(convert_value<Elem, V>(arr[i], elem_path));
        }
        return result;

    } else if constexpr (is_map_v<T>) {
        if (!v.is_table())
            throw ConversionError(path,
                std::format("expected table, got {}", type_name_of(v)));
        using Val = map_value_inner_t<T>;
        T result;
        for (auto const& [k, val] : v.as_table()) {
            auto entry_path = path.empty() ? k : std::format("{}.{}", path, k);
            result.emplace(k, convert_value<Val, V>(val, entry_path));
        }
        return result;

    } else if constexpr (Describable<T>) {
        if (!v.is_table())
            throw ConversionError(path,
                std::format("expected table, got {}", type_name_of(v)));
        auto desc = txn_describe(static_cast<T*>(nullptr));
        T result{};
        auto const& table = v.as_table();
        std::apply([&](auto const&... fds) {
            (([&] {
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
                        result.*(fds.ptr) = convert_value<Inner, V>(it->second, full_path);
                    }
                } else {
                    if (it == table.end()) {
                        throw ConversionError(full_path, "missing required key");
                    }
                    result.*(fds.ptr) = convert_value<M, V>(it->second, full_path);
                }
            }()), ...);
        }, desc.fields);
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

    } else {
        static_assert(always_false<T>, "type is not serializable");
    }
}

} // namespace detail

// --- Public API ---

template<typename T, ValueLike V>
T from_value(V const& v) {
    return detail::convert_value<T, V>(v, "");
}

template<typename T, ValueLike V>
T from_value(V const& v, std::string const& path) {
    return detail::convert_value<T, V>(v, path);
}

template<ValueLike V, typename T>
V to_value(T const& obj) {
    return detail::serialize_value<V, T>(obj);
}

} // namespace txn
