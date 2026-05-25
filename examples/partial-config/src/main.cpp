import txn;
import std;

// Minimal self-contained ValueLike.
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

// Struct with C++ defaults (member initializers).
struct Theme {
    std::string accent = "blue";
    int radius = 4;
    std::optional<bool> enabled;
};

inline auto txn_describe(Theme*) {
    return txn::describe<Theme>(
        txn::field(&Theme::accent, "accent"),
        txn::field(&Theme::radius, "radius"),
        txn::field(&Theme::enabled, "enabled"));
}

int main() {
    // Only provide "accent" — radius and enabled should keep C++ defaults in Partial mode.
    auto v = make_table({{"accent", DemoValue{"tiffany"}}});

    // Strict mode: should fail on missing radius
    auto strict = txn::from_value<Theme>(v);
    std::println("[partial-config] Strict mode missing key error? {}", !strict);

    // Partial mode: keeps defaults
    auto partial = txn::from_value<Theme>(v, txn::Mode::Partial);
    if (partial) {
        std::println("[partial-config] Partial OK: accent='{}' (provided), radius={} (default kept), enabled={}",
                     partial->accent, partial->radius, partial->enabled.has_value());
    }

    // Empty table still keeps all defaults
    auto empty = make_table({});
    auto p2 = txn::from_value<Theme>(empty, txn::Mode::Partial);
    if (p2) {
        std::println("[partial-config] Empty partial keeps all defaults: accent='{}' radius={}",
                     p2->accent, p2->radius);
    }

    std::println("[partial-config] Partial mode overlay behavior demonstrated.");
    return 0;
}
