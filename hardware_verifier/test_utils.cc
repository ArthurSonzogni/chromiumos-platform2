/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hardware_verifier/test_utils.h"

#include <cstdlib>
#include <string>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <google/protobuf/text_format.h>
#include <gtest/gtest.h>

#include "hardware_verifier/hardware_verifier.pb.h"

namespace hardware_verifier {

base::FilePath GetTestDataPath() {
  char* src_env = std::getenv("SRC");
  CHECK_NE(src_env, nullptr)
      << "Expect to have the envvar |SRC| set when testing.";
  return base::FilePath(src_env).Append("testdata");
}

HwVerificationReport LoadHwVerificationReport(const base::FilePath& file_path) {
  std::string content;
  EXPECT_TRUE(base::ReadFileToString(file_path, &content));

  HwVerificationReport ret;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(content, &ret));
  return ret;
}

BaseFileTest::PathType::PathType(std::initializer_list<std::string> paths) {
  for (const std::string& path : paths) {
    file_path_ = file_path_.Append(path);
  }
}

BaseFileTest::BaseFileTest() {
  SetTestRoot(mock_context()->root_dir());
}

void BaseFileTest::SetTestRoot(const base::FilePath& path) {
  ASSERT_TRUE(root_dir_.empty());
  ASSERT_FALSE(path.empty());
  root_dir_ = path;
}

base::FilePath BaseFileTest::GetPathUnderRoot(const PathType& path) const {
  CHECK(!root_dir_.empty());
  // Check if the path already under the test rootfs.
  CHECK(!root_dir_.IsParent(path.file_path()));
  if (!path.file_path().IsAbsolute()) {
    return root_dir_.Append(path.file_path());
  }
  auto res = root_dir_;
  CHECK(base::FilePath("/").AppendRelativePath(path.file_path(), &res));
  return res;
}

const base::FilePath& BaseFileTest::root_dir() const {
  CHECK(!root_dir_.empty());
  return root_dir_;
}

}  // namespace hardware_verifier
