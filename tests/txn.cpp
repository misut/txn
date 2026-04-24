import txn;
import std;

// --- MockValue: satisfies txn::ValueLike for standalone testing ---

struct MockValue;
using MockTable = std::map<std::string, MockValue>;
using MockArray = std::vector<MockValue>;

struct MockValue {
    using Data = std::variant<
        std::string, std::int64_t, double, bool,
        std::unique_ptr<MockArray>, std::unique_ptr<MockTable>
    >;
    Data data;

    MockValue() : data{std::string{}} {}
    MockValue(std::string v) : data{std::move(v)} {}
    MockValue(char const* v) : data{std::string{v}} {}
    MockValue(std::int64_t v) : data{v} {}
    MockValue(int v) : data{static_cast<std::int64_t>(v)} {}
    MockValue(double v) : data{v} {}
    MockValue(bool v) : data{v} {}
    MockValue(MockArray v) : data{std::make_unique<MockArray>(std::move(v))} {}
    MockValue(MockTable v) : data{std::make_unique<MockTable>(std::move(v))} {}

    MockValue(MockValue const& o) {
        std::visit([this](auto const& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<MockArray>>)
                data = std::make_unique<MockArray>(*v);
            else if constexpr (std::is_same_v<T, std::unique_ptr<MockTable>>)
                data = std::make_unique<MockTable>(*v);
            else
                data = v;
        }, o.data);
    }
    MockValue(MockValue&&) noexcept = default;
    MockValue& operator=(MockValue const& o) {
        if (this != &o) { MockValue tmp{o}; *this = std::move(tmp); }
        return *this;
    }
    MockValue& operator=(MockValue&&) noexcept = default;

    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_integer() const { return std::holds_alternative<std::int64_t>(data); }
    bool is_float() const { return std::holds_alternative<double>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_array() const { return std::holds_alternative<std::unique_ptr<MockArray>>(data); }
    bool is_table() const { return std::holds_alternative<std::unique_ptr<MockTable>>(data); }

    std::string const& as_string() const { return std::get<std::string>(data); }
    std::int64_t as_integer() const { return std::get<std::int64_t>(data); }
    double as_float() const { return std::get<double>(data); }
    bool as_bool() const { return std::get<bool>(data); }
    MockArray const& as_array() const { return *std::get<std::unique_ptr<MockArray>>(data); }
    MockArray& as_array() { return *std::get<std::unique_ptr<MockArray>>(data); }
    MockTable const& as_table() const { return *std::get<std::unique_ptr<MockTable>>(data); }
    MockTable& as_table() { return *std::get<std::unique_ptr<MockTable>>(data); }
};

// --- Test structs ---

struct Point {
    int x;
    int y;
};
inline auto txn_describe(Point*) {
    return txn::describe<Point>(
        txn::field(&Point::x, "x"),
        txn::field(&Point::y, "y"));
}

struct Server {
    std::string host;
    int port;
    std::optional<bool> debug;
};
inline auto txn_describe(Server*) {
    return txn::describe<Server>(
        txn::field(&Server::host, "host"),
        txn::field(&Server::port, "port"),
        txn::field(&Server::debug, "debug"));
}

struct Config {
    Server server;
    std::vector<std::string> tags;
};
inline auto txn_describe(Config*) {
    return txn::describe<Config>(
        txn::field(&Config::server, "server"),
        txn::field(&Config::tags, "tags"));
}

struct WithVecStruct {
    std::vector<Point> points;
};
inline auto txn_describe(WithVecStruct*) {
    return txn::describe<WithVecStruct>(
        txn::field(&WithVecStruct::points, "points"));
}

struct WithMap {
    std::map<std::string, int> env;
};
inline auto txn_describe(WithMap*) {
    return txn::describe<WithMap>(
        txn::field(&WithMap::env, "env"));
}

// --- Auto-reflection test structs (no txn_describe) ---

