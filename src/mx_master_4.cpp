#include "mx_master_4.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <unistd.h>

#include <libudev.h>

namespace mx4hyprland {

namespace {

constexpr size_t HID_LONG_REPORT_SIZE = 20;
constexpr uint16_t HIDPP_USAGE_PAGE = 65280;

class UdevDeleter {
public:
    void operator()(udev* u) const {
        if (u != nullptr) {
            udev_unref(u);
        }
    }
};

class UdevEnumerateDeleter {
public:
    void operator()(udev_enumerate* e) const {
        if (e != nullptr) {
            udev_enumerate_unref(e);
        }
    }
};

class UdevDeviceDeleter {
public:
    void operator()(udev_device* d) const {
        if (d != nullptr) {
            udev_device_unref(d);
        }
    }
};

using UdevPtr = std::unique_ptr<udev, UdevDeleter>;
using UdevEnumeratePtr = std::unique_ptr<udev_enumerate, UdevEnumerateDeleter>;
using UdevDevicePtr = std::unique_ptr<udev_device, UdevDeviceDeleter>;

}

void FileDescriptor::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

MXMaster4::MXMaster4(
    ConnectionType type,
    std::filesystem::path path,
    std::optional<int> device_idx
)
    : connection_type_(type)
    , device_path_(std::move(path))
    , device_idx_(device_idx)
{}

MXMaster4::~MXMaster4() {
    close();
}

std::optional<MXMaster4> MXMaster4::find(
    std::optional<ConnectionType> connection_type,
    std::optional<std::filesystem::path> device_path
) {
    if (!connection_type || *connection_type == ConnectionType::Bolt) {
        if (auto bolt = find_bolt_device()) {
            return bolt;
        }
    }

    if (!connection_type || *connection_type == ConnectionType::Bluetooth) {
        auto path = device_path.value_or(find_bluetooth_path().value_or(""));
        if (!path.empty() && std::filesystem::exists(path)) {
            return MXMaster4(ConnectionType::Bluetooth, path, std::nullopt);
        }
    }

    return std::nullopt;
}

std::optional<MXMaster4> MXMaster4::find_bolt_device() {
    hid_device_info* devs = hid_enumerate(LOGITECH_VID, 0);
    if (devs == nullptr) {
        return std::nullopt;
    }

    std::optional<MXMaster4> result;
    for (hid_device_info* cur = devs; cur != nullptr; cur = cur->next) {
        if (cur->usage_page == HIDPP_USAGE_PAGE) {
            std::filesystem::path path(cur->path);
            logger().debug("Found Bolt device");
            result = MXMaster4(ConnectionType::Bolt, path, cur->interface_number);
            break;
        }
    }

    hid_free_enumeration(devs);
    return result;
}

std::optional<std::filesystem::path> MXMaster4::find_bluetooth_path() {
    UdevPtr udev_ctx(udev_new());
    if (!udev_ctx) {
        logger().error("Failed to create udev context");
        return std::nullopt;
    }

    UdevEnumeratePtr enumerate(udev_enumerate_new(udev_ctx.get()));
    if (!enumerate) {
        logger().error("Failed to create udev enumerate");
        return std::nullopt;
    }

    udev_enumerate_add_match_subsystem(enumerate.get(), "hidraw");
    udev_enumerate_scan_devices(enumerate.get());

    std::ostringstream vid_ss;
    vid_ss << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << LOGITECH_VID;
    std::string vid_hex = vid_ss.str();

    std::ostringstream pid_ss;
    pid_ss << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << MX_MASTER_4_BLUETOOTH_PID;
    std::string pid_hex = pid_ss.str();

    std::string target_modalias_part = "0005:0000" + vid_hex + ":0000" + pid_hex;
    std::transform(target_modalias_part.begin(), target_modalias_part.end(),
                   target_modalias_part.begin(), ::toupper);

    logger().debug("Looking for Bluetooth device with modalias containing: ", target_modalias_part);

    udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate.get());
    udev_list_entry* entry = nullptr;

