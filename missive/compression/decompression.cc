// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "missive/compression/decompression.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/feature_list.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/strings/string_piece.h>
#include <base/task/thread_pool.h>
#include <snappy.h>

#include "missive/proto/record.pb.h"
#include "missive/resources/resource_interface.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace reporting {

// static
scoped_refptr<Decompression> Decompression::Create() {
  return base::WrapRefCounted(new Decompression());
}

std::string Decompression::DecompressRecord(
    std::string record, CompressionInformation compression_information) {
  // Before doing decompression, we must make sure there is enough memory - we
  // are going to temporarily double the record.
  ScopedReservation scoped_reservation(record.size(), GetMemoryResource());
  if (!scoped_reservation.reserved()) {
    LOG(ERROR) << "SCOPED HAS TRIGGERED";
    return record;
  }

  // Decompress
  switch (compression_information.compression_algorithm()) {
    case CompressionInformation::COMPRESSION_NONE: {
      // Don't decompress, simply return serialized record
      LOG(ERROR) << "RETURN RAW RECORD";
      return record;
    }
    case CompressionInformation::COMPRESSION_SNAPPY: {
      return Decompression::DecompressRecordSnappy(std::move(record));
    }
  }
}

Decompression::Decompression() {}
Decompression::~Decompression() = default;

std::string Decompression::DecompressRecordSnappy(std::string record) {
  // Compression is enabled and crosses the threshold,
  std::string output;
  snappy::Uncompress(record.data(), record.size(), &output);
  return output;
}
}  // namespace reporting