struct AutoPoint {
    int x;
    int y;
};

struct AutoServer {
    std::string host;
    int port;
    std::optional<bool> debug;
};

struct AutoWithVec {
    std::vector<int> items;
};

// Override: manual txn_describe takes precedence, enabling custom keys.
struct Aliased {
    int value;
};
inline auto txn_describe(Aliased*) {
    return txn::describe<Aliased>(
        txn::field(&Aliased::value, "v"));
}

// --- Structs exercising partial-mode defaults ---
//
// Defaults are attached via member-initializers so that Mode::Partial
// deserialization preserves them when the JSON omits a field.

struct ServerWithDefaults {
    std::string host = "localhost";
    int port = 8080;
    std::optional<bool> debug;
};
inline auto txn_describe(ServerWithDefaults*) {
    return txn::describe<ServerWithDefaults>(
        txn::field(&ServerWithDefaults::host, "host"),
        txn::field(&ServerWithDefaults::port, "port"),
        txn::field(&ServerWithDefaults::debug, "debug"));
}

struct AutoServerWithDefaults {
    std::string host = "localhost";
    int port = 8080;
    std::optional<bool> debug;
};

// --- Custom-parser ADL hook: Color accepts "#rrggbb" or {r,g,b,a} object ---

struct Color {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
};

// ADL hook: accepts hex strings like "#rrggbb". For any other shape
// (e.g. the {r,g,b,a} object form), the hook declines by returning
// std::nullopt and the default reflection path takes over.
template<txn::ValueLike V>
inline auto txn_from_value(txn::tag<Color>, V const& v, std::string const& path)
    -> std::optional<std::expected<Color, txn::ConversionError>>
{
    if (!v.is_string()) return std::nullopt;
    auto s = v.as_string();
    if (s.size() != 7 || s[0] != '#') {
        return std::expected<Color, txn::ConversionError>{std::unexpected(
            txn::ConversionError{path, "hex color must be 7 chars: '#rrggbb'"})};
    }
    auto parse = [&](std::string_view sv) -> std::expected<unsigned char, txn::ConversionError> {
        unsigned int value = 0;
        auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), value, 16);
        if (ec != std::errc{} || ptr != sv.end() || value > 255) {
            return std::unexpected(txn::ConversionError{path,
                std::format("invalid hex byte '{}'", sv)});
        }
        return static_cast<unsigned char>(value);
    };
    auto r = parse(std::string_view{s}.substr(1, 2));
    if (!r) return std::expected<Color, txn::ConversionError>{std::unexpected(std::move(r.error()))};
    auto g = parse(std::string_view{s}.substr(3, 2));
    if (!g) return std::expected<Color, txn::ConversionError>{std::unexpected(std::move(g.error()))};
    auto b = parse(std::string_view{s}.substr(5, 2));
    if (!b) return std::expected<Color, txn::ConversionError>{std::unexpected(std::move(b.error()))};
    return std::expected<Color, txn::ConversionError>{Color{*r, *g, *b, 255}};
}

// --- Test helpers ---

int failed = 0;

void check(bool cond, std::string_view msg) {
    if (!cond) {
        std::println(std::cerr, "FAIL: {}", msg);
        ++failed;
    }
}

MockValue make_table(MockTable t) { return MockValue{std::move(t)}; }

// --- Tests ---

void test_string() {
    auto v = MockValue{"hello"};
    auto r = txn::from_value<std::string>(v);
    check(r.has_value() && *r == "hello", "string from_value");
}

void test_integer() {
    auto v = MockValue{std::int64_t{42}};
    auto r = txn::from_value<int>(v);
    check(r.has_value() && *r == 42, "integer from_value");
}

void test_float() {
    auto v = MockValue{3.14};
    auto r = txn::from_value<double>(v);
    check(r.has_value() && std::abs(*r - 3.14) < 1e-9, "float from_value");
}

