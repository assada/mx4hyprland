#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

#include <hidapi/hidapi.h>

namespace mx4hyprland {

inline constexpr uint16_t LOGITECH_VID = 0x046D;
inline constexpr uint16_t MX_MASTER_4_BLUETOOTH_PID = 0xB042;
inline constexpr std::string_view MX_MASTER_4_BLUETOOTH_NAME = "MX Master 4";

inline constexpr int EFFECT_MIN = 0;
inline constexpr int EFFECT_MAX = 15;

enum class ConnectionType {
    Bolt,
    Bluetooth
};

enum class ReportID : uint8_t {
    Short = 0x10,
    Long = 0x11
};

enum class FunctionID : uint16_t {
    IRoot = 0x0000,
    IFeatureSet = 0x0001,
    IFeatureInfo = 0x0002,
    Haptic = 0x0B4E
};

class DeviceDisconnectedError : public std::runtime_error {
public:
    explicit DeviceDisconnectedError(const std::string& msg)
        : std::runtime_error(msg) {}
};

class HidDeviceDeleter {
public:
    void operator()(hid_device* dev) const {
        if (dev != nullptr) {
            hid_close(dev);
        }
    }
};

using HidDevicePtr = std::unique_ptr<hid_device, HidDeviceDeleter>;

class FileDescriptor {
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int fd) : fd_(fd) {}

    ~FileDescriptor() { close(); }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const { return fd_; }
    [[nodiscard]] bool valid() const { return fd_ >= 0; }

    void reset(int new_fd = -1) {
        close();
        fd_ = new_fd;
    }

    int release() {
        int tmp = fd_;
        fd_ = -1;
        return tmp;
    }

private:
    void close();
    int fd_ = -1;
};

class MXMaster4 {
public:
    MXMaster4(const MXMaster4&) = delete;
    MXMaster4& operator=(const MXMaster4&) = delete;
    MXMaster4(MXMaster4&&) noexcept = default;
    MXMaster4& operator=(MXMaster4&&) noexcept = default;
    ~MXMaster4();

    [[nodiscard]] static std::optional<MXMaster4> find(
        std::optional<ConnectionType> connection_type = std::nullopt,
        std::optional<std::filesystem::path> device_path = std::nullopt
    );

    void open();
    void close();

    void send_haptic_feedback(int effect_id = 1);

    [[nodiscard]] ConnectionType connection_type() const { return connection_type_; }
    [[nodiscard]] bool is_open() const;

private:
    MXMaster4(ConnectionType type, std::filesystem::path path, std::optional<int> device_idx);

    static std::optional<MXMaster4> find_bolt_device();
    static std::optional<std::filesystem::path> find_bluetooth_path();

    void write_bluetooth(std::span<const uint8_t> data);
    void send_bolt_hidpp(FunctionID feature_idx, std::span<const uint8_t> args);

    ConnectionType connection_type_;
    std::filesystem::path device_path_;
    std::optional<int> device_idx_;

    std::variant<std::monostate, HidDevicePtr, FileDescriptor> device_;
};

}
