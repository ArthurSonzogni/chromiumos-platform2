// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/compression/compression_module.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/containers/flat_map.h>
#include <base/hash/hash.h>
#include <base/rand_util.h>
#include <base/strings/strcat.h>
#include <base/feature_list.h>
#include <base/synchronization/waitable_event.h>
#include <base/task/thread_pool.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <snappy.h>

#include "missive/compression/scoped_compression_feature.h"
#include "missive/proto/record.pb.h"
#include "missive/util/test_support_callbacks.h"

using ::testing::Eq;
using ::testing::StrEq;

namespace reporting {
namespace {

constexpr char kTestString[] = "AAAAA11111";

class CompressionModuleTest : public ::testing::Test {
 protected:
  CompressionModuleTest() = default;

  std::string BenchmarkCompressRecordSnappy(std::string record_string) {
    std::string output;
    snappy::Compress(record_string.data(), record_string.size(), &output);
    return output;
  }

  scoped_refptr<CompressionModule> compression_module_;
  base::test::TaskEnvironment task_environment_{};
};

TEST_F(CompressionModuleTest, CompressRecordSnappy) {
  test::ScopedCompressionFeature compression_feature{/*enable=*/true};
  scoped_refptr<CompressionModule> test_compression_module =
      CompressionModule::Create(0, CompressionInformation::COMPRESSION_SNAPPY);

  // Compress string directly with snappy as benchmark
  const std::string expected_output =
      BenchmarkCompressRecordSnappy(kTestString);

  test::TestMultiEvent<std::string, absl::optional<CompressionInformation>>
      compressed_record_event;
  // Compress string with CompressionModule
  test_compression_module->CompressRecord(kTestString,
                                          compressed_record_event.cb());

  const std::tuple<std::string, absl::optional<CompressionInformation>>
      compressed_record_tuple = compressed_record_event.result();

  const base::StringPiece compressed_string_callback =
      std::get<0>(compressed_record_tuple);

  // Expect that benchmark compression is the same as compression module
  EXPECT_THAT(compressed_string_callback, StrEq(expected_output));

  const absl::optional<CompressionInformation> compression_info =
      std::get<1>(compressed_record_tuple);

  EXPECT_TRUE(compression_info.has_value());

  // Expect that compression information contains COMPRESSION_SNAPPY
  EXPECT_THAT(compression_info.value().compression_algorithm(),
              Eq(CompressionInformation::COMPRESSION_SNAPPY));
}

TEST_F(CompressionModuleTest, CompressRecordBelowThreshold) {
  test::ScopedCompressionFeature compression_feature{/*enable=*/true};
  scoped_refptr<CompressionModule> test_compression_module =
      CompressionModule::Create(512,
                                CompressionInformation::COMPRESSION_SNAPPY);

  test::TestMultiEvent<std::string, absl::optional<CompressionInformation>>
      compressed_record_event;
  // Compress string with CompressionModule
  test_compression_module->CompressRecord(kTestString,
                                          compressed_record_event.cb());

  const std::tuple<std::string, absl::optional<CompressionInformation>>
      compressed_record_tuple = compressed_record_event.result();

  const base::StringPiece compressed_string_callback =
      std::get<0>(compressed_record_tuple);

  // Expect that record is not compressed since size is smaller than 512 bytes
  EXPECT_THAT(compressed_string_callback, StrEq(kTestString));

  const absl::optional<CompressionInformation> compression_info =
      std::get<1>(compressed_record_tuple);

  EXPECT_TRUE(compression_info.has_value());

  // Expect that compression information contains COMPRESSION_NONE since the
  // record was below the compression threshold.
  EXPECT_THAT(compression_info.value().compression_algorithm(),
              Eq(CompressionInformation::COMPRESSION_NONE));
}

TEST_F(CompressionModuleTest, CompressRecordCompressionDisabled) {
  // Disable compression feature
  test::ScopedCompressionFeature compression_feature{/*disable=*/false};
  scoped_refptr<CompressionModule> test_compression_module =
      CompressionModule::Create(0, CompressionInformation::COMPRESSION_SNAPPY);

  test::TestMultiEvent<std::string, absl::optional<CompressionInformation>>
      compressed_record_event;

  // Compress string with CompressionModule
  test_compression_module->CompressRecord(kTestString,
                                          compressed_record_event.cb());

  const std::tuple<std::string, absl::optional<CompressionInformation>>
      compressed_record_tuple = compressed_record_event.result();

  const base::StringPiece compressed_string_callback =
      std::get<0>(compressed_record_tuple);

  // Expect that record is not compressed since compression is not enabled
  EXPECT_THAT(compressed_string_callback, StrEq(kTestString));

  const absl::optional<CompressionInformation> compression_info =
      std::get<1>(compressed_record_tuple);

  // Expect no compression information since compression has been disabled.
  EXPECT_FALSE(compression_info.has_value());
}

TEST_F(CompressionModuleTest, CompressRecordCompressionNone) {
  test::ScopedCompressionFeature compression_feature{/*enable=*/true};
  scoped_refptr<CompressionModule> test_compression_module =
      CompressionModule::Create(0, CompressionInformation::COMPRESSION_NONE);

  test::TestMultiEvent<std::string, absl::optional<CompressionInformation>>
      compressed_record_event;

  // Compress string with CompressionModule
  test_compression_module->CompressRecord(kTestString,
                                          compressed_record_event.cb());
  const std::tuple<std::string, absl::optional<CompressionInformation>>
      compressed_record_tuple = compressed_record_event.result();

  const base::StringPiece compressed_string_callback =
      std::get<0>(compressed_record_tuple);

  // Expect that record is not compressed since COMPRESSION_NONE was chosen as
  // the compression_algorithm.
  EXPECT_THAT(compressed_string_callback, StrEq(kTestString));

  const absl::optional<CompressionInformation> compression_info =
      std::get<1>(compressed_record_tuple);

  EXPECT_TRUE(compression_info.has_value());

  // Expect that compression information contains COMPRESSION_NONE
  EXPECT_THAT(compression_info.value().compression_algorithm(),
              Eq(CompressionInformation::COMPRESSION_NONE));
}

}  // namespace
}  // namespace reporting
