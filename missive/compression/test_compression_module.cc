// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/compression/test_compression_module.h"

#include <string>
#include <utility>

#include <base/callback.h>
#include <base/strings/string_piece.h>

#include "missive/proto/record.pb.h"
#include "missive/util/statusor.h"

using ::testing::Invoke;

namespace reporting {
namespace test {

constexpr size_t kCompressionThreshold = 2;
const CompressionInformation::CompressionAlgorithm kCompressionType =
    CompressionInformation::COMPRESSION_NONE;

TestCompressionModuleStrict::TestCompressionModuleStrict()
    : CompressionModule(kCompressionThreshold, kCompressionType) {
  ON_CALL(*this, CompressRecord)
      .WillByDefault(Invoke(
          [](std::string record,
             base::OnceCallback<void(
                 std::string, base::Optional<CompressionInformation>)> cb) {
            // compression_info is not set.
            std::move(cb).Run(record, base::nullopt);
          }));
}

TestCompressionModuleStrict::~TestCompressionModuleStrict() = default;

}  // namespace test
}  // namespace reporting