    udev_list_entry_foreach(entry, devices) {
        const char* syspath = udev_list_entry_get_name(entry);
        UdevDevicePtr hidraw_dev(udev_device_new_from_syspath(udev_ctx.get(), syspath));

        if (!hidraw_dev) {
            continue;
        }

        const char* devnode = udev_device_get_devnode(hidraw_dev.get());
        logger().debug("Checking hidraw device: ", syspath, " -> ", devnode ? devnode : "no devnode");

        udev_device* hid_dev = udev_device_get_parent_with_subsystem_devtype(
            hidraw_dev.get(), "hid", nullptr
        );

        if (hid_dev == nullptr) {
            logger().debug("  No HID parent found");
            continue;
        }

        const char* hid_name = udev_device_get_sysattr_value(hid_dev, "name");
        const char* hid_name_prop = udev_device_get_property_value(hid_dev, "HID_NAME");

        std::string name;
        if (hid_name != nullptr) {
            name = hid_name;
        } else if (hid_name_prop != nullptr) {
            name = hid_name_prop;
        }

        if (name.empty()) {
            const char* hid_id = udev_device_get_property_value(hid_dev, "HID_ID");
            if (hid_id != nullptr) {
                std::string hid_id_upper(hid_id);
                std::transform(hid_id_upper.begin(), hid_id_upper.end(),
                               hid_id_upper.begin(), ::toupper);
                if (hid_id_upper.find(vid_hex) != std::string::npos &&
                    hid_id_upper.find(pid_hex) != std::string::npos) {
                    logger().debug("  No name but HID_ID matches VID/PID, using as candidate");
                    name = "MX Master 4 (detected by ID)";
                }
            }
        }

        if (name.empty()) {
            logger().debug("  HID parent has no name");
            continue;
        }

        logger().debug("  HID name: ", name);

        if (name.find(MX_MASTER_4_BLUETOOTH_NAME) == std::string::npos &&
            name.find("detected by ID") == std::string::npos) {
            continue;
        }

        logger().debug("  Name matches! Checking IDs...");

        const char* hid_id = udev_device_get_property_value(hid_dev, "HID_ID");
        const char* modalias = udev_device_get_property_value(hid_dev, "MODALIAS");

        logger().debug("  HID_ID: ", hid_id ? hid_id : "(null)");
        logger().debug("  MODALIAS: ", modalias ? modalias : "(null)");

        if (hid_id != nullptr) {
            std::string hid_id_str(hid_id);
            std::transform(hid_id_str.begin(), hid_id_str.end(),
                           hid_id_str.begin(), ::toupper);

            std::string target_padded = "0005:0000" + vid_hex + ":0000" + pid_hex;
            std::string target_unpadded = "0005:" + vid_hex + ":" + pid_hex;

            logger().debug("  Looking for: ", target_padded, " or ", target_unpadded);
            logger().debug("  Got: ", hid_id_str);

            if (hid_id_str.find(target_padded) != std::string::npos ||
                hid_id_str.find(target_unpadded) != std::string::npos) {
                if (devnode != nullptr) {
                    logger().info("Found Bluetooth device: ", name, " at ", devnode);
                    return std::filesystem::path(devnode);
                }
            }
        }

        if (modalias != nullptr) {
            std::string modalias_str(modalias);
            std::transform(modalias_str.begin(), modalias_str.end(),
                           modalias_str.begin(), ::toupper);

            if (modalias_str.find(target_modalias_part) != std::string::npos) {
                if (devnode != nullptr) {
                    logger().info("Found Bluetooth device: ", name, " at ", devnode);
                    return std::filesystem::path(devnode);
                }
            }
        }

        logger().debug("  ID mismatch, skipping");
    }

    logger().debug("Bluetooth device not found via udev");
    return std::nullopt;
}

