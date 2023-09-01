// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <sysexits.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <libtouchraw/touchraw_interface.h>

namespace touchraw {

constexpr int kByte = 8;  // Number of bits per byte.
// Constant to signal that we need to continue running the daemon after
// initialization.
constexpr int kContinueRunning = -1;

// Binary flags mask.
// Bit  |   4  |    3   |   2   |   1     |   0     |
// Flag | skip | filter | full  | decode  | binary  |
constexpr int kBinaryMask = 0x01;
constexpr int kDecodeMask = 0x02;
constexpr int kFullMask = 0x04;
constexpr int kFilterMask = 0x08;
constexpr int kSkipMask = 0x10;

// According to the escape word design defined here go/cros-heatmap-external
// v0.5:
// Byte | 3       | 2 | 1 | 0   |
//      | escape  | repetition  |
constexpr int kEscapeMask = 0x8000;
constexpr int kRepititionMask = 0x0FFF;

constexpr int kHIDRawNameLength = 256;
constexpr char kHidrawDir[] = "/dev/";
constexpr char kHidrawPrefix[] = "/dev/hidraw";

class HeatmapConsumer : public HeatmapConsumerInterface {
 public:
  /**
   * HeatmapConsumer constructor.
   * This class consumes defragmented heatmap frames and dumps them on the
   * console.
   *
   * @param path Input device file path.
   * @param flags Command-line flags.
   * @param threshold Threshold value used to filter heatmap data if enabled.
   */
  HeatmapConsumer(const std::string path,
                  const uint16_t flags,
                  const int threshold)
      : path_(path), flags_(flags), threshold_(threshold) {}

  ~HeatmapConsumer() override = default;
  HeatmapConsumer(const HeatmapConsumer&) = delete;
  HeatmapConsumer& operator=(const HeatmapConsumer&) = delete;

  void Push(std::unique_ptr<const Heatmap> hm) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&HeatmapConsumer::Dump,
                                  base::Unretained(this), std::move(hm)));
  }

 protected:
  /**
   * Dump defragmented heatmap.
   *
   * @param hm Heatmap data.
   */
  void Dump(std::unique_ptr<const Heatmap> hm) {
    static timespec time;
    clock_gettime(CLOCK_REALTIME, &time);

    // Dump HID packet and/or payload header.
    if ((flags_ & kBinaryMask) == 0) {
      std::cout << "Event time: " << std::dec << time.tv_sec << "."
                << std::setfill('0') << std::setw(9) << time.tv_nsec
                << std::endl;
      std::cout << "Heatmap protocol vendor id: 0x" << std::hex << hm->vendor_id
                << std::endl;
      std::cout << "Heatmap protocol version: 0x" << hm->protocol_version
                << std::endl;
      std::cout << "Timestamp: " << std::dec << hm->scan_time << "(0x"
                << std::hex << hm->scan_time << ")" << std::endl;

      std::cout << "Payload Protocol Version: " << std::dec << hm->encoding
                << std::endl;
      std::cout << "Payload bit depth: " << static_cast<uint16_t>(hm->bit_depth)
                << std::endl;
      std::cout << "Payload height: " << static_cast<uint16_t>(hm->height)
                << std::endl;
      std::cout << "Payload width: " << static_cast<uint16_t>(hm->width)
                << std::endl;
      std::cout << "Payload threshold value: " << hm->threshold << "(0x"
                << std::hex << hm->threshold << ")" << std::endl;
      std::cout << "Payload length: " << std::dec << hm->length << std::endl;
    } else {
      std::cout << "0x" << std::hex << static_cast<uint16_t>(hm->bit_depth)
                << ",";
      std::cout << "0x" << std::hex << static_cast<uint16_t>(hm->height) << ",";
      std::cout << "0x" << std::hex << static_cast<uint16_t>(hm->width) << ",";
      std::cout << "0x" << std::hex << hm->threshold << "," << std::endl;
    }

    DumpPayload(std::move(hm));
  }

 private:
  void DumpPayload(std::unique_ptr<const Heatmap> hm) {
    int cur = 0;  // Index of heatmap payload.
    int pos = 0;  // Track the size of decoded payload in words.
    uint32_t data = 0;
    uint32_t temp = 0;
    int word_size =
        std::ceil(hm->bit_depth / kByte *
                  1.0);  // The number of bytes for each word (heatmap cell).
    int hex_width = word_size * 2;  // The width of a word in hex format.
    bool full = (flags_ & kBinaryMask) ? true : flags_ & kFullMask;

    // Assume word size should not go beyond the size of data, which is 4 bytes.
    if (word_size > sizeof(data)) {
      LOG(WARNING) << "Not supported - heat map word size is " << word_size;
      return;
    }

    // Only RLE is supported for decoding now.
    if ((hm->encoding == EncodingType::kRLE) &&
        ((flags_ & kBinaryMask) ||
         (flags_ & kDecodeMask))) {  // RLE and decode.
      while (cur < hm->payload.size()) {
        temp = 0;
        for (int i = 0; i < word_size; ++i) {
          temp |= (hm->payload[cur++] << (kByte * i));
        }
        if (temp & kEscapeMask) {
          for (int i = 0; i < (temp & kRepititionMask); ++i) {
            ProcessWord(hm->height, hm->width, data, hex_width, pos++,
                        hm->bit_depth, full);
          }
        } else {
          data = temp;
          ProcessWord(hm->height, hm->width, data, hex_width, pos++,
                      hm->bit_depth, full);
        }
      }
    } else {  // Raw data or encoded data without decode option or unsupported
              // encoding protocol.
      while (cur < hm->payload.size()) {
        data = 0;
        for (int i = 0; i < word_size; ++i) {
          data |= (hm->payload[cur++] << (kByte * i));
        }
        ProcessWord(hm->height, hm->width, data, hex_width, pos++,
                    hm->bit_depth, full);
      }
    }

    // Validate decoded heatmap data size if received raw data OR encoded with
    // RLE and decode option is enabled.
    if ((hm->encoding < EncodingType::kRLE) ||
        ((hm->encoding == EncodingType::kRLE) &&
         ((flags_ & kBinaryMask) || (flags_ & kDecodeMask)))) {
      if (pos != (hm->height * hm->width)) {
        LOG(ERROR) << "Incorrect heatmap data size: " << pos
                   << " words. Expected " << (hm->height * hm->width)
                   << " words.";
        return;
      }
    }

    std::cout << std::endl;
  }

  void ProcessWord(int rows,
                   int cols,
                   int data,
                   int hex_width,
                   int pos,
                   int bit_depth,
                   bool full) {
    int cur_row = pos / cols;
    int cur_col = pos % cols;
    static bool skip = true;

    // Filter out values under a threshold.
    if (flags_ & kFilterMask) {
      if (data < threshold_ || data > ((1 << bit_depth) - threshold_)) {
        data = 0;
      }
    }

    // Skip frames that are all zeros.
    if (flags_ & kSkipMask) {
      if (pos == 0) {
        skip = true;
      }
      if (skip) {
        if (data == 0) {
          return;
        } else {
          skip = false;
          for (int i = 0; i < pos; ++i) {
            DumpWord(i / cols, i % cols, rows, cols, hex_width, 0);
          }
        }
      }
      DumpWord(cur_row, cur_col, rows, cols, hex_width, data);
      return;
    }

    // Full frame is not enabled;
    // By default only dump first 5 rows and last 5 rows.
    if (!full) {
      if (cur_row >= 5 && cur_row < (rows - 5)) {
        if (cur_row == 5 && cur_col == 0) {
          std::cout << "......" << std::endl;
        }
        return;
      }
    }
    DumpWord(cur_row, cur_col, rows, cols, hex_width, data);
  }

  void DumpWord(
      int cur_row, int cur_col, int rows, int cols, int hex_width, int data) {
    if (cur_row < rows && cur_col < cols) {
      std::cout << "0x" << std::hex << std::setfill('0') << std::setw(hex_width)
                << data << ",";
      if (cur_col == (cols - 1)) {
        std::cout << std::endl;
      }
    } else {
      LOG(ERROR) << "Data out of range.";
      return;
    }
  }

  const std::string path_;
  const uint16_t flags_;
  const int threshold_;
};

