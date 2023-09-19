// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imageloader/dlc.h"

#include <memory>

#include <base/values.h>
#include <dlcservice/metadata/mock_metadata.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "imageloader/manifest.h"
#include "imageloader/mock_helper_process_proxy.h"
#include "imageloader/test_utilities.h"

namespace imageloader {

using dlcservice::metadata::MockMetadata;

namespace {
MockMetadata::Entry MakeTestMetadata() {
  return {
      .manifest = base::Value::Dict()
                      .Set("table-sha256-hash",
                           "4bbb4dc53254e28c6a870d979cbdeaee"
                           "9ad86fae66a8664dd939e6e0f70eb681")
                      .Set("image-sha256-hash",
                           "a7e78b6e269800b60c760b918920727e"
                           "0033f5649fda7f638270fa306b034960")
                      .Set("fs-type", "ext4")
                      .Set("version", "0.0.1")
                      .Set("is-removable", true)
                      .Set("manifest-version", 1),
      .table =
          "0 8000 verity payload=ROOT_DEV hashtree=HASH_DEV hashstart=8000 "
          "alg=sha256 "
          "root_hexdigest="
          "d0ab1712e8c34b72be9b0f568fad8f95d2ffe94d26847ce9945d7bd083772b9d "
          "salt="
          "9f0d573399b8f7785f5ace130928e419098131b61c1f8307a7ec539d16fa3c09"};
}
}  // namespace

TEST(DlcTest, MountDlc) {
  base::FilePath metadata_path = GetTestDataPath("example_dlc");
  base::FilePath image_path = metadata_path.Append("dlc.img");
  base::FilePath manifest_path = metadata_path.Append("imageloader.json");
  base::FilePath table_path = metadata_path.Append("table");

  auto proxy = std::make_unique<MockHelperProcessProxy>();
  EXPECT_CALL(*proxy, SendMountCommand(testing::_, testing::_,
                                       FileSystem::kExt4, testing::_))
      .Times(1);
  ON_CALL(*proxy,
          SendMountCommand(testing::_, testing::_, testing::_, testing::_))
      .WillByDefault(testing::Return(true));

  auto metadata = std::make_shared<MockMetadata>();
  Dlc dlc("id", "package", base::FilePath(), metadata);
  EXPECT_TRUE(dlc.Mount(proxy.get(), image_path, manifest_path, table_path,
                        base::FilePath()));
}

TEST(DlcTest, MountDlcWithCompressedMetadata) {
  base::FilePath image_path = GetTestDataPath("example_dlc").Append("dlc.img");

  auto metadata = std::make_shared<MockMetadata>();
  EXPECT_CALL(*metadata, Get).WillOnce(testing::Return(MakeTestMetadata()));

  auto proxy = std::make_unique<MockHelperProcessProxy>();
  EXPECT_CALL(*proxy, SendMountCommand(testing::_, testing::_,
                                       FileSystem::kExt4, testing::_))
      .Times(1);
  ON_CALL(*proxy,
          SendMountCommand(testing::_, testing::_, testing::_, testing::_))
      .WillByDefault(testing::Return(true));

  Dlc dlc("id", "package", base::FilePath(), metadata);
  EXPECT_TRUE(dlc.Mount(proxy.get(), image_path));
}

}  // namespace imageloader
