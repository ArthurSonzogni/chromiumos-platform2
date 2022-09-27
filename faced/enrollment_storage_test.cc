// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/enrollment_storage.h"

#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "faced/testing/status.h"

namespace faced {
namespace {

constexpr char kUserId1[] = "0000000000000000000000000000000000000001";
constexpr char kData1[] = "Hello, world1!";
constexpr char kUserId2[] = "0000000000000000000000000000000000000002";
constexpr char kData2[] = "Hello, world2!";

TEST(EnrollmentStorage, SavesAndReadsEnrollmentsCorrectly) {
  // Create a temp directory for saving files
  base::ScopedTempDir temp_dir;

  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  EnrollmentStorage storage(temp_dir.GetPath());
  FACE_ASSERT_OK(storage.WriteEnrollment(kUserId1, kData1));
  FACE_ASSERT_OK(storage.WriteEnrollment(kUserId2, kData2));

  FACE_ASSERT_OK_AND_ASSIGN(std::string data1,
                            storage.ReadEnrollment(kUserId1));
  EXPECT_EQ(data1, kData1);

  FACE_ASSERT_OK_AND_ASSIGN(std::string data2,
                            storage.ReadEnrollment(kUserId2));
  EXPECT_EQ(data2, kData2);

  // Overwrite kUserId1's data with kData2 and check that it has changed
  FACE_ASSERT_OK(storage.WriteEnrollment(kUserId1, kData2));
  FACE_ASSERT_OK_AND_ASSIGN(data1, storage.ReadEnrollment(kUserId1));
  EXPECT_EQ(data1, kData2);
}

}  // namespace
}  // namespace faced
