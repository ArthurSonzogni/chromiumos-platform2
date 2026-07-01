// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/hidraw_subsystem_udev_rule.h"

#include <fcntl.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <string>

#include "base/logging.h"

using std::string;

namespace permission_broker {

namespace {

const int kShortHeaderLength = 1;
const int kLongHeaderLength = 3;

enum ItemType {
  TYPE_MAIN = 0,
  TYPE_GLOBAL = 1,
  TYPE_LOCAL = 2,
  TYPE_RESERVED = 3
};

enum MainItemTag {
  MAIN_TAG_DEFAULT = 0x00,
  MAIN_TAG_INPUT = 0x08,
  MAIN_TAG_OUTPUT = 0x09,
  MAIN_TAG_COLLECTION = 0x0a,
  MAIN_TAG_FEATURE = 0x0b,
  MAIN_TAG_END_COLLECTION = 0x0c
};

enum GlobalItemTag {
  GLOBAL_TAG_USAGE_PAGE = 0x00,
  GLOBAL_TAG_LOGICAL_MINIMUM = 0x01,
  GLOBAL_TAG_LOGICAL_MAXIMUM = 0x02,
  GLOBAL_TAG_PHYSICAL_MINIMUM = 0x03,
  GLOBAL_TAG_PHYSICAL_MAXIMUM = 0x04,
  GLOBAL_TAG_UNIT_EXPONENT = 0x05,
  GLOBAL_TAG_UNIT = 0x06,
  GLOBAL_TAG_REPORT_SIZE = 0x07,
  GLOBAL_TAG_REPORT_ID = 0x08,
  GLOBAL_TAG_REPORT_COUNT = 0x09,
  GLOBAL_TAG_PUSH = 0x0A,
  GLOBAL_TAG_POP = 0x0B
};

enum LocalItemTag {
  LOCAL_TAG_USAGE = 0x00,
  LOCAL_TAG_USAGE_MINIMUM = 0x01,
  LOCAL_TAG_USAGE_MAXIMUM = 0x02,
  LOCAL_TAG_DESIGNATOR_INDEX = 0x03,
  LOCAL_TAG_DESIGNATOR_MINIMUM = 0x04,
  LOCAL_TAG_DESIGNATOR_MAXIMUM = 0x05,
  LOCAL_TAG_STRING_INDEX = 0x07,
  LOCAL_TAG_STRING_MINIMUM = 0x08,
  LOCAL_TAG_STRING_MAXIMUM = 0x09,
  LOCAL_TAG_DELIMITER = 0x0A
};

enum ReservedItemTag {
  RESERVED_TAG_LONG = 0x0f,
};

struct DescriptorItem {
  ItemType type;
  union {
    MainItemTag main;
    GlobalItemTag global;
    LocalItemTag local;
    ReservedItemTag reserved;
    uint32_t raw;
  } tag;
  uint32_t data_value;
  int depth;
};

// Attempts to populate a ReportDescriptor for a given hidraw device.
bool GetHidReportDescriptor(struct udev_device* device,
                            HidReportDescriptor* descriptor) {
  const char* dev_node = udev_device_get_devnode(device);
  int device_fd = open(dev_node, O_RDONLY);
  if (device_fd < 0) {
    return false;
  }
  unsigned size = 0;
  if (ioctl(device_fd, HIDIOCGRDESCSIZE, &size) < 0) {
    close(device_fd);
    return false;
  }
  hidraw_report_descriptor report_descriptor;
  report_descriptor.size = size;
  if (size > sizeof(report_descriptor.value) ||
      ioctl(device_fd, HIDIOCGRDESC, &report_descriptor) < 0) {
    close(device_fd);
    return false;
  }
  close(device_fd);
  if (report_descriptor.size > sizeof(descriptor->data)) {
    return false;
  }
  descriptor->size = report_descriptor.size;
  memcpy(&descriptor->data[0], &report_descriptor.value[0], descriptor->size);
  return true;
}

bool ParseDescriptorItem(const HidReportDescriptor& descriptor,
                         int offset,
                         int current_depth,
                         DescriptorItem* item,
                         int* bytes_read,
                         int* new_depth) {
  if (offset >= descriptor.size) {
    return false;
  }
  // HID 1.11 6.2.2.2: bSize 0/1/2/3 -> 0/1/2/4 data bytes.
  static const int kSizeMap[4] = {0, 1, 2, 4};
  uint8_t header = descriptor.data[offset];
  int data_size = kSizeMap[header & 0x03];
  item->type = static_cast<ItemType>((header >> 2) & 0x03);
  item->tag.raw = (header & 0xf0) >> 4;
  item->depth = current_depth;
  item->data_value = 0;

  // Long-form items are ignored.
  if (item->type == TYPE_RESERVED && item->tag.reserved == RESERVED_TAG_LONG) {
    if (offset + kShortHeaderLength >= descriptor.size) {
      return false;
    }
    int long_descriptor_size = descriptor.data[offset + kShortHeaderLength];
    if (offset + kLongHeaderLength + long_descriptor_size > descriptor.size) {
      return false;
    }
    *bytes_read = kLongHeaderLength + long_descriptor_size;
    return true;
  }

  if (offset + data_size + 1 > descriptor.size) {
    return false;
  }
  memcpy(&item->data_value, &descriptor.data[offset + 1], data_size);
  *bytes_read = kShortHeaderLength + data_size;

  if (item->type != TYPE_MAIN) {
    return true;
  }

  if (item->tag.main == MAIN_TAG_END_COLLECTION) {
    if (current_depth == 0) {
      return false;
    }
    *new_depth = current_depth - 1;
  } else if (item->tag.main == MAIN_TAG_COLLECTION) {
    *new_depth = current_depth + 1;
  }

  return true;
}

// Attempts to extract all toplevel items from a descriptor, preserving order.
// Returns false if any header contents are missing or invalid.
bool ParseToplevelDescriptorItems(const HidReportDescriptor& descriptor,
                                  std::vector<DescriptorItem>* items) {
  int depth = 0;
  int offset = 0;
  while (offset < descriptor.size) {
    int bytes_read;
    DescriptorItem item;
    if (!ParseDescriptorItem(descriptor, offset, depth, &item, &bytes_read,
                             &depth)) {
      return false;
    }
    if (item.depth == 0) {
      items->push_back(item);
    }
    offset += bytes_read;
  }
  return offset == descriptor.size;
}

}  // namespace

HidrawSubsystemUdevRule::HidrawSubsystemUdevRule(const string& name)
    : Rule(name) {}

Rule::Result HidrawSubsystemUdevRule::ProcessDevice(
    struct udev_device* device) {
  const char* const subsystem = udev_device_get_subsystem(device);
  if (!subsystem || strcmp(subsystem, "hidraw")) {
    return IGNORE;
  }
  return ProcessHidrawDevice(device);
}

// static
bool HidrawSubsystemUdevRule::ParseToplevelCollectionUsages(
    const HidReportDescriptor& descriptor, std::vector<HidUsage>* usages) {
  std::vector<DescriptorItem> items;
  if (!ParseToplevelDescriptorItems(descriptor, &items)) {
    return false;
  }
  // Track Usage Page as a persistent global and Usage as a local item that is
  // consumed by the next main item, matching kernel hid-core semantics.
  uint32_t usage_page = 0;
  bool have_usage = false;
  uint32_t usage = 0;
  for (const auto& it : items) {
    if (it.type == TYPE_GLOBAL && it.tag.global == GLOBAL_TAG_USAGE_PAGE) {
      usage_page = it.data_value;
    } else if (it.type == TYPE_LOCAL && it.tag.local == LOCAL_TAG_USAGE) {
      usage = it.data_value;
      have_usage = true;
    } else if (it.type == TYPE_MAIN) {
      if (it.tag.main == MAIN_TAG_COLLECTION && have_usage) {
        usages->push_back(HidUsage(static_cast<HidUsage::Page>(usage_page),
                                   static_cast<uint16_t>(usage)));
      }
      have_usage = false;  // main item resets local state
    }
  }
  return true;
}

// static
bool HidrawSubsystemUdevRule::GetHidToplevelUsages(
    struct udev_device* device, std::vector<HidUsage>* usages) {
  const char* dev_node = udev_device_get_devnode(device);

  HidReportDescriptor descriptor;
  if (!GetHidReportDescriptor(device, &descriptor)) {
    LOG(INFO) << "Unable to query descriptor for " << dev_node;
    return false;
  }

  if (!ParseToplevelCollectionUsages(descriptor, usages)) {
    LOG(INFO) << "Error parsing descriptor for " << dev_node;
    return false;
  }

  return true;
}

}  // namespace permission_broker
