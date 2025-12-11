#include "haptic_manager.hpp"
#include "logger.hpp"

namespace mx4hyprland {

HapticManager::HapticManager(std::unique_ptr<MXMaster4> device)
    : device_(std::move(device))
{}

HapticManager::~HapticManager() {
    stop();
}

void HapticManager::start() {
    if (running_.exchange(true)) {
        return;
    }

    worker_thread_ = std::jthread([this](std::stop_token st) {
        worker_loop(st);
    });

    logger().info("Haptic Manager started");
}

void HapticManager::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    worker_thread_.request_stop();
    queue_cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    if (device_) {
        device_->close();
    }
}

void HapticManager::trigger(int effect_id) {
    std::lock_guard lock(queue_mutex_);

    if (queue_.size() >= MAX_QUEUE_SIZE) {
        logger().warning("Haptic queue full, dropping event");
        return;
    }

    queue_.push(effect_id);
    queue_cv_.notify_one();
}

void HapticManager::worker_loop(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        int effect_id = -1;

        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, stop_token, [this] {
                return !queue_.empty();
            });

            if (stop_token.stop_requested() && queue_.empty()) {
                break;
            }

            if (!queue_.empty()) {
                effect_id = queue_.front();
                queue_.pop();
            }
        }

        if (effect_id >= 0) {
            safe_send(effect_id);
        }
    }
}

void HapticManager::safe_send(int effect_id) {
    try {
        device_->send_haptic_feedback(effect_id);
    } catch (const DeviceDisconnectedError&) {
        logger().warning("Device disconnected, attempting reconnect...");
        device_->close();

        try {
            device_->open();
            device_->send_haptic_feedback(effect_id);
        } catch (const std::exception& e) {
            logger().error("Reconnect failed: ", e.what());
        }
    } catch (const std::exception& e) {
        logger().error("Unexpected HID error: ", e.what());
    }
}

}
