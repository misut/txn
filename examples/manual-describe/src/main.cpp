import txn;
import std;

// Minimal self-contained ValueLike (duplicated from basic-usage for self-contained examples).
struct DemoValue;
using DemoTable = std::map<std::string, DemoValue>;
using DemoArray = std::vector<DemoValue>;
struct DemoValue {
    using Data = std::variant<std::string, std::int64_t, double, bool, std::unique_ptr<DemoArray>, std::unique_ptr<DemoTable>>;
    Data data;
    DemoValue() : data{std::string{}} {}
    DemoValue(std::string v) : data{std::move(v)} {}
    DemoValue(char const* v) : data{std::string{v}} {}
    DemoValue(std::int64_t v) : data{v} {}
    DemoValue(double v) : data{v} {}
    DemoValue(bool v) : data{v} {}
    DemoValue(DemoArray v) : data{std::make_unique<DemoArray>(std::move(v))} {}
    DemoValue(DemoTable v) : data{std::make_unique<DemoTable>(std::move(v))} {}
    DemoValue(DemoValue const& o) {
        std::visit([this](auto const& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<DemoArray>>) data = std::make_unique<DemoArray>(*v);
            else if constexpr (std::is_same_v<T, std::unique_ptr<DemoTable>>) data = std::make_unique<DemoTable>(*v);
            else data = v;
        }, o.data);
    }
    DemoValue(DemoValue&&) noexcept = default;
    DemoValue& operator=(DemoValue const& o) { if (this != &o) { DemoValue tmp{o}; *this = std::move(tmp); } return *this; }
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
    DemoTable const& as_table() const { return *std::get<std::unique_ptr<DemoTable>>(data); }
};
DemoValue make_table(DemoTable t) { return DemoValue{std::move(t)}; }

// Struct with manual descriptor (renames "host" -> "server_host" in the external format).
struct Server {
    std::string host;
    int port;
};
inline auto txn_describe(Server*) {
    return txn::describe<Server>(
        txn::field(&Server::host, "server_host"),
        txn::field(&Server::port, "port"));
}

int main() {
    // External data uses the custom key "server_host".
    auto v = make_table({
        {"server_host", DemoValue{"custom.example"}},
        {"port", DemoValue{std::int64_t{8080}}}
    });

    auto result = txn::from_value<Server>(v);
    if (!result) {
        std::println(std::cerr, "failed: {}", result.error().message);
        return 1;
    }
    std::println("[manual-describe] from_value with custom key OK: host={} port={}",
                 result->host, result->port);

    auto back = txn::to_value<DemoValue>(*result);
    auto const& t = back.as_table();
    std::println("[manual-describe] to_value wrote custom key 'server_host' = {}", t.at("server_host").as_string());
    std::println("[manual-describe] (auto field name 'host' was NOT used)");
    return 0;
}
