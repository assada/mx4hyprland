#pragma once

#include "haptic_manager.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>

namespace mx4hyprland {

class IPCServer {
public:
    IPCServer(
        std::shared_ptr<HapticManager> manager,
        std::filesystem::path socket_path
    );

    ~IPCServer();

    IPCServer(const IPCServer&) = delete;
    IPCServer& operator=(const IPCServer&) = delete;
    IPCServer(IPCServer&&) = delete;
    IPCServer& operator=(IPCServer&&) = delete;

    void start();
    void stop();

private:
    void server_loop(std::stop_token stop_token);
    void handle_client(int client_fd);

    std::shared_ptr<HapticManager> manager_;
    std::filesystem::path socket_path_;

    std::jthread server_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> server_fd_{-1};
};

}
