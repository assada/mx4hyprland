#include "config.hpp"
#include "haptic_manager.hpp"
#include "hyprland_listener.hpp"
#include "ipc_server.hpp"
#include "logger.hpp"
#include "mx_master_4.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::atomic<bool> g_should_stop{false};
std::atomic<bool> g_should_reload{false};

void signal_handler(int sig) {
    if (sig == SIGHUP) {
        g_should_reload.store(true);
    } else {
        g_should_stop.store(true);
    }
}

void setup_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}

struct Args {
    std::optional<std::filesystem::path> config_path;
    std::string log_level = "info";
};

Args parse_args(int argc, char* argv[]) {
    Args args;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            args.config_path = argv[++i];
        } else if ((arg == "-l" || arg == "--log-level") && i + 1 < argc) {
            args.log_level = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: mx4hyprland [OPTIONS]\n"
                      << "\n"
                      << "Options:\n"
                      << "  -c, --config <PATH>    Path to config file\n"
                      << "  -l, --log-level <LVL>  Log level (debug, info, warning, error)\n"
                      << "  -h, --help             Show this help\n";
            std::exit(0);
        }
    }

    return args;
}

}

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);

    mx4hyprland::logger().set_level(args.log_level);

    setup_signal_handlers();

    auto config = mx4hyprland::AppConfig::load(args.config_path);

    auto device = mx4hyprland::MXMaster4::find();
    if (!device) {
        mx4hyprland::logger().error("MX Master 4 not found");
        return 1;
    }

    auto haptic_manager = std::make_shared<mx4hyprland::HapticManager>(
        std::make_unique<mx4hyprland::MXMaster4>(std::move(*device))
    );

    auto ipc_socket_path = mx4hyprland::get_xdg_runtime_dir() /
        (std::string(mx4hyprland::APP_NAME) + ".sock");

    auto ipc_server = std::make_unique<mx4hyprland::IPCServer>(
        haptic_manager,
        ipc_socket_path
    );

    auto hyprland_listener = std::make_unique<mx4hyprland::HyprlandListener>(
        haptic_manager,
        config
    );

    haptic_manager->start();
    ipc_server->start();
    hyprland_listener->start();

    mx4hyprland::logger().info("mx4hyprland started");

    while (!g_should_stop.load()) {
        if (g_should_reload.exchange(false)) {
            auto new_config = mx4hyprland::AppConfig::load(args.config_path);
            hyprland_listener->update_config(std::move(new_config));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    mx4hyprland::logger().info("Shutting down...");

    hyprland_listener->stop();
    ipc_server->stop();
    haptic_manager->stop();

    return 0;
}