void test_bool() {
    auto v = MockValue{true};
    auto r = txn::from_value<bool>(v);
    check(r.has_value() && *r == true, "bool from_value");
}

void test_simple_struct() {
    auto v = make_table({{"x", MockValue{std::int64_t{1}}},
                         {"y", MockValue{std::int64_t{2}}}});
    auto r = txn::from_value<Point>(v);
    check(r.has_value() && r->x == 1 && r->y == 2, "simple struct from_value");
}

void test_nested_struct() {
    auto server = MockTable{
        {"host", MockValue{"localhost"}},
        {"port", MockValue{std::int64_t{8080}}}
    };
    auto v = make_table({
        {"server", MockValue{std::move(server)}},
        {"tags", MockValue{MockArray{MockValue{"web"}, MockValue{"prod"}}}}
    });
    auto r = txn::from_value<Config>(v);
    check(r.has_value(), "nested struct ok");
    check(r->server.host == "localhost", "nested struct host");
    check(r->server.port == 8080, "nested struct port");
    check(r->server.debug == std::nullopt, "nested struct optional absent");
    check(r->tags.size() == 2, "nested struct tags size");
    check(r->tags[0] == "web", "nested struct tags[0]");
}

void test_optional_present() {
    auto v = make_table({
        {"host", MockValue{"h"}},
        {"port", MockValue{std::int64_t{80}}},
        {"debug", MockValue{true}}
    });
    auto r = txn::from_value<Server>(v);
    check(r.has_value() && r->debug.has_value() && *r->debug == true, "optional present");
}

void test_optional_absent() {
    auto v = make_table({
        {"host", MockValue{"h"}},
        {"port", MockValue{std::int64_t{80}}}
    });
    auto r = txn::from_value<Server>(v);
    check(r.has_value() && !r->debug.has_value(), "optional absent");
}

void test_vector_primitives() {
    auto v = MockValue{MockArray{
        MockValue{std::int64_t{1}},
        MockValue{std::int64_t{2}},
        MockValue{std::int64_t{3}}
    }};
    auto r = txn::from_value<std::vector<int>>(v);
    check(r.has_value() && r->size() == 3 && (*r)[0] == 1 && (*r)[2] == 3, "vector primitives");
}

void test_vector_structs() {
    auto v = make_table({{"points", MockValue{MockArray{
        MockValue{MockTable{{"x", MockValue{std::int64_t{1}}}, {"y", MockValue{std::int64_t{2}}}}},
        MockValue{MockTable{{"x", MockValue{std::int64_t{3}}}, {"y", MockValue{std::int64_t{4}}}}}
    }}}});
    auto r = txn::from_value<WithVecStruct>(v);
    check(r.has_value() && r->points.size() == 2, "vector structs size");
    check(r->points[0].x == 1 && r->points[1].y == 4, "vector structs values");
}

void test_map() {
    auto v = make_table({{"env", MockValue{MockTable{
        {"a", MockValue{std::int64_t{1}}},
        {"b", MockValue{std::int64_t{2}}}
    }}}});
    auto r = txn::from_value<WithMap>(v);
    check(r.has_value() && r->env.size() == 2 && r->env["a"] == 1 && r->env["b"] == 2, "map");
}

void test_missing_key_error() {
    auto v = make_table({{"host", MockValue{"h"}}});
    auto r = txn::from_value<Server>(v);
    check(!r.has_value(), "missing key returns error");
    check(r.error().path == "port", "missing key error path");
}

void test_type_mismatch_error() {
    auto v = make_table({
        {"host", MockValue{std::int64_t{123}}},
        {"port", MockValue{std::int64_t{80}}}
    });
    auto r = txn::from_value<Server>(v);
    check(!r.has_value(), "type mismatch returns error");
    check(r.error().path == "host", "type mismatch error path");
}

