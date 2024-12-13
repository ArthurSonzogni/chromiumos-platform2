// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gtest/gtest.h>

#include "login_manager/system_utils_impl.h"

namespace login_manager {

TEST(SystemUtilsTest, CorrectFileWrite) {
  base::ScopedTempDir tmpdir;
  base::FilePath scratch;
  ASSERT_TRUE(tmpdir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateTemporaryFileInDir(tmpdir.GetPath(), &scratch));

  std::string old_data("what");
  std::string new_data("ho, neighbor");

  ASSERT_TRUE(base::WriteFile(scratch, old_data));

  SystemUtilsImpl system_utils;
  ASSERT_TRUE(system_utils.WriteFileAtomically(
      scratch, base::as_byte_span(new_data), S_IRUSR | S_IWUSR | S_IROTH));
  std::string written_data;
  ASSERT_TRUE(base::ReadFileToString(scratch, &written_data));
  ASSERT_EQ(new_data, written_data);
}

}  // namespace login_manager
