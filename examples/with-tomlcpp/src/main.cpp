import txn;
import toml;
import std;

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
    std::string_view toml_source = R"(
tags = ["api", "v2", "legacy"]

[server]
host = "production.example.com"
port = 443
debug = true
)";

    // Real TOML parsing — wrap the root Table in toml::Value so it satisfies txn::ValueLike
    auto table = toml::parse(toml_source);
    auto root = toml::Value{std::move(table)};

    // txn deserialization directly from the real toml::Value
    auto result = txn::from_value<Config>(root);
    if (!result) {
        std::println(std::cerr, "txn from toml failed: {} at {}", result.error().message, result.error().path);
        return 1;
    }

    auto const& cfg = *result;
    std::println("[with-tomlcpp] Successfully loaded real TOML via txn + tomlcpp");
    std::println("  server.host = {}", cfg.server.host);
    std::println("  server.port = {}", cfg.server.port);
    std::println("  server.debug = {}", cfg.server.debug.value_or(false));
    std::println("  tags = [{}]", std::string{cfg.tags[0]} + ", " + cfg.tags[1] + ", " + cfg.tags[2]);

    // Roundtrip back to a new toml::Value (demonstrates to_value with real provider)
    auto serialized = txn::to_value<toml::Value>(cfg);
    std::println("[with-tomlcpp] Roundtrip to toml::Value succeeded. server.host in serialized = {}",
                 serialized.as_table().at("server").as_table().at("host").as_string());

    std::println("[with-tomlcpp] End-to-end real TOML + txn integration complete.");
    return 0;
}