void test_nested_error_path() {
    // Config -> Server -> port (wrong type). Verifies error escapes both
    // describable-inside-describable layers and reports the full dotted path.
    auto server = MockTable{
        {"host", MockValue{"localhost"}},
        {"port", MockValue{"not-an-int"}}
    };
    auto v = make_table({
        {"server", MockValue{std::move(server)}},
        {"tags", MockValue{MockArray{}}}
    });
    auto r = txn::from_value<Config>(v);
    check(!r.has_value(), "nested type mismatch returns error");
    check(r.error().path == "server.port", "nested error path is dotted");
}

void test_to_value_primitives() {
    auto sv = txn::to_value<MockValue>(std::string{"hi"});
    check(sv.is_string() && sv.as_string() == "hi", "to_value string");

    auto iv = txn::to_value<MockValue>(42);
    check(iv.is_integer() && iv.as_integer() == 42, "to_value int");

    auto fv = txn::to_value<MockValue>(1.5);
    check(fv.is_float() && std::abs(fv.as_float() - 1.5) < 1e-9, "to_value float");

    auto bv = txn::to_value<MockValue>(true);
    check(bv.is_bool() && bv.as_bool() == true, "to_value bool");
}

void test_to_value_struct() {
    Server s{"localhost", 8080, true};
    auto v = txn::to_value<MockValue>(s);
    check(v.is_table(), "to_value struct is table");
    auto const& t = v.as_table();
    check(t.at("host").as_string() == "localhost", "to_value struct host");
    check(t.at("port").as_integer() == 8080, "to_value struct port");
    check(t.at("debug").as_bool() == true, "to_value struct debug");
}

void test_to_value_optional_absent() {
    Server s{"h", 80, std::nullopt};
    auto v = txn::to_value<MockValue>(s);
    check(!v.as_table().contains("debug"), "to_value optional absent omitted");
}

void test_to_value_vector() {
    std::vector<int> vec{1, 2, 3};
    auto v = txn::to_value<MockValue>(vec);
    check(v.is_array(), "to_value vector is array");
    check(v.as_array().size() == 3, "to_value vector size");
    check(v.as_array()[1].as_integer() == 2, "to_value vector[1]");
}

void test_roundtrip() {
    Config original;
    original.server = {"example.com", 443, true};
    original.tags = {"api", "v2"};

    auto v = txn::to_value<MockValue>(original);
    auto r = txn::from_value<Config>(v);
    check(r.has_value(), "roundtrip ok");
    check(r->server.host == "example.com", "roundtrip host");
    check(r->server.port == 443, "roundtrip port");
    check(r->server.debug.has_value() && *r->server.debug == true, "roundtrip debug");
    check(r->tags.size() == 2 && r->tags[1] == "v2", "roundtrip tags");
}

// --- Auto-reflection tests ---

void test_auto_simple() {
    auto v = make_table({{"x", MockValue{std::int64_t{7}}},
                         {"y", MockValue{std::int64_t{8}}}});
    auto r = txn::from_value<AutoPoint>(v);
    check(r.has_value() && r->x == 7 && r->y == 8, "auto simple struct from_value");
}

void test_auto_to_value() {
    AutoPoint p{11, 22};
    auto v = txn::to_value<MockValue>(p);
    check(v.is_table(), "auto to_value is table");
    check(v.as_table().at("x").as_integer() == 11, "auto to_value x");
    check(v.as_table().at("y").as_integer() == 22, "auto to_value y");
}

void test_auto_with_optional() {
    auto v = make_table({
        {"host", MockValue{"h"}},
        {"port", MockValue{std::int64_t{80}}}
    });
    auto r = txn::from_value<AutoServer>(v);
    check(r.has_value() && r->host == "h" && r->port == 80, "auto server fields");
    check(r.has_value() && !r->debug.has_value(), "auto optional absent");

    AutoServer out{"x", 1, true};
    auto ov = txn::to_value<MockValue>(out);
    check(ov.as_table().at("debug").as_bool() == true, "auto optional to_value");
}

