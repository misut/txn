import txn;
import std;

// Minimal self-contained ValueLike (duplicated for self-contained example).
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

// --- Color with custom hook (exact logic from txn tests) ---

struct Color {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
};

template<txn::ValueLike V>
inline auto txn_from_value(txn::tag<Color>, V const& v, std::string const& path)
    -> std::optional<std::expected<Color, txn::ConversionError>>
{
    if (!v.is_string()) return std::nullopt;  // decline -> fall back to reflection
    auto s = v.as_string();
    if (s.size() != 7 || s[0] != '#') {
        return std::expected<Color, txn::ConversionError>{std::unexpected(
            txn::ConversionError{path, "hex color must be 7 chars: '#rrggbb'"})};
    }
    auto parse = [&](std::string_view sv) -> std::expected<unsigned char, txn::ConversionError> {
        unsigned int value = 0;
        auto const* first = sv.data();
        auto const* last = sv.data() + sv.size();
        auto [ptr, ec] = std::from_chars(first, last, value, 16);
        if (ec != std::errc{} || ptr != last || value > 255) {
            return std::unexpected(txn::ConversionError{path, std::format("invalid hex byte '{}'", sv)});
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

int main() {
    // 1. Hex string -> hook handles it
    auto v1 = DemoValue{"#ff8800"};
    auto c1 = txn::from_value<Color>(v1);
    if (c1) {
        std::println("[custom-hook] hex string parsed via hook: r={} g={} b={}", c1->r, c1->g, c1->b);
    }

    // 2. Object shape -> hook declines, reflection takes over
    auto v2 = make_table({{"r", DemoValue{std::int64_t{10}}}, {"g", DemoValue{std::int64_t{20}}}, {"b", DemoValue{std::int64_t{30}}}, {"a", DemoValue{std::int64_t{255}}}});
    auto c2 = txn::from_value<Color>(v2);
    if (c2) {
        std::println("[custom-hook] object shape fell back to reflection: r={} g={} b={}", c2->r, c2->g, c2->b);
    }

    // 3. Bad hex -> hook error propagates
    auto v3 = DemoValue{"#zz"};
    auto c3 = txn::from_value<Color>(v3);
    if (!c3) {
        std::println("[custom-hook] expected error from hook: {}", c3.error().message);
    }

    std::println("[custom-hook] All hook behaviors demonstrated successfully.");
    return 0;
}
