// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "runtime_probe/utils/file_test_utils.h"
#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

class FileUtilsTest : public BaseFileTest {
 protected:
  void SetUp() {
    CreateTestRoot();
    SetFile("a/b1/c1", "c1");
    SetFile("a/b1/c2", "c2");
    SetFile("a/b1/c3", "c3");
    SetFile("a/b2/c1", "c1");
    SetFile("a/b3/c1", "c1");
    SetFile("s/*", "");
    SetFile("s/[test]", "");
  }

  std::vector<base::FilePath> ToFilePathVector(
      const std::vector<std::string>& in) {
    std::vector<base::FilePath> res;
    for (const auto& path : in) {
      res.push_back(GetPathUnderRoot(path));
    }
    // Make sure the order is the same as the glob results.
    sort(res.begin(), res.end());
    return res;
  }
};

namespace {

// Sort the result since the result is not guaranteed to be sorted.
std::vector<base::FilePath> GlobForTest(const base::FilePath& pattern) {
  auto res = Glob(pattern);
  sort(res.begin(), res.end());
  return res;
}

}  // namespace

TEST_F(FileUtilsTest, MapFilesToDict) {
  const std::vector<std::string> keys{"c1", "c2"};
  const std::vector<std::string> optional_keys{"c3", "c4"};
  const std::vector<std::string> res_files{"c1", "c2", "c3"};
  base::Value res(base::Value::Type::DICTIONARY);
  for (const auto& file : res_files) {
    res.SetKey(file, base::Value(file));
  }
  auto path = GetPathUnderRoot("a/b1");
  EXPECT_EQ(MapFilesToDict(path, keys, optional_keys), res);
}

TEST_F(FileUtilsTest, Glob) {
  auto path = GetPathUnderRoot("a/*/?1");
  const std::vector<std::string> res{"a/b1/c1", "a/b2/c1", "a/b3/c1"};
  EXPECT_EQ(GlobForTest(path), ToFilePathVector(res));
}

TEST_F(FileUtilsTest, GlobOneFile) {
  auto path = GetPathUnderRoot("a/b2/c1");
  const std::vector<std::string> res{"a/b2/c1"};
  EXPECT_EQ(GlobForTest(path), ToFilePathVector(res));
}

TEST_F(FileUtilsTest, GlobDir) {
  auto path = GetPathUnderRoot("a/b2/");
  const std::vector<std::string> res{"a/b2"};
  EXPECT_EQ(GlobForTest(path), ToFilePathVector(res));
}

TEST_F(FileUtilsTest, GlobNoFile) {
  auto path = GetPathUnderRoot("x/y/z");
  const std::vector<std::string> res{};
  EXPECT_EQ(GlobForTest(path), ToFilePathVector(res));
}

TEST_F(FileUtilsTest, GlobSpecial1) {
  auto path = GetPathUnderRoot("s/[*]");
  const std::vector<std::string> res{"s/*"};
  EXPECT_EQ(GlobForTest(path), ToFilePathVector(res));
}

TEST_F(FileUtilsTest, GlobSpecial2) {
  auto path = GetPathUnderRoot("s/[[]test]");
  const std::vector<std::string> res{"s/[test]"};
  EXPECT_EQ(GlobForTest(path), ToFilePathVector(res));
}

}  // namespace runtime_probe
