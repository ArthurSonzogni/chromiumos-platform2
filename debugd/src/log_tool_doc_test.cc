// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <set>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <gtest/gtest.h>
#include "debugd/src/log_tool.h"

namespace debugd {

TEST(LogToolDocTest, EntriesDocumented) {
  // Check if there are matching entries of the markdown document.
  auto categories = GetAllDebugTitlesForTest();
  std::set<base::StringPiece> documented_entries;
  std::vector<base::StringPiece> unsorted_documented_entries;
  constexpr char kLogEntriesMd[] = "docs/log_entries.md";

  base::FilePath markdown_filepath(
      base::FilePath(getenv("SRC")).Append(kLogEntriesMd));
  std::string mdfile;
  CHECK(base::ReadFileToString(markdown_filepath, &mdfile));

  for (const auto& line : base::SplitStringPiece(
           mdfile, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (line.substr(0, 3) == "## ") {
      unsorted_documented_entries.push_back(line.substr(3));
      documented_entries.insert(line.substr(3));
    }
  }
  CHECK_GE(documented_entries.size(), 2)
      << "Expecting at least 2 document entries but only found "
      << documented_entries.size();

  for (const auto& category : categories) {
    for (const auto& entry : category) {
      EXPECT_TRUE(documented_entries.find(entry) != documented_entries.end())
          << "Please add an entry for \"" << entry << "\" in " << kLogEntriesMd;
    }
  }

  auto it = std::is_sorted_until(unsorted_documented_entries.begin(),
                                 unsorted_documented_entries.end());
  EXPECT_TRUE(it == unsorted_documented_entries.end())
      << *it << " is not sorted in " << kLogEntriesMd;
}

TEST(LogToolDocTest, EntriesAreSorted) {
  // Check if entries of log_tool.cc are sorted.
  auto categories = GetAllDebugTitlesForTest();
  for (const auto& category : categories) {
    if (category.size() <= 1)
      continue;
    auto it = std::is_sorted_until(category.begin(), category.end());
    EXPECT_TRUE(it == category.end()) << *it << " is not sorted.";
  }
}

}  // namespace debugd
