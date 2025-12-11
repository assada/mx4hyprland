#include "config.hpp"
#include "logger.hpp"

#include <cstdlib>
#include <toml++/toml.hpp>
#include <unistd.h>

namespace mx4hyprland {

std::filesystem::path get_xdg_config_home() {
    if (const char* env = std::getenv("XDG_CONFIG_HOME"); env != nullptr) {
        return env;
    }
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        return std::filesystem::path(home) / ".config";
    }
    return "~/.config";
}

std::filesystem::path get_xdg_runtime_dir() {
    if (const char* env = std::getenv("XDG_RUNTIME_DIR"); env != nullptr) {
        return env;
    }
    return std::filesystem::path("/run/user") / std::to_string(getuid());
}

std::optional<int> AppConfig::get_effect(
    std::string_view event_name,
    std::string_view event_args
) const {
    auto it = events.find(std::string(event_name));
    if (it == events.end()) {
        return default_effect;
    }

    return std::visit(
        [this, event_args](auto&& val) -> std::optional<int> {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, int>) {
                return val;
            } else {
                auto arg_it = val.args.find(std::string(event_args));
                if (arg_it != val.args.end()) {
                    return arg_it->second;
                }
                if (val.default_effect) {
                    return val.default_effect;
                }
                return default_effect;
            }
        },
        it->second
    );
}

AppConfig AppConfig::load(const std::optional<std::filesystem::path>& config_path) {
    std::vector<std::filesystem::path> paths;

    if (config_path) {
        paths.push_back(*config_path);
    } else {
        paths.push_back(get_xdg_config_home() / APP_NAME / CONFIG_FILENAME);
        paths.emplace_back(CONFIG_FILENAME);
    }

    for (const auto& path : paths) {
        if (!std::filesystem::exists(path)) {
            continue;
        }

        try {
            auto tbl = toml::parse_file(path.string());
            AppConfig config;

            if (auto def = tbl["default_effect"].value<int64_t>()) {
                config.default_effect = static_cast<int>(*def);
            }

            if (auto events_tbl = tbl["events"].as_table()) {
                for (const auto& [key, value] : *events_tbl) {
                    std::string event_name(key.str());

                    if (auto effect_val = value.value<int64_t>()) {
                        config.events[event_name] = static_cast<int>(*effect_val);
                    } else if (auto event_tbl = value.as_table()) {
                        EventConfig event_config;

                        if (auto def = (*event_tbl)["default"].value<int64_t>()) {
                            event_config.default_effect = static_cast<int>(*def);
                        }

                        if (auto args_tbl = (*event_tbl)["args"].as_table()) {
                            for (const auto& [arg_key, arg_value] : *args_tbl) {
                                if (auto effect = arg_value.value<int64_t>()) {
                                    event_config.args[std::string(arg_key.str())] =
                                        static_cast<int>(*effect);
                                }
                            }
                        }

                        config.events[event_name] = std::move(event_config);
                    }
                }
            }

            logger().info("Config loaded from ", path.string());
            return config;

        } catch (const toml::parse_error& err) {
            logger().error("Failed to parse config ", path.string(), ": ", err.description());
        }
    }

    logger().warning("No config found, using defaults");
    return {};
}

}
