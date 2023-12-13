// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_PARSER_H_
#define LIBTOUCHRAW_PARSER_H_

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <linux/hidraw.h>

#include <base/task/sequenced_task_runner.h>
#include <brillo/udev/udev_device.h>
#include <gtest/gtest_prod.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/touchraw.h"
#include "libtouchraw/touchraw_export.h"

namespace touchraw {

// Please refer to
// https://www.usb.org/document-library/device-class-definition-hid-111 for HID
// report item format.
struct Item {
  std::span<const uint8_t> data;
  uint8_t data_size;  // Specify size of data. Based on the device class
                      // definition doc section 6.2.2.3, long items may contain
                      // up to 256 bytes of data. So 8 bits is enough here.
  uint16_t size;      // Specify total size of an item.
  uint8_t prefix;     // All items have a one-byte prefix that contains the item
                      // tag, item type, and item size.
};

// Describe a main item of report descriptor.
struct MainItem {
  uint16_t usage_page;
  uint16_t usage;
  uint8_t report_id;
  uint32_t data_size;  // Size of the data field in bytes.
};

// Necessary information to create a main item.
struct MainItemInfo {
  uint16_t usage_page;
  uint16_t usage;
  uint8_t report_id;
  uint32_t report_size;
  uint32_t report_count;
};

class ReportDescriptor {
 public:
  explicit ReportDescriptor(const hidraw_report_descriptor* rpt_desc);

  ReportDescriptor(const ReportDescriptor&) = delete;
  ReportDescriptor& operator=(const ReportDescriptor&) = delete;

  // True if the internal index has not reached the end of the report
  // descriptor; False otherwise.
  bool HasNextItem();
  // Return the next item.
  Item GetNextItem();
  // Reset the internal index.
  void Reset();

 private:
  const hidraw_report_descriptor* rpt_desc_;
  uint16_t next_item_idx_;  // Starting index of the next item to be processed.
};

class LIBTOUCHRAW_EXPORT Parser : public HIDDataConsumerInterface {
 public:
  /**
   * Factory method: creates and returns a Parser.
   * May return null on failure cases that it fails to get a report descriptor
   * or the report descriptor does not support heat map.
   *
   * @param fd File descriptor.
   * @param q HeatmapChunk consumer queue for tasks to be posted.
   * @return Unique pointer of Parser if create succeeds, null pointer
   * otherwise.
   */
  static std::unique_ptr<Parser> Create(const int fd,
                                        HeatmapChunkConsumerInterface* q);

  static std::unique_ptr<Parser> CreateForTesting(
      HeatmapChunkConsumerInterface* q);

  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  void Push(const HIDData data) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Parser::ParseHIDData, base::Unretained(this), data));
  }

 protected:
  /**
   * Parse HID data read from the file descriptor.
   *
   * @param hid_data
   */
  void ParseHIDData(const HIDData& hid_data);

 private:
  FRIEND_TEST(ParserTest, ReportDescriptorWithHeatmap);
  FRIEND_TEST(ParserTest, ReportDescriptorWithoutHeatmap);
  FRIEND_TEST(ParserTest, UnknownHidType);

  /**
   * Parser constructor.
   * This class retrieves the input device report descriptor and parses HID data
   * into heatmap chunks.
   *
   * @param q HeatmapChunk consumer queue for tasks to be posted.
   */
  explicit Parser(HeatmapChunkConsumerInterface* q);

  /**
   * Get report descriptor from ioctl.
   *
   * @return Usually, on success zero is returned. On error, -1 is returned, and
   * errno is set to indicate the error.
   */
  int GetReportDescriptorIoctl(const int fd,
                               hidraw_report_descriptor* rpt_desc);

  /**
   * Helper function to create a udev device.
   *
   * @return Unique pointer of UdevDevice if create succeeds, null pointer
   * otherwise.
   */
  std::unique_ptr<brillo::UdevDevice> CreateUdevDevice(const int fd);

  /**
   * Get report descriptor from sysfs.
   *
   * @return Usually, on success zero is returned. On error, -1 is returned, and
   * errno is set to indicate the error.
   */
  int GetReportDescriptorSysfs(const int fd,
                               hidraw_report_descriptor* rpt_desc);
  // TODO: b/317990775 - Extract descriptor parsing into a sub-library of
  // libtouchraw.
  bool ParseHeatmapReportsFromDescriptor(
      const hidraw_report_descriptor* rpt_desc);

  /**
   * Helper function to get data field value.
   *
   * @param index Data field index.
   * @param size Data field size.
   * @return Data field value.
   */
  uint32_t GetDataField(int index, int size, std::span<const uint8_t> payload);

  // Helper function to process items and save report data fields into the
  // usages table.
  void ProcessItem(Item& item);
  // Helper function to get the value of the data field of a report.
  uint32_t GetPropValue(std::span<const uint8_t> data, int data_size);

  // Usages table that contains each usage item from the report descriptor.
  std::vector<MainItem> usages_;
  // Stores the information of each usage item.
  MainItemInfo info_;
  // Offset index of usages table for the first chunk of heat map input report
  // type.
  std::optional<uint16_t> sync_report_offset_;
  // Offset index of usages table for the subsequent chunks of heat map input
  // report type.
  std::optional<uint16_t> sub_report_offset_;

  // Task queue.
  HeatmapChunkConsumerInterface* q_;
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_PARSER_H_