void MXMaster4::open() {
    if (is_open()) {
        return;
    }

    if (connection_type_ == ConnectionType::Bolt) {
        hid_device* raw_dev = hid_open_path(device_path_.c_str());
        if (raw_dev == nullptr) {
            throw DeviceDisconnectedError("Failed to open Bolt device");
        }
        device_ = HidDevicePtr(raw_dev);
        logger().info("Connected via Bolt");
    } else {
        int fd = ::open(device_path_.c_str(), O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            throw DeviceDisconnectedError(
                "Failed to open Bluetooth device: " + device_path_.string() +
                " (" + std::strerror(errno) + ")"
            );
        }
        device_ = FileDescriptor(fd);
        logger().info("Connected via Bluetooth");
    }
}

void MXMaster4::close() {
    device_ = std::monostate{};
}

bool MXMaster4::is_open() const {
    return std::visit(
        [](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return false;
            } else if constexpr (std::is_same_v<T, HidDevicePtr>) {
                return arg != nullptr;
            } else {
                return arg.valid();
            }
        },
        device_
    );
}

void MXMaster4::write_bluetooth(std::span<const uint8_t> data) {
    auto* fd_ptr = std::get_if<FileDescriptor>(&device_);
    if (fd_ptr == nullptr || !fd_ptr->valid()) {
        throw DeviceDisconnectedError("Bluetooth device is not open");
    }

    ssize_t written = ::write(fd_ptr->get(), data.data(), data.size());

    if (written < 0) {
        if (errno == ENODEV || errno == EIO) {
            throw DeviceDisconnectedError("Device disconnected");
        }
        throw DeviceDisconnectedError(
            "Failed to write to Bluetooth device: " + std::string(std::strerror(errno))
        );
    }

    if (static_cast<size_t>(written) != data.size()) {
        throw DeviceDisconnectedError("Incomplete write to Bluetooth device");
    }
}

void MXMaster4::send_bolt_hidpp(FunctionID feature_idx, std::span<const uint8_t> args) {
    auto* hid_ptr = std::get_if<HidDevicePtr>(&device_);
    if (hid_ptr == nullptr || !(*hid_ptr)) {
        throw DeviceDisconnectedError("Bolt device is not open");
    }

    std::array<uint8_t, HID_LONG_REPORT_SIZE> packet{};
    packet[0] = static_cast<uint8_t>(args.size() <= 3 ? ReportID::Short : ReportID::Long);
    packet[1] = static_cast<uint8_t>(device_idx_.value_or(0));
    packet[2] = static_cast<uint8_t>((static_cast<uint16_t>(feature_idx) >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(static_cast<uint16_t>(feature_idx) & 0xFF);

    std::copy(args.begin(), args.end(), packet.begin() + 4);

    int res = hid_write(hid_ptr->get(), packet.data(), packet.size());
    if (res < 0) {
        throw DeviceDisconnectedError("HID write failed");
    }

    std::array<uint8_t, HID_LONG_REPORT_SIZE> response{};
    res = hid_read_timeout(hid_ptr->get(), response.data(), response.size(), 100);
    if (res < 0) {
        throw DeviceDisconnectedError("HID read failed");
    }
}

void MXMaster4::send_haptic_feedback(int effect_id) {
    if (effect_id < EFFECT_MIN || effect_id > EFFECT_MAX) {
        throw std::invalid_argument("effect_id must be between 0 and 15");
    }

    if (!is_open()) {
        open();
    }

    if (connection_type_ == ConnectionType::Bolt) {
        std::array<uint8_t, 1> args = {static_cast<uint8_t>(effect_id)};
        send_bolt_hidpp(FunctionID::Haptic, args);
    } else {
        std::array<uint8_t, HID_LONG_REPORT_SIZE> packet{};
        packet[0] = static_cast<uint8_t>(ReportID::Long);
        packet[1] = 0xFF;
        packet[2] = 0x0B;
        packet[3] = 0x4E;
        packet[4] = static_cast<uint8_t>(effect_id);

        write_bluetooth(packet);
    }
}

}
