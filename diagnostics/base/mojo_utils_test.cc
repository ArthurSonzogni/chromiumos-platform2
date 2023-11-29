// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/base/mojo_utils.h"

#include <memory>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>
#include <mojo/public/cpp/system/handle.h>

namespace diagnostics {
namespace {

TEST(RoutineUtilsTest, CreateMojoHandleAndRetrieveContent) {
  const std::string_view content("{\"key\": \"value\"}");

  mojo::ScopedHandle handle =
      CreateReadOnlySharedMemoryRegionMojoHandle(content);
  EXPECT_TRUE(handle.is_valid());

  auto shm_mapping =
      GetReadOnlySharedMemoryMappingFromMojoHandle(std::move(handle));
  ASSERT_TRUE(shm_mapping.IsValid());

  std::string_view actual(shm_mapping.GetMemoryAs<char>(),
                          shm_mapping.mapped_size());
  EXPECT_EQ(content, actual);
}

TEST(RoutineUtilsTest, GetReadOnlySharedMemoryRegionFromMojoInvalidHandle) {
  mojo::ScopedHandle handle;
  EXPECT_FALSE(handle.is_valid());

  auto shm_mapping =
      GetReadOnlySharedMemoryMappingFromMojoHandle(std::move(handle));
  EXPECT_FALSE(shm_mapping.IsValid());
}

TEST(RoutineUtilsTest, CreateReadOnlySharedMemoryFromEmptyContent) {
  mojo::ScopedHandle handle = CreateReadOnlySharedMemoryRegionMojoHandle("");
  // Cannot create valid handle using empty content line.
  EXPECT_FALSE(handle.is_valid());
}

}  // namespace
}  // namespace diagnostics