void test_auto_with_vector() {
    auto v = make_table({{"items", MockValue{MockArray{
        MockValue{std::int64_t{1}},
        MockValue{std::int64_t{2}},
        MockValue{std::int64_t{3}}
    }}}});
    auto r = txn::from_value<AutoWithVec>(v);
    check(r.has_value() && r->items.size() == 3 && r->items[2] == 3, "auto vector field");
}

void test_auto_roundtrip() {
    AutoServer original{"example.com", 443, true};
    auto v = txn::to_value<MockValue>(original);
    auto r = txn::from_value<AutoServer>(v);
    check(r.has_value(), "auto roundtrip ok");
    check(r->host == "example.com", "auto roundtrip host");
    check(r->port == 443, "auto roundtrip port");
    check(r->debug.has_value() && *r->debug == true, "auto roundtrip debug");
}

// --- Partial mode tests ---

void test_partial_describable_keeps_defaults() {
    // Only "port" provided; host keeps its member-init default.
    auto v = make_table({{"port", MockValue{std::int64_t{9000}}}});
    auto r = txn::from_value<ServerWithDefaults>(v, txn::Mode::Partial);
    check(r.has_value(), "partial describable ok");
    check(r->host == "localhost", "partial keeps default host");
    check(r->port == 9000, "partial reads provided port");
    check(!r->debug.has_value(), "partial optional still absent");
}

void test_partial_auto_keeps_defaults() {
    auto v = make_table({{"port", MockValue{std::int64_t{9000}}}});
    auto r = txn::from_value<AutoServerWithDefaults>(v, txn::Mode::Partial);
    check(r.has_value(), "partial auto ok");
    check(r->host == "localhost", "partial auto keeps default host");
    check(r->port == 9000, "partial auto reads provided port");
}

void test_partial_empty_table_keeps_all_defaults() {
    auto v = make_table({});
    auto r = txn::from_value<ServerWithDefaults>(v, txn::Mode::Partial);
    check(r.has_value(), "empty partial ok");
    check(r->host == "localhost" && r->port == 8080,
        "empty partial keeps all defaults");
}

void test_strict_still_errors_on_missing() {
    // Default mode must still error on missing non-optional field.
    auto v = make_table({{"port", MockValue{std::int64_t{9000}}}});
    auto r = txn::from_value<ServerWithDefaults>(v);
    check(!r.has_value(), "strict still errors on missing key");
}

void test_partial_type_mismatch_still_errors() {
    // Partial tolerates absence; it does NOT tolerate wrong types.
    auto v = make_table({{"port", MockValue{"not a number"}}});
    auto r = txn::from_value<ServerWithDefaults>(v, txn::Mode::Partial);
    check(!r.has_value(), "partial still errors on type mismatch");
    check(r.error().path == "port", "partial type-mismatch path");
}

void test_partial_nested_struct() {
    // Config contains a Server; Partial should descend into children
    // and keep defaults at every level.
    struct Cfg {
        ServerWithDefaults server;
        std::vector<std::string> tags;
    };
    // Use a local txn_describe inside a helper namespace to avoid
    // polluting the global namespace. Simpler: just auto-reflect.
    // AutoReflectable requires cppx::Reflectable; all plain-data struct
    // types here satisfy it.
    auto inner = make_table({{"port", MockValue{std::int64_t{1234}}}});
    auto v = make_table({
        {"server", MockValue{std::move(inner.as_table())}},
        {"tags", MockValue{MockArray{MockValue{"a"}}}}
    });
    auto r = txn::from_value<Cfg>(v, txn::Mode::Partial);
    check(r.has_value(), "partial nested ok");
    check(r->server.host == "localhost", "partial nested keeps inner default");
    check(r->server.port == 1234, "partial nested reads inner override");
    check(r->tags.size() == 1, "partial nested tags propagated");
}

