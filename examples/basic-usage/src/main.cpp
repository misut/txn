import txn;
import std;

// Minimal ValueLike implementation for the demo (self-contained, no external deps).
// In a real application you would use toml::Value, a JSON value, or your own tree.
struct DemoValue;

using DemoTable = std::map<std::string, DemoValue>;
using DemoArray = std::vector<DemoValue>;

struct DemoValue {
    using Data = std::variant<
        std::string, std::int64_t, double, bool,
        std::unique_ptr<DemoArray>, std::unique_ptr<DemoTable>
    >;
    Data data;

    DemoValue() : data{std::string{}} {}
    DemoValue(std::string v) : data{std::move(v)} {}
    DemoValue(char const* v) : data{std::string{v}} {}
    DemoValue(std::int64_t v) : data{v} {}
    DemoValue(double v) : data{v} {}
    DemoValue(bool v) : data{v} {}
    DemoValue(DemoArray v) : data{std::make_unique<DemoArray>(std::move(v))} {}
    DemoValue(DemoTable v) : data{std::make_unique<DemoTable>(std::move(v))} {}

    // Full copy support (required because of unique_ptr<...> in the variant).
    DemoValue(DemoValue const& o) {
        std::visit([this](auto const& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<DemoArray>>)
                data = std::make_unique<DemoArray>(*v);
            else if constexpr (std::is_same_v<T, std::unique_ptr<DemoTable>>)
                data = std::make_unique<DemoTable>(*v);
            else
                data = v;
        }, o.data);
    }
    DemoValue(DemoValue&&) noexcept = default;
    DemoValue& operator=(DemoValue const& o) {
        if (this != &o) { DemoValue tmp{o}; *this = std::move(tmp); }
        return *this;
    }
    DemoValue& operator=(DemoValue&&) noexcept = default;

    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_integer() const { return std::holds_alternative<std::int64_t>(data); }
    bool is_float() const { return std::holds_alternative<double>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_array() const { return std::holds_alternative<std::unique_ptr<DemoArray>>(data); }
    bool is_table() const { return std::holds_alternative<std::unique_ptr<DemoTable>>(data); }

    std::string_view as_string() const { return std::get<std::string>(data); }
    std::int64_t as_integer() const { return std::get<std::int64_t>(data); }
    double as_float() const { return std::get<double>(data); }
    bool as_bool() const { return std::get<bool>(data); }
    DemoArray const& as_array() const { return *std::get<std::unique_ptr<DemoArray>>(data); }
    DemoTable const& as_table() const { return *std::get<std::unique_ptr<DemoTable>>(data); }
};

DemoValue make_table(DemoTable t) { return DemoValue{std::move(t)}; }

// --- Types exercised via auto-reflection (no txn_describe needed) ---

struct Server {
    std::string host;
    int port;
    std::optional<bool> debug;
};

struct Config {
    Server server;
    std::vector<std::string> tags;
};

int main() {
    // Build a DemoValue tree that matches the shape of Config.
    auto server = DemoTable{
        {"host", DemoValue{"example.com"}},
        {"port", DemoValue{std::int64_t{443}}},
        {"debug", DemoValue{true}}
    };
    auto v = make_table({
        {"server", DemoValue{std::move(server)}},
        {"tags", DemoValue{DemoArray{
            DemoValue{"api"}, DemoValue{"v2"}
        }}}
    });

    // Deserialize using auto-reflection.
    auto result = txn::from_value<Config>(v);
    if (!result) {
        std::println(std::cerr, "from_value failed: {} at {}",
                     result.error().message, result.error().path);
        return 1;
    }

    auto const& cfg = *result;
    std::println("[basic-usage] from_value succeeded");
    std::println("  server.host = {}", cfg.server.host);
    std::println("  server.port = {}", cfg.server.port);
    std::println("  server.debug = {}", cfg.server.debug.value_or(false));
    std::println("  tags[0] = {}", cfg.tags[0]);

    // Serialize back (roundtrip).
    auto back = txn::to_value<DemoValue>(cfg);
    auto round = txn::from_value<Config>(back);
    if (!round || round->server.host != "example.com" || round->tags.size() != 2) {
        std::println(std::cerr, "roundtrip failed");
        return 1;
    }

    std::println("[basic-usage] roundtrip OK (auto-reflection + optional + vector + nesting)");
    return 0;
}
