// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/callback.h>
#include <base/memory/ref_counted.h>
#include <base/optional.h>
#include <base/strings/string_piece.h>

#include "missive/proto/record.pb.h"

#ifndef MISSIVE_COMPRESSION_COMPRESSION_MODULE_H_
#define MISSIVE_COMPRESSION_COMPRESSION_MODULE_H_

namespace reporting {

class CompressionModule : public base::RefCountedThreadSafe<CompressionModule> {
 public:
  // Feature to enable/disable compression.
  // By default compression is disabled, until server can support compression.
  static const char kCompressReportingFeature[];

  // Not copyable or movable
  CompressionModule(const CompressionModule& other) = delete;
  CompressionModule& operator=(const CompressionModule& other) = delete;

  // Factory method creates |CompressionModule| object.
  static scoped_refptr<CompressionModule> Create(
      size_t compression_threshold_,
      CompressionInformation::CompressionAlgorithm compression_type_);

  // CompressRecord will attempt to compress the provided |record| and respond
  // with the callback. On success the returned std::string sink will
  // contain a compressed WrappedRecord string. The sink string then can be
  // further updated by the caller. std::string is used instead of
  // base::StringPiece because ownership is taken of |record| through
  // std::move(record).
  void CompressRecord(
      std::string record,
      base::OnceCallback<
          void(std::string, base::Optional<CompressionInformation>)> cb) const;

  // Returns 'true' if |kCompressReportingPipeline| feature is enabled.
  static bool is_enabled();

  // Variable which defines which compression type to use
  const CompressionInformation::CompressionAlgorithm compression_type_;

 protected:
  // Constructor can only be called by |Create| factory method.
  CompressionModule(
      size_t compression_threshold_,
      CompressionInformation::CompressionAlgorithm compression_type_);

  // Refcounted object must have destructor declared protected or private.
  virtual ~CompressionModule();

 private:
  friend base::RefCountedThreadSafe<CompressionModule>;

  // Compresses a record using snappy
  void CompressRecordSnappy(
      std::string record,
      base::OnceCallback<
          void(std::string, base::Optional<CompressionInformation>)> cb) const;

  // Minimum compression threshold (in bytes) for when a record will be
  // compressed
  const size_t compression_threshold_;
};

}  // namespace reporting

#endif  // MISSIVE_COMPRESSION_COMPRESSION_MODULE_H_
