// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTOUCHRAW_DEFRAGMENTER_H_
#define LIBTOUCHRAW_DEFRAGMENTER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <base/task/sequenced_task_runner.h>
#include <gtest/gtest_prod.h>

#include "libtouchraw/consumer_interface.h"
#include "libtouchraw/touchraw.h"
#include "libtouchraw/touchraw_export.h"

namespace touchraw {

class LIBTOUCHRAW_EXPORT Defragmenter : public HeatmapChunkConsumerInterface {
 public:
  /**
   * Defragmenter constructor.
   * For each heatmap frame, touch controller may send it in chunks.
   * This class takes in parsed heatmap chunks and combines them into one
   * heatmap chunk per frame if needed.
   *
   * @param q Heatmap consumer queue for tasks to be posted.
   */
  explicit Defragmenter(std::unique_ptr<HeatmapConsumerInterface> q);

  Defragmenter(const Defragmenter&) = delete;
  Defragmenter& operator=(const Defragmenter&) = delete;

  void Push(std::unique_ptr<const HeatmapChunk> chunk) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Defragmenter::DefragmentHeatmap,
                                  base::Unretained(this), std::move(chunk)));
  }

 protected:
  /**
   * Defragment parsed heatmap events.
   *
   * @param chunk One chunk of heatmap parsed data.
   */
  void DefragmentHeatmap(std::unique_ptr<const HeatmapChunk> chunk);

 private:
  // Helper function to retrieve heatmap data header as defined in
  // go/cros-heatmap-external Heatmap Data Format.
  bool GetPayloadHeader(std::span<const uint8_t> payload);
  // Helper function to validate zero padding.
  bool ValidatePadding(std::span<const uint8_t> payload,
                       const ssize_t padding_offset);
  // Helper function to do basic check for chunk.
  bool ValidateChunk(const HeatmapChunk* chunk);

  FRIEND_TEST(DefragmenterTest, CheckPayloadHeader);
  FRIEND_TEST(DefragmenterTest, InvalidZeroPadding);
  FRIEND_TEST(DefragmenterTest, LengthValidation);
  FRIEND_TEST(DefragmenterTest, ChunkValidation);

  std::unique_ptr<Heatmap> hm_;
  int64_t scan_time_;    // Scan time of the last chunk.
  uint32_t byte_count_;  // Total number of heat map data, which includes 8
                         // bytes header defined in go/cros-heatmap-external
                         // v0.5 Heatmap Data Format plus the actual heat map
                         // data. Zero padding is excluded.
  uint16_t expected_seq_id_;

  // Task queue.
  const std::unique_ptr<HeatmapConsumerInterface> q_;
};

}  // namespace touchraw

#endif  // LIBTOUCHRAW_DEFRAGMENTER_H_
