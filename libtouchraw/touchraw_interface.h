// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_TOUCHRAW_INTERFACE_H_
#define LIBTOUCHRAW_TOUCHRAW_INTERFACE_H_

#include <memory>

#include <absl/status/status.h>
#include <base/files/file_path.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/crop.h"
#include "libtouchraw/reader.h"
#include "libtouchraw/touchraw_export.h"

namespace touchraw {

class LIBTOUCHRAW_EXPORT TouchrawInterface {
 public:
  /**
   * Factory method: creates and returns a TouchrawInterface.
   * May return null on failure.
   *
   * @param path Input device file path.
   * @param consumer Heatmap consumer queue for tasks to be posted.
   * @param crop Optional crop to apply to heatmap if needed.
   *
   * @return Unique pointer of TouchrawInterface if create succeeds, null
   * pointer otherwise.
   */
  static std::unique_ptr<TouchrawInterface> Create(
      const base::FilePath& path,
      std::unique_ptr<HeatmapConsumerInterface> consumer,
      Crop crop = {0, 0, 0, 0});

  TouchrawInterface(const TouchrawInterface&) = delete;
  TouchrawInterface& operator=(const TouchrawInterface&) = delete;

  /**
   * Start watching the file descriptor.
   *
   * @return True if the file descriptor is being watched successfully, false
   * otherwise.
   */
  absl::Status StartWatching();
  // Stop watching the file descriptor.
  void StopWatching();

 private:
  explicit TouchrawInterface(std::unique_ptr<Reader> reader);

  std::unique_ptr<Reader> reader_;
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_TOUCHRAW_INTERFACE_H_
