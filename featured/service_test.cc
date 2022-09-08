// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/dcheck_is_on.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "featured/service.h"

namespace featured {

TEST(FeatureCommand, FileExistsTest) {
  base::FilePath file;
  ASSERT_TRUE(base::CreateTemporaryFile(&file));

  FileExistsCommand c(file.MaybeAsASCII());
  ASSERT_TRUE(c.Execute());

  FileNotExistsCommand c2(file.MaybeAsASCII());
  ASSERT_FALSE(c2.Execute());
}

TEST(FeatureCommand, FileNotExistsTest) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());

  base::FilePath file(dir.GetPath().Append("non-existent"));

  FileNotExistsCommand c(file.MaybeAsASCII());
  ASSERT_TRUE(c.Execute());

  FileExistsCommand c2(file.MaybeAsASCII());
  ASSERT_FALSE(c2.Execute());
}

}  // namespace featured
