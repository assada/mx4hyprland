#include "hyprland_listener.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace mx4hyprland {

namespace {

constexpr size_t BUFFER_SIZE = 4096;
constexpr auto RECONNECT_DELAY = std::chrono::seconds(3);
constexpr auto SHORT_RECONNECT_DELAY = std::chrono::seconds(1);

class SocketRAII {
public:
    explicit SocketRAII(int fd = -1) : fd_(fd) {}

    ~SocketRAII() {
        close_socket();
    }

    SocketRAII(const SocketRAII&) = delete;
    SocketRAII& operator=(const SocketRAII&) = delete;

    SocketRAII(SocketRAII&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    SocketRAII& operator=(SocketRAII&& other) noexcept {
        if (this != &other) {
            close_socket();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const { return fd_; }
    [[nodiscard]] bool valid() const { return fd_ >= 0; }

    void reset(int new_fd = -1) {
        close_socket();
        fd_ = new_fd;
    }

private:
    void close_socket() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_;
};

}

HyprlandListener::HyprlandListener(
    std::shared_ptr<HapticManager> manager,
    AppConfig config
)
    : manager_(std::move(manager))
    , config_(std::move(config))
{}

HyprlandListener::~HyprlandListener() {
    stop();
}

void HyprlandListener::start() {
    if (running_.exchange(true)) {
        return;
    }

    listener_thread_ = std::jthread([this](std::stop_token st) {
        listener_loop(st);
    });
}

void HyprlandListener::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    listener_thread_.request_stop();

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
}

void HyprlandListener::update_config(AppConfig new_config) {
    std::lock_guard lock(config_mutex_);
    config_ = std::move(new_config);
    logger().info("Hyprland listener config updated");
}

std::filesystem::path HyprlandListener::get_socket_path() const {
    const char* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature == nullptr) {
        return {};
    }

    return get_xdg_runtime_dir() / "hypr" / signature / ".socket2.sock";
}

void HyprlandListener::listener_loop(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        auto socket_path = get_socket_path();
        if (socket_path.empty()) {
            logger().error("HYPRLAND_INSTANCE_SIGNATURE not found");
            std::this_thread::sleep_for(RECONNECT_DELAY);
            continue;
        }

        SocketRAII sock(socket(AF_UNIX, SOCK_STREAM, 0));
        if (!sock.valid()) {
            logger().error("Failed to create socket: ", std::strerror(errno));
            std::this_thread::sleep_for(RECONNECT_DELAY);
            continue;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            logger().warning("Hyprland socket unreachable, retrying...");
            std::this_thread::sleep_for(RECONNECT_DELAY);
            continue;
        }

        logger().info("Connected to Hyprland socket2");

        std::array<char, BUFFER_SIZE> buffer{};
        std::string line_buffer;

        while (!stop_token.stop_requested()) {
            ssize_t bytes_read = recv(sock.get(), buffer.data(), buffer.size() - 1, 0);

            if (bytes_read <= 0) {
                if (bytes_read == 0) {
                    logger().warning("Hyprland connection closed, reconnecting...");
                } else {
                    logger().error("Socket read error: ", std::strerror(errno));
                }
                break;
            }

            buffer[static_cast<size_t>(bytes_read)] = '\0';
            line_buffer.append(buffer.data(), static_cast<size_t>(bytes_read));

            size_t pos;
            while ((pos = line_buffer.find('\n')) != std::string::npos) {
                std::string line = line_buffer.substr(0, pos);
                line_buffer.erase(0, pos + 1);

                if (line.find(">>") != std::string::npos) {
                    process_event(line);
                }
            }
        }

        std::this_thread::sleep_for(SHORT_RECONNECT_DELAY);
    }
}

void HyprlandListener::process_event(std::string_view raw_line) {
    auto separator_pos = raw_line.find(">>");
    if (separator_pos == std::string_view::npos) {
        return;
    }

    std::string event(raw_line.substr(0, separator_pos));
    std::string args(raw_line.substr(separator_pos + 2));

    bool should_dedup = std::ranges::any_of(DEDUP_EVENTS, [&event](std::string_view e) {
        return e == event;
    });

    if (should_dedup) {
        auto it = event_cache_.find(event);
        if (it != event_cache_.end() && it->second == args) {
            return;
        }
        event_cache_[event] = args;
    }

    std::optional<int> effect_id;
    {
        std::lock_guard lock(config_mutex_);
        effect_id = config_.get_effect(event, args);
    }

    if (effect_id) {
        manager_->trigger(*effect_id);
    }
}

}
