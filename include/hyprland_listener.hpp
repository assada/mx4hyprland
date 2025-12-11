#pragma once

#include "config.hpp"
#include "haptic_manager.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace mx4hyprland {

class HyprlandListener {
public:
    HyprlandListener(
        std::shared_ptr<HapticManager> manager,
        AppConfig config
    );

    ~HyprlandListener();

    HyprlandListener(const HyprlandListener&) = delete;
    HyprlandListener& operator=(const HyprlandListener&) = delete;
    HyprlandListener(HyprlandListener&&) = delete;
    HyprlandListener& operator=(HyprlandListener&&) = delete;

    void start();
    void stop();

    void update_config(AppConfig new_config);

private:
    void listener_loop(std::stop_token stop_token);
    void process_event(std::string_view raw_line);

    [[nodiscard]] std::filesystem::path get_socket_path() const;

    std::shared_ptr<HapticManager> manager_;
    AppConfig config_;
    std::mutex config_mutex_;

    std::unordered_map<std::string, std::string> event_cache_;

    std::jthread listener_thread_;
    std::atomic<bool> running_{false};

    static constexpr std::array DEDUP_EVENTS = { // TODO: move to config
        std::string_view{"workspace"},
        std::string_view{"activewindow"},
        std::string_view{"focusedmon"},
        std::string_view{"activewindowv2"}
    };
};

}
