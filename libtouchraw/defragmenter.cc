// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtouchraw/defragmenter.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/memory/ptr_util.h>

#include "libtouchraw/consumer_interface.h"

namespace touchraw {

constexpr int kByte = 0x08;  // Number of bits per byte.
// Refer to go/cros-heatmap-external v0.5 Heatmap Data Format for details.
// Byte 0    Protocol version
// Byte 1    Bit depth
// Byte 2    Height of the heatmap
// Byte 3    Width of the heatmap
// Byte 4,5  Filtering threshold
// Byte 6,7  Length
constexpr int kPayloadHeaderSize = 8;  // Payload header size in bytes.
constexpr int kStartSeqID = 1;
constexpr int kInvalidSeqID = kStartSeqID - 1;

Defragmenter::Defragmenter(std::unique_ptr<HeatmapConsumerInterface> q)
    : hm_(std::make_unique<Heatmap>()),
      scan_time_(std::numeric_limits<int64_t>::min()),
      expected_seq_id_(kInvalidSeqID),
      q_(std::move(q)) {}

// TODO: b/326310591 - Improve the defragmenter class to be more readable.
void Defragmenter::DefragmentHeatmap(
    std::unique_ptr<const HeatmapChunk> chunk) {
  ssize_t size;  // The size of current chunk added to heatmap payload without
                 // padding.

  if (!ValidateChunk(chunk.get())) {
    LOG(WARNING) << "Discard an invalid chunk. Report type "
                 << chunk->report_type;
    return;
  }

  if (chunk->scan_time != scan_time_) {  // New frame.
    if (!hm_->payload.empty() &&
        hm_->payload.size() != hm_->length) {  // Drop incomplete frames.
      LOG(WARNING) << "Discard an incomplete frame with scan time "
                   << hm_->scan_time << ", frame size " << hm_->payload.size()
                   << ", expected frame size " << hm_->length;
    }
    if (chunk->report_type == ReportType::kFirst) {  // First chunk.
      hm_->vendor_id = chunk->vendor_id;
      hm_->protocol_version = chunk->protocol_version;
      hm_->scan_time = chunk->scan_time;
      byte_count_ = chunk->byte_count.value();

      if (!GetPayloadHeader(chunk->payload))
        return;

      size = std::min(static_cast<uint32_t>(chunk->payload.size()),
                      chunk->byte_count.value());
      hm_->payload.assign(chunk->payload.begin() + kPayloadHeaderSize,
                          chunk->payload.begin() + size);

      expected_seq_id_ = kStartSeqID;
    } else {  // The fist chunk is missing - discard.
      if (chunk->report_type == ReportType::kSubsequent) {
        LOG(WARNING) << "Discard a chunk with (scan time " << chunk->scan_time
                     << ", seq id " << chunk->sequence_id.value()
                     << "), first chunk is missing.";
      } else {
        LOG(WARNING) << "Invalid report type " << chunk->report_type;
      }
      return;
    }
  } else {  // Subsequent chunks.
    if (chunk->report_type == ReportType::kSubsequent) {
      if (expected_seq_id_ == kInvalidSeqID) {
        LOG(WARNING) << "Discard a disrupted frame with scan time "
                     << hm_->scan_time;
        hm_->payload.clear();
        return;
      }
      if (chunk->sequence_id.value() == expected_seq_id_) {
        size =
            std::min(static_cast<uint32_t>(chunk->payload.size()),
                     hm_->length - static_cast<uint32_t>(hm_->payload.size()));
        hm_->payload.insert(hm_->payload.end(), chunk->payload.begin(),
                            chunk->payload.begin() + size);

        ++expected_seq_id_;
      } else {
        LOG(WARNING) << "Discard a chunk with (scan time " << chunk->scan_time
                     << ", seq id " << chunk->sequence_id.value()
                     << ") due to disrupted sequences. The expected seq id is "
                     << expected_seq_id_;
        expected_seq_id_ = kInvalidSeqID;
        return;
      }
    } else {
      LOG(WARNING) << "Unknown report type " << chunk->report_type;
      expected_seq_id_ = kInvalidSeqID;
      return;
    }
  }

  scan_time_ = chunk->scan_time;
  if (hm_->payload.size() ==
      hm_->length) {  // All chunks of a frame have arrived.
    if (!ValidatePadding(chunk->payload, size))
      return;
    // TODO: b/320785596 - Add more validations if necessary.
    q_->Push(std::move(hm_));
    hm_ = std::make_unique<Heatmap>();
  } else if (hm_->payload.size() > hm_->length) {
    LOG(WARNING) << "Discard a frame with scan time " << hm_->scan_time
                 << " as the payload size " << hm_->payload.size()
                 << " is larger than the expected size " << hm_->length;
  }
}

bool Defragmenter::ValidateChunk(const HeatmapChunk* chunk) {
  if (!chunk) {
    LOG(WARNING) << "Heatmap chunk is invalid.";
    return false;
  }
  if (chunk->report_type == ReportType::kFirst) {
    if (!chunk->byte_count.has_value()) {
      LOG(WARNING)
          << "Received a first chunk but byte count does not contain a value.";
      return false;
    }
  } else if (chunk->report_type == ReportType::kSubsequent) {
    if (!chunk->sequence_id.has_value()) {
      LOG(WARNING) << "Received a subsequent chunk but sequence id does not "
                      "contain a value.";
      return false;
    }
  } else {
    LOG(WARNING) << "Invalid report type.";
    return false;
  }
  return true;
}

bool Defragmenter::GetPayloadHeader(std::span<const uint8_t> payload) {
  if (payload.size() < kPayloadHeaderSize) {
    LOG(WARNING) << "Heatmap payload size " << payload.size()
                 << " is too short.";
    return false;
  }

  hm_->encoding = static_cast<EncodingType>(payload[0]);
  hm_->bit_depth = payload[1];
  hm_->height = payload[2];
  hm_->width = payload[3];
  hm_->threshold = payload[4] | (payload[5] << kByte);
  hm_->length = payload[6] | (payload[7] << kByte);

  if (hm_->length != (byte_count_ - kPayloadHeaderSize)) {
    LOG(WARNING) << "Heatmap length " << hm_->length << " does not equal to "
                 << (byte_count_ - kPayloadHeaderSize);
    return false;
  }

  return true;
}

bool Defragmenter::ValidatePadding(std::span<const uint8_t> payload,
                                   const ssize_t padding_offset) {
  // Zero padding validation.
  for (int i = padding_offset; i < payload.size(); ++i) {
    if (payload[i] != 0) {
      LOG(WARNING) << "Zero padding validation failed.";
      return false;
    }
  }

  return true;
}

}  // namespace touchraw