class HeatmapRecorder : public brillo::Daemon {
 public:
  /**
   * HeatmapRecorder constructor.
   *
   * @param argc The number of command-line arguments passed by the user
   * including the name of the program.
   * @param argv An array of character pointers listing all the arguments.
   */
  HeatmapRecorder(int argc, char** argv)
      : argc_(argc),
        argv_(argv),
        flags_(0),
        threshold_(0),
        weak_ptr_factory_(this) {}

  ~HeatmapRecorder() override = default;
  HeatmapRecorder(const HeatmapRecorder&) = delete;
  HeatmapRecorder& operator=(const HeatmapRecorder&) = delete;

 protected:
  int OnInit() override {
    int ret;

    if (ret = Daemon::OnInit(); ret != EX_OK)
      return ret;

    ret = ProcessFlags();
    if (ret != kContinueRunning)
      Exit(ret);

    return EX_OK;
  }

 private:
  int DumpDeviceInfo() {
    hidraw_devinfo info;
    int res = EX_OK;

    int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      LOG(ERROR) << "Failed to open device " << path_;
      return EX_NOINPUT;
    }

    /* Get Raw Info */
    if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
      LOG(ERROR) << "Failed to get HID raw info.";
      res = EX_IOERR;
    } else {
      std::cout << "Vendor Id: 0x" << std::hex << info.vendor << std::endl;
      std::cout << "Product Id: 0x" << std::hex << info.product << std::endl;
      std::cout << std::endl;
    }

    close(fd);
    return res;
  }

  void MonitorDevice(std::unique_ptr<HeatmapConsumer> consumer) {
    if (!consumer) {
      LOG(ERROR) << "Failed to create a HeatmapConsumer object";
      Exit(EX_UNAVAILABLE);
    }

    static std::unique_ptr<touchraw::TouchrawInterface> interface =
        touchraw::TouchrawInterface::Create(base::FilePath(path_),
                                            std::move(consumer));
    if (!interface) {
      LOG(ERROR) << "Failed to create TouchrawInterface object";
      Exit(EX_UNAVAILABLE);
    }

    if (!interface->StartWatching().ok()) {
      LOG(ERROR) << "Failed to watch the device";
      Exit(EX_UNAVAILABLE);
    }
  }

  void AddDevice() {
    auto consumer =
        std::make_unique<HeatmapConsumer>(path_, flags_, threshold_);

    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&HeatmapRecorder::MonitorDevice,
                       weak_ptr_factory_.GetWeakPtr(), std::move(consumer)));
  }

  std::string GetDeviceName(std::string path) {
    char buf[kHIDRawNameLength] = {};

    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      LOG(ERROR) << "Failed to open device " << path;
      return buf;
    }

    /* Get Raw Name */
    if (ioctl(fd, HIDIOCGRAWNAME(kHIDRawNameLength), buf) < 0)
      LOG(ERROR) << "Failed to get HID raw name.";

    close(fd);
    return buf;
  }

  // Helper function to list available hidraw devices and return the user chosen
  // device path.
  std::string ListDevices() {
    std::map<int, std::string> devices;
    std::string name;
    std::string num;
    int key;
    int low;
    int high;

    for (const auto& entry : std::filesystem::directory_iterator(kHidrawDir)) {
      if (!entry.path().string().starts_with(kHidrawPrefix))
        continue;
      if ((name = GetDeviceName(entry.path())).empty())
        continue;
      if (!base::StringToInt(
              entry.path().string().substr(sizeof(kHidrawPrefix) - 1), &key)) {
        LOG(ERROR) << entry.path() << " does not end with a number.";
        continue;
      }
      devices.emplace(key, name);
    }

    if (devices.empty()) {
      std::cerr << "No devices found" << std::endl;
      return "";
    }

    low = devices.begin()->first;
    high = devices.rbegin()->first;

    std::cout << "Available devices:" << std::endl;
    for (const auto& device : devices)
      std::cout << kHidrawPrefix << device.first << "   " << device.second
                << std::endl;

    std::cout << "Select the device event number [" << low << "-" << high
              << "]: ";
    std::cin >> num;

    return kHidrawPrefix + num;
  }

  // Main method that parses and triggers all the actions based on the passed
  // flags. Returns the exit code of the program or kContinueRunning if it
  // should not exit.
  int ProcessFlags() {
    int res = 0;

    DEFINE_string(path, "", "Path to the hidraw device node.");
    DEFINE_bool(binary, false,
                "Binary format - dump full frame decoded heatmap data.");
    DEFINE_bool(decode, false, "Decode heatmap data.");
    DEFINE_bool(full, false, "Dump full frame of heatmap data.");
    DEFINE_int32(filter, -1, "Filter out values within a threshold.");
    DEFINE_bool(skip, false, "Skip dumping frames that are all zeros.");
    DEFINE_int32(log_level, 1,
                 "Log level - 0: LOG(INFO), 1: LOG(WARNING), 2: LOG(ERROR), "
                 "-1: VLOG(1), -2: VLOG(2), ...");

    brillo::FlagHelper::Init(argc_, argv_, "heatmap-recorder");

    if (FLAGS_path.empty()) {
      path_ = ListDevices();
      if (path_.empty()) {
        std::cout << "Device path is empty." << std::endl;
        return EX_USAGE;
      }
    } else {
      path_ = FLAGS_path;
    }
    std::cout << "Device path: " << path_ << std::endl;

    logging::SetMinLogLevel(FLAGS_log_level);
    std::cout << "Log level is " << FLAGS_log_level << std::endl;

    if (FLAGS_binary) {
      flags_ |= kBinaryMask;
      std::cout << "Binary flag is set." << std::endl;
    } else {
      if ((res = DumpDeviceInfo()) != EX_OK) {
        return res;
      }
    }

    if (FLAGS_decode) {
      flags_ |= kDecodeMask;
      std::cout << "Decode flag is set." << std::endl;
    }

    if (FLAGS_full) {
      flags_ |= kFullMask;
      std::cout << "Full flag is set." << std::endl;
    }

    if (FLAGS_filter != -1) {
      flags_ |= kFilterMask;
      threshold_ = FLAGS_filter;
      std::cout << "Filter flag is set. Threshold is " << std::dec << threshold_
                << std::endl;
    }

    if (FLAGS_skip) {
      flags_ |= kSkipMask;
      std::cout << "Skip flag is set." << std::endl;
    }

    AddDevice();

    return kContinueRunning;
  }

  void Exit(int ret) { QuitWithExitCode(ret); }

  // Copy of argc and argv passed to main().
  int argc_;
  char** argv_;

  uint16_t flags_;
  int threshold_;
  std::string path_;

  base::WeakPtrFactory<HeatmapRecorder> weak_ptr_factory_;
};

}  // namespace touchraw

int main(int argc, char* argv[]) {
  touchraw::HeatmapRecorder hr(argc, argv);
  return hr.Run();
}
