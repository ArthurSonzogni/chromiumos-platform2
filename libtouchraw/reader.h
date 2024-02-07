// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_READER_H_
#define LIBTOUCHRAW_READER_H_

#include <memory>

#include <absl/status/status.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <gtest/gtest_prod.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/touchraw_export.h"

namespace touchraw {

class LIBTOUCHRAW_EXPORT Reader {
 public:
  /**
   * Reader constructor.
   * This class monitors and reads the input device when HID data is available.
   *
   * @param fd Scoped file descriptor of the input device.
   * @param q HIDData consumer queue for tasks to be posted.
   * @param watcher File descriptor watcher, default is nullptr.
   */
  explicit Reader(base::ScopedFD fd,
                  std::unique_ptr<HIDDataConsumerInterface> q,
                  std::unique_ptr<base::FileDescriptorWatcher::Controller>
                      watcher = nullptr);

  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

  virtual ~Reader() = default;

  // Start reading events.
  absl::Status Start();

  // Stop reading events.
  void Stop();

 private:
  FRIEND_TEST(ReaderTest, StopSucceeded);
  FRIEND_TEST(ReaderTest, ReadFailed);
  FRIEND_TEST(ReaderTest, EmptyBuffer);
  FRIEND_TEST(ReaderTest, ValidBufferOneByte);
  FRIEND_TEST(ReaderTest, ValidBufferFiveBytes);

  void OnFileCanReadWithoutBlocking(int fd);
  void ProcessData(const uint8_t* buf, const ssize_t read_size);

  // File descriptor to read.
  base::ScopedFD fd_;

  // Consumer queue.
  const std::unique_ptr<HIDDataConsumerInterface> q_;

  // Controller for watching the input file descriptor.
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_READER_H_
