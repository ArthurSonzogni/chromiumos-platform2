// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_COMPRESSION_TEST_COMPRESSION_MODULE_H_
#define MISSIVE_COMPRESSION_TEST_COMPRESSION_MODULE_H_

#include <string>

#include <base/callback.h>
#include <base/strings/string_piece.h>

#include "missive/compression/compression_module.h"
#include "missive/proto/record.pb.h"
#include "missive/util/statusor.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace reporting {
namespace test {

// An |CompressionModuleInterface| that does no compression.
class TestCompressionModuleStrict : public CompressionModule {
 public:
  TestCompressionModuleStrict();

  MOCK_METHOD(void,
              CompressRecord,
              (std::string record,
               base::OnceCallback<void(
                   std::string, absl::optional<CompressionInformation>)> cb),
              (const override));

 protected:
  ~TestCompressionModuleStrict() override;
};

// Most of the time no need to log uninterested calls to |EncryptRecord|.
typedef ::testing::NiceMock<TestCompressionModuleStrict> TestCompressionModule;

}  // namespace test
}  // namespace reporting

#endif  // MISSIVE_COMPRESSION_TEST_COMPRESSION_MODULE_H_
