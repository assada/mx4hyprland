#include "ipc_server.hpp"
#include "logger.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace mx4hyprland {

namespace {

constexpr size_t MAX_MESSAGE_SIZE = 128;
constexpr int LISTEN_BACKLOG = 5;

class ClientSocketRAII {
public:
    explicit ClientSocketRAII(int fd) : fd_(fd) {}

    ~ClientSocketRAII() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    ClientSocketRAII(const ClientSocketRAII&) = delete;
    ClientSocketRAII& operator=(const ClientSocketRAII&) = delete;

    [[nodiscard]] int get() const { return fd_; }

private:
    int fd_;
};

}

IPCServer::IPCServer(
    std::shared_ptr<HapticManager> manager,
    std::filesystem::path socket_path
)
    : manager_(std::move(manager))
    , socket_path_(std::move(socket_path))
{}

IPCServer::~IPCServer() {
    stop();
}

void IPCServer::start() {
    if (running_.exchange(true)) {
        return;
    }

    server_thread_ = std::jthread([this](std::stop_token st) {
        server_loop(st);
    });
}

void IPCServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    server_thread_.request_stop();

    int fd = server_fd_.exchange(-1);
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    if (std::filesystem::exists(socket_path_)) {
        std::filesystem::remove(socket_path_);
    }
}

void IPCServer::server_loop(std::stop_token stop_token) {
    if (std::filesystem::exists(socket_path_)) {
        std::filesystem::remove(socket_path_);
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        logger().error("Failed to create IPC socket: ", std::strerror(errno));
        return;
    }

    server_fd_.store(sock);

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        logger().error("Failed to bind IPC socket: ", std::strerror(errno));
        return;
    }

    chmod(socket_path_.c_str(), 0600);

    if (listen(sock, LISTEN_BACKLOG) < 0) {
        logger().error("Failed to listen on IPC socket: ", std::strerror(errno));
        return;
    }

    logger().info("IPC listening on ", socket_path_.string());

    while (!stop_token.stop_requested()) {
        int client_fd = accept(sock, nullptr, nullptr);

        if (client_fd < 0) {
            if (errno == EINTR || errno == EBADF) {
                break;
            }
            logger().error("Accept failed: ", std::strerror(errno));
            continue;
        }

        handle_client(client_fd);
    }
}

void IPCServer::handle_client(int client_fd) {
    ClientSocketRAII client(client_fd);

    std::array<char, MAX_MESSAGE_SIZE> buffer{};
    ssize_t bytes_read = recv(client.get(), buffer.data(), buffer.size() - 1, 0);

    if (bytes_read <= 0) {
        return;
    }

    buffer[static_cast<size_t>(bytes_read)] = '\0';

    std::string_view message(buffer.data());

    auto end_pos = message.find_first_of("\r\n ");
    if (end_pos != std::string_view::npos) {
        message = message.substr(0, end_pos);
    }

    int effect_id = 0;
    auto [ptr, ec] = std::from_chars(message.data(), message.data() + message.size(), effect_id);

    if (ec == std::errc{} && ptr == message.data() + message.size()) {
        manager_->trigger(effect_id);
    } else {
        logger().debug("Received unknown IPC command: ", message);
    }
}

}
