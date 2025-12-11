#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace mx4hyprland {

struct EventConfig {
    std::optional<int> default_effect;
    std::unordered_map<std::string, int> args;
};

using EventValue = std::variant<int, EventConfig>;

struct AppConfig {
    std::optional<int> default_effect;
    std::unordered_map<std::string, EventValue> events;

    [[nodiscard]] std::optional<int> get_effect(
        std::string_view event_name,
        std::string_view event_args
    ) const;

    static AppConfig load(const std::optional<std::filesystem::path>& config_path = std::nullopt);
};

[[nodiscard]] std::filesystem::path get_xdg_config_home();
[[nodiscard]] std::filesystem::path get_xdg_runtime_dir();

inline constexpr std::string_view APP_NAME = "mx4hyprland";
inline constexpr std::string_view CONFIG_FILENAME = "config.toml";

}
