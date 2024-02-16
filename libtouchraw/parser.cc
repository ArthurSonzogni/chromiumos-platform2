// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtouchraw/parser.h"

#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <base/files/file.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>

namespace touchraw {

constexpr int kByte = 0x08;           // Number of bits per byte.
constexpr int kShortItemSize = 0x04;  // Maximum data size for short items.

constexpr int kHidDigitizersPage = 0x0d;
constexpr int kHidDGHeatMapProtocolVendorID = 0x6a;
constexpr int kHidDGHeatMapProtocolVersion = 0x6b;
constexpr int kHidDGScanTime = 0x56;
constexpr int kHidDGHeatMapFrameData = 0x6c;

constexpr int kHidGenericDesktopPage = 0x01;
constexpr int kHidGDByteCount = 0x3b;

constexpr int kHidGenericDeviceControlsPage = 0x06;
constexpr int kHidGDCSequenceID = 0x27;

constexpr int kHIDLongItemPrefix = 0xfe;
constexpr int kHIDItemPrefixMask = 0xfc;
constexpr int kHIDGlobalItemTagUsagePage = 0x04;
constexpr int kHIDLocalItemTagUsage = 0x08;
constexpr int kHIDMainItemTagInput = 0x80;
constexpr int kHIDGlobalItemTagReportID = 0x84;
constexpr int kHIDGlobalItemTagReportSize = 0x74;
constexpr int kHIDGlobalItemTagReportCount = 0x94;

ReportDescriptor::ReportDescriptor(const hidraw_report_descriptor* rpt_desc)
    : rpt_desc_(rpt_desc), next_item_idx_(0) {}

bool ReportDescriptor::HasNextItem() {
  return next_item_idx_ < rpt_desc_->size;
}

Item ReportDescriptor::GetNextItem() {
  Item item;
  int cur_item_idx = next_item_idx_;
  int data_offset;  // Data starting index of an item.

  // Process item prefix.
  if (cur_item_idx < rpt_desc_->size) {
    item.prefix = rpt_desc_->value[cur_item_idx];
    if (rpt_desc_->value[cur_item_idx] == kHIDLongItemPrefix) {  // Long item.
      data_offset = 3;
      item.data_size = rpt_desc_->value[cur_item_idx + 1];
    } else {  // Short item.
      data_offset = 1;
      item.data_size =
          ((item.prefix & 0x03) == 0x03) ? 4 : (item.prefix & 0x03);
    }
    item.size = item.data_size + data_offset;
    next_item_idx_ += item.size;  // Update the index to next item.
  }

  // Copy item data.
  if (next_item_idx_ <= rpt_desc_->size) {
    item.data =
        std::span<const uint8_t>(rpt_desc_->value + cur_item_idx + data_offset,
                                 rpt_desc_->value + next_item_idx_);
  }

  return item;
}

void ReportDescriptor::Reset() {
  next_item_idx_ = 0;
}

std::unique_ptr<Parser> Parser::Create(
    const int fd, std::unique_ptr<HeatmapChunkConsumerInterface> q) {
  // Using `new` to access a non-public constructor.
  std::unique_ptr<Parser> parser = base::WrapUnique(new Parser(std::move(q)));
  hidraw_report_descriptor rpt_desc;

  if (parser->GetReportDescriptorSysfs(fd, &rpt_desc) != 0) {
    if (parser->GetReportDescriptorIoctl(fd, &rpt_desc) != 0) {
      return nullptr;
    }
  }
  if (!parser->ParseHeatmapReportsFromDescriptor(&rpt_desc)) {
    LOG(WARNING) << "The report descriptor does not support heatmap";
    return nullptr;
  }
  return parser;
}

std::unique_ptr<Parser> Parser::CreateForTesting(
    std::unique_ptr<HeatmapChunkConsumerInterface> q) {
  return base::WrapUnique(new Parser(std::move(q)));
}

Parser::Parser(std::unique_ptr<HeatmapChunkConsumerInterface> q)
    : q_(std::move(q)) {}

int Parser::GetReportDescriptorIoctl(const int fd,
                                     hidraw_report_descriptor* rpt_desc) {
  int res, desc_size = 0;

  LOG(INFO) << "Get report descriptor from hidraw.";

  // Get Report Descriptor Size.
  res = ioctl(fd, HIDIOCGRDESCSIZE, &desc_size);
  if (res < 0) {
    LOG(WARNING) << "Failed to get report descriptor size: " << strerror(errno);
    return res;
  }

  // Get Report Descriptor.
  rpt_desc->size = desc_size;
  res = ioctl(fd, HIDIOCGRDESC, rpt_desc);
  if (res < 0) {
    LOG(WARNING) << "Failed to get report descriptor: " << strerror(errno);
  }
  return res;
}

std::unique_ptr<brillo::UdevDevice> Parser::CreateUdevDevice(const int fd) {
  base::stat_wrapper_t stat_buf;
  dev_t devnum;

  // Get the dev_t (major/minor numbers) from the file handle.
  if (base::File::Fstat(fd, &stat_buf) == -1) {
    LOG(WARNING) << "Failed to stat device handle " << fd << ": "
                 << strerror(errno);
    return nullptr;
  }
  devnum = stat_buf.st_rdev;

  // Create a udev device.
  std::unique_ptr<brillo::Udev> udev = brillo::Udev::Create();
  auto dev = udev->CreateDeviceFromDeviceNumber('c', devnum);
  if (!dev) {
    LOG(WARNING) << "Could not get udev entry for device with MAJOR: "
                 << major(devnum) << " MINOR: " << minor(devnum);
    return nullptr;
  }
  return dev;
}

int Parser::GetReportDescriptorSysfs(const int fd,
                                     hidraw_report_descriptor* rpt_desc) {
  int res;
  std::string rpt_path;

  auto dev = CreateUdevDevice(fd);
  if (!dev) {
    LOG(WARNING) << "Failed to create a udev device";
    return -1;
  }

  // Construct <sysfs_path>/device/report_descriptor.
  rpt_path = std::string(dev->GetSysPath()) + "/device/report_descriptor";
  base::File file(base::FilePath(rpt_path),
                  base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid()) {
    LOG(WARNING) << "Could not open " << rpt_path;
    return -1;
  }

  memset(rpt_desc, 0x0, sizeof(*rpt_desc));
  res = file.Read(0, reinterpret_cast<char*>(rpt_desc->value),
                  sizeof(rpt_desc->value));
  if (res < 0) {
    LOG(WARNING) << "Error reading " << rpt_path;
  }
  rpt_desc->size = static_cast<uint32_t>(res);

  return res;
}

uint32_t Parser::GetPropValue(std::span<const uint8_t> data, int data_size) {
  // Does not support long items for now.
  if (data_size > kShortItemSize) {
    LOG(WARNING) << "Not supported - data size is " << data_size;
    return std::numeric_limits<uint32_t>::max();
  }

  uint32_t value = 0;
  for (int i = 0; i < data_size; ++i) {
    value |= data[i] << (i * kByte);
  }

  return value;
}

void Parser::ProcessItem(Item& item) {
  switch (item.prefix & kHIDItemPrefixMask) {
    case kHIDGlobalItemTagUsagePage:
      info_.usage_page = GetPropValue(item.data, item.data_size);
      break;
    case kHIDLocalItemTagUsage:
      info_.usage = GetPropValue(item.data, item.data_size);
      break;
    case kHIDMainItemTagInput:
      if (info_.usage_page == kHidDigitizersPage &&
          info_.usage == kHidDGHeatMapProtocolVendorID) {
        if (!sync_report_offset_.has_value()) {
          sync_report_offset_ = usages_.size();
        } else {
          sub_report_offset_ = usages_.size();
        }
      }
      usages_.push_back({info_.usage_page, info_.usage, info_.report_id,
                         static_cast<uint16_t>(info_.report_size *
                                               info_.report_count / kByte)});
      break;
    case kHIDGlobalItemTagReportID:
      info_.report_id = item.data[0];
      break;
    case kHIDGlobalItemTagReportSize:
      info_.report_size = GetPropValue(item.data, item.data_size);
      break;
    case kHIDGlobalItemTagReportCount:
      info_.report_count = GetPropValue(item.data, item.data_size);
      break;
    default:
      break;
  }
}

bool Parser::ParseHeatmapReportsFromDescriptor(
    const hidraw_report_descriptor* rpt_desc) {
  ReportDescriptor descriptor(rpt_desc);
  LOG(INFO) << "Parse report descriptor.";

  // Parse report descriptor.
  // TODO: b/320780085 - Validate report descriptor collection that represents
  // the heatmap data.
  while (descriptor.HasNextItem()) {
    Item item = descriptor.GetNextItem();
    ProcessItem(item);
  }
  return sync_report_offset_.has_value();
}

void Parser::ParseHIDData(std::unique_ptr<const HIDData> hid_data) {
  int cur = 0;
  uint16_t offset = 0;
  auto chunk = std::make_unique<HeatmapChunk>();

  // Discards HIDData for unsupported report_ids.
  if (sync_report_offset_ &&
      hid_data->report_id == usages_[sync_report_offset_.value()].report_id) {
    offset = sync_report_offset_.value();
  } else if (sub_report_offset_ &&
             hid_data->report_id ==
                 usages_[sub_report_offset_.value()].report_id) {
    offset = sub_report_offset_.value();
  } else {
    LOG(INFO) << "Report id " << static_cast<int>(hid_data->report_id)
              << ": Not heat map data.";
    return;
  }

  chunk->report_type = ReportType::kInvalid;

  for (int i = offset; usages_[i].report_id == hid_data->report_id; ++i) {
    switch (usages_[i].usage_page) {
      case kHidDigitizersPage:
        switch (usages_[i].usage) {
          case kHidDGHeatMapProtocolVendorID:
            chunk->vendor_id =
                GetDataField(cur, usages_[i].data_size, hid_data->payload);
            break;
          case kHidDGHeatMapProtocolVersion:
            chunk->protocol_version =
                GetDataField(cur, usages_[i].data_size, hid_data->payload);
            break;
          case kHidDGScanTime:
            chunk->scan_time =
                GetDataField(cur, usages_[i].data_size, hid_data->payload);
            break;
          case kHidDGHeatMapFrameData:
            // TODO: b/320780085 - Validate report descriptor collection that
            // represents the heatmap data
            chunk->payload.assign(hid_data->payload.begin() + cur,
                                  hid_data->payload.end());
            if (chunk->payload.size() != usages_[i].data_size) {
              LOG(WARNING) << "Discard this chunk because chunk size "
                           << chunk->payload.size()
                           << " is not equal to the expected size "
                           << usages_[i].data_size;
              return;
            }
            break;
          default:
            break;
        }
        break;
      case kHidGenericDesktopPage:
        switch (usages_[i].usage) {
          case kHidGDByteCount:
            chunk->byte_count =
                GetDataField(cur, usages_[i].data_size, hid_data->payload);
            chunk->report_type = ReportType::kFirst;
            break;
          default:
            break;
        }
        break;
      case kHidGenericDeviceControlsPage:
        switch (usages_[i].usage) {
          case kHidGDCSequenceID:
            chunk->sequence_id =
                GetDataField(cur, usages_[i].data_size, hid_data->payload);
            chunk->report_type = ReportType::kSubsequent;
            break;
          default:
            break;
        }
        break;
      default:
        break;
    }
    cur += usages_[i].data_size;
  }

  // Dispatch.
  q_->Push(std::move(chunk));
}

// Support short items only - data size is limited to 4 bytes.
uint32_t Parser::GetDataField(int index,
                              int size,
                              std::span<const uint8_t> payload) {
  if ((index + size) >= payload.size()) {
    LOG(WARNING) << "Data out of range.";
    return std::numeric_limits<uint32_t>::max();
  }
  // Does not support long items for now.
  if (size > kShortItemSize) {
    LOG(WARNING) << "Not supported - data size is " << size;
    return std::numeric_limits<uint32_t>::max();
  }

  uint32_t value = 0;
  for (int i = 0; i < size; ++i) {
    value |= payload[index + i] << (kByte * i);
  }
  return value;
}

}  // namespace touchraw