// --- Custom-parser hook tests ---

void test_custom_parser_hex_string() {
    auto v = MockValue{"#0abab5"};
    auto r = txn::from_value<Color>(v);
    check(r.has_value(), "hex hook parse ok");
    check(r->r == 0x0a && r->g == 0xba && r->b == 0xb5 && r->a == 0xff,
        "hex hook bytes correct");
}

void test_custom_parser_decline_falls_back_to_reflection() {
    // Object shape: hook declines, reflection picks up the r/g/b/a fields.
    auto v = make_table({
        {"r", MockValue{std::int64_t{10}}},
        {"g", MockValue{std::int64_t{186}}},
        {"b", MockValue{std::int64_t{181}}},
        {"a", MockValue{std::int64_t{255}}}
    });
    auto r = txn::from_value<Color>(v);
    check(r.has_value(), "object shape ok via reflection fallback");
    check(r->r == 10 && r->g == 186 && r->b == 181 && r->a == 255,
        "object-shape bytes correct");
}

void test_custom_parser_propagates_error() {
    auto v = MockValue{"not-a-hex"};
    auto r = txn::from_value<Color>(v);
    check(!r.has_value(), "hex hook error propagates");
    check(r.error().message.contains("hex"),
        "hex hook error message mentions hex");
}

void test_custom_parser_with_partial_mode_in_nested_struct() {
    // Confirms hook + partial mode co-exist: a struct containing a Color
    // can be partially populated, and the Color field is still parsed by
    // the hook when present.
    struct Palette {
        Color primary;
        Color secondary;
    };
    auto v = make_table({{"primary", MockValue{"#ff0000"}}});
    auto r = txn::from_value<Palette>(v, txn::Mode::Partial);
    check(r.has_value(), "partial palette ok");
    check(r->primary.r == 255 && r->primary.g == 0 && r->primary.b == 0,
        "partial palette hook applied");
    check(r->secondary.r == 0 && r->secondary.a == 255,
        "partial palette kept default Color{}");
}

void test_describe_priority_over_auto() {
    // Aliased has a manual txn_describe with key "v"; auto-reflection is NOT used.
    auto v = make_table({{"v", MockValue{std::int64_t{42}}}});
    auto r = txn::from_value<Aliased>(v);
    check(r.has_value() && r->value == 42, "Describable override reads key 'v'");

    Aliased out{99};
    auto ov = txn::to_value<MockValue>(out);
    check(ov.as_table().contains("v"), "Describable override writes key 'v'");
    check(!ov.as_table().contains("value"), "auto-reflection did not run");
}

int main() {
    test_string();
    test_integer();
    test_float();
    test_bool();
    test_simple_struct();
    test_nested_struct();
    test_optional_present();
    test_optional_absent();
    test_vector_primitives();
    test_vector_structs();
    test_map();
    test_missing_key_error();
    test_type_mismatch_error();
    test_nested_error_path();
    test_to_value_primitives();
    test_to_value_struct();
    test_to_value_optional_absent();
    test_to_value_vector();
    test_roundtrip();
    test_auto_simple();
    test_auto_to_value();
    test_auto_with_optional();
    test_auto_with_vector();
    test_auto_roundtrip();
    test_describe_priority_over_auto();

    test_partial_describable_keeps_defaults();
    test_partial_auto_keeps_defaults();
    test_partial_empty_table_keeps_all_defaults();
    test_strict_still_errors_on_missing();
    test_partial_type_mismatch_still_errors();
    test_partial_nested_struct();

    test_custom_parser_hex_string();
    test_custom_parser_decline_falls_back_to_reflection();
    test_custom_parser_propagates_error();
    test_custom_parser_with_partial_mode_in_nested_struct();

    if (failed > 0) {
        std::println(std::cerr, "\n{} test(s) failed", failed);
        return 1;
    }
    std::println("all tests passed");
    return 0;
}
