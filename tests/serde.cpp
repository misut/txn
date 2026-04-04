import txn;
import std;

#include "txn_describe.h"

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
TXN_DESCRIBE(Point, x, y)

struct Server {
    std::string host;
    int port;
    std::optional<bool> debug;
};
TXN_DESCRIBE(Server, host, port, debug)

struct Config {
    Server server;
    std::vector<std::string> tags;
};
TXN_DESCRIBE(Config, server, tags)

struct WithVecStruct {
    std::vector<Point> points;
};
TXN_DESCRIBE(WithVecStruct, points)

struct WithMap {
    std::map<std::string, int> env;
};
TXN_DESCRIBE(WithMap, env)

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
    auto s = txn::from_value<std::string>(v);
    check(s == "hello", "string from_value");
}

void test_integer() {
    auto v = MockValue{std::int64_t{42}};
    auto i = txn::from_value<int>(v);
    check(i == 42, "integer from_value");
}

void test_float() {
    auto v = MockValue{3.14};
    auto f = txn::from_value<double>(v);
    check(std::abs(f - 3.14) < 1e-9, "float from_value");
}

void test_bool() {
    auto v = MockValue{true};
    auto b = txn::from_value<bool>(v);
    check(b == true, "bool from_value");
}

void test_simple_struct() {
    auto v = make_table({{"x", MockValue{std::int64_t{1}}},
                         {"y", MockValue{std::int64_t{2}}}});
    auto p = txn::from_value<Point>(v);
    check(p.x == 1 && p.y == 2, "simple struct from_value");
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
    auto cfg = txn::from_value<Config>(v);
    check(cfg.server.host == "localhost", "nested struct host");
    check(cfg.server.port == 8080, "nested struct port");
    check(cfg.server.debug == std::nullopt, "nested struct optional absent");
    check(cfg.tags.size() == 2, "nested struct tags size");
    check(cfg.tags[0] == "web", "nested struct tags[0]");
}

void test_optional_present() {
    auto v = make_table({
        {"host", MockValue{"h"}},
        {"port", MockValue{std::int64_t{80}}},
        {"debug", MockValue{true}}
    });
    auto s = txn::from_value<Server>(v);
    check(s.debug.has_value() && *s.debug == true, "optional present");
}

void test_optional_absent() {
    auto v = make_table({
        {"host", MockValue{"h"}},
        {"port", MockValue{std::int64_t{80}}}
    });
    auto s = txn::from_value<Server>(v);
    check(!s.debug.has_value(), "optional absent");
}

void test_vector_primitives() {
    auto v = MockValue{MockArray{
        MockValue{std::int64_t{1}},
        MockValue{std::int64_t{2}},
        MockValue{std::int64_t{3}}
    }};
    auto vec = txn::from_value<std::vector<int>>(v);
    check(vec.size() == 3 && vec[0] == 1 && vec[2] == 3, "vector primitives");
}

void test_vector_structs() {
    auto v = make_table({{"points", MockValue{MockArray{
        MockValue{MockTable{{"x", MockValue{std::int64_t{1}}}, {"y", MockValue{std::int64_t{2}}}}},
        MockValue{MockTable{{"x", MockValue{std::int64_t{3}}}, {"y", MockValue{std::int64_t{4}}}}}
    }}}});
    auto ws = txn::from_value<WithVecStruct>(v);
    check(ws.points.size() == 2, "vector structs size");
    check(ws.points[0].x == 1 && ws.points[1].y == 4, "vector structs values");
}

void test_map() {
    auto v = make_table({{"env", MockValue{MockTable{
        {"a", MockValue{std::int64_t{1}}},
        {"b", MockValue{std::int64_t{2}}}
    }}}});
    auto wm = txn::from_value<WithMap>(v);
    check(wm.env.size() == 2 && wm.env["a"] == 1 && wm.env["b"] == 2, "map");
}

void test_missing_key_error() {
    auto v = make_table({{"host", MockValue{"h"}}});
    bool caught = false;
    try {
        txn::from_value<Server>(v);
    } catch (txn::ConversionError const& e) {
        caught = true;
        check(std::string{e.path()} == "port", "missing key error path");
    }
    check(caught, "missing key throws");
}

void test_type_mismatch_error() {
    auto v = make_table({
        {"host", MockValue{std::int64_t{123}}},
        {"port", MockValue{std::int64_t{80}}}
    });
    bool caught = false;
    try {
        txn::from_value<Server>(v);
    } catch (txn::ConversionError const& e) {
        caught = true;
        check(std::string{e.path()} == "host", "type mismatch error path");
    }
    check(caught, "type mismatch throws");
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
    auto restored = txn::from_value<Config>(v);

    check(restored.server.host == "example.com", "roundtrip host");
    check(restored.server.port == 443, "roundtrip port");
    check(restored.server.debug.has_value() && *restored.server.debug == true, "roundtrip debug");
    check(restored.tags.size() == 2 && restored.tags[1] == "v2", "roundtrip tags");
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
    test_to_value_primitives();
    test_to_value_struct();
    test_to_value_optional_absent();
    test_to_value_vector();
    test_roundtrip();

    if (failed > 0) {
        std::println(std::cerr, "\n{} test(s) failed", failed);
        return 1;
    }
    std::println("all tests passed");
    return 0;
}
