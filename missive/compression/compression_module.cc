// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "missive/compression/compression_module.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/feature_list.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/optional.h>
#include <base/strings/string_piece.h>
#include <base/task/thread_pool.h>
#include <snappy.h>

#include "missive/proto/record.pb.h"
#include "missive/storage/resources/resource_interface.h"

namespace reporting {

const base::Feature kCompressReportingPipeline{
    CompressionModule::kCompressReportingFeature,
    base::FEATURE_ENABLED_BY_DEFAULT};

// static
const char CompressionModule::kCompressReportingFeature[] =
    "CompressReportingPipeline";

// static
scoped_refptr<CompressionModule> CompressionModule::Create(
    size_t compression_threshold,
    CompressionInformation::CompressionAlgorithm compression_type) {
  return base::WrapRefCounted(
      new CompressionModule(compression_threshold, compression_type));
}

void CompressionModule::CompressRecord(
    std::string record,
    base::OnceCallback<void(std::string,
                            base::Optional<CompressionInformation>)> cb) const {
  if (!is_enabled()) {
    // Compression disabled, don't compress and don't return compression
    // information.
    std::move(cb).Run(std::move(record), base::nullopt);
    return;
  }
  if (record.length() < compression_threshold_) {
    // Record size is smaller than threshold, don't compress.
    CompressionInformation compression_information;
    compression_information.set_compression_algorithm(
        CompressionInformation::COMPRESSION_NONE);
    std::move(cb).Run(std::move(record), std::move(compression_information));
    return;
  }
  // Before doing compression, we must make sure there is enough memory - we are
  // going to temporarily double the record.
  ScopedReservation scoped_reservation(record.size(), GetMemoryResource());
  if (!scoped_reservation.reserved()) {
    CompressionInformation compression_information;
    compression_information.set_compression_algorithm(
        CompressionInformation::COMPRESSION_NONE);
    std::move(cb).Run(std::move(record), std::move(compression_information));
    return;
  }

  // Compress if record is larger than the compression threshold and compression
  // enabled
  switch (compression_type_) {
    case CompressionInformation::COMPRESSION_NONE: {
      // Don't compress, simply return serialized record
      CompressionInformation compression_information;
      compression_information.set_compression_algorithm(
          CompressionInformation::COMPRESSION_NONE);
      std::move(cb).Run(std::move(record), std::move(compression_information));
      break;
    }
    case CompressionInformation::COMPRESSION_SNAPPY: {
      CompressionModule::CompressRecordSnappy(std::move(record), std::move(cb));
      break;
    }
  }
}

// static
bool CompressionModule::is_enabled() {
  return base::FeatureList::IsEnabled(kCompressReportingPipeline);
}

CompressionModule::CompressionModule(
    size_t compression_threshold,
    CompressionInformation::CompressionAlgorithm compression_type)
    : compression_type_(compression_type),
      compression_threshold_(compression_threshold) {}
CompressionModule::~CompressionModule() = default;

void CompressionModule::CompressRecordSnappy(
    std::string record,
    base::OnceCallback<void(std::string,
                            base::Optional<CompressionInformation>)> cb) const {
  // Compression is enabled and crosses the threshold,
  std::string output;
  snappy::Compress(record.data(), record.size(), &output);

  // Return compressed string
  CompressionInformation compression_information;
  compression_information.set_compression_algorithm(
      CompressionInformation::COMPRESSION_SNAPPY);
  std::move(cb).Run(output, compression_information);
}
}  // namespace reporting
