// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_CONSUMER_INTERFACE_H_
#define LIBTOUCHRAW_CONSUMER_INTERFACE_H_

#include <memory>

#include "libtouchraw/touchraw.h"

namespace touchraw {

class HIDDataConsumerInterface {
 public:
  virtual void Push(std::unique_ptr<const HIDData> data) = 0;
  virtual ~HIDDataConsumerInterface() = default;
};

class HeatmapChunkConsumerInterface {
 public:
  virtual void Push(std::unique_ptr<const HeatmapChunk> chunk) = 0;
  virtual ~HeatmapChunkConsumerInterface() = default;
};

class HeatmapConsumerInterface {
 public:
  virtual void Push(std::unique_ptr<Heatmap> hm) = 0;
  virtual ~HeatmapConsumerInterface() = default;
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_CONSUMER_INTERFACE_H_
