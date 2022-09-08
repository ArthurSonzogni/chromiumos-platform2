// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/compression/test_compression_module.h"

#include <optional>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_piece.h>

#include "missive/proto/record.pb.h"
#include "missive/resources/resource_interface.h"
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
             scoped_refptr<ResourceInterface> resource_interface,
             base::OnceCallback<void(
                 std::string, std::optional<CompressionInformation>)> cb) {
            // compression_info is not set.
            std::move(cb).Run(record, std::nullopt);
          }));
}

TestCompressionModuleStrict::~TestCompressionModuleStrict() = default;

}  // namespace test
}  // namespace reporting
