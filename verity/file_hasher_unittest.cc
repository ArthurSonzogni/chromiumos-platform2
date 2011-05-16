// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Tests for verity::FileHasher
#include "verity/file_hasher.h"

#include <base/basictypes.h>
#include <base/memory/scoped_ptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "verity/simple_file/mock_file.h"
#include "verity/utils.h"

using ::testing::_;  // wildcard mock matcher
using ::testing::AtLeast;  // Times modifier
using ::testing::DefaultValue;  // allow for easy default return value change
using ::testing::InSequence;
using ::testing::Invoke;  // mock Invoke action
using ::testing::Return;  // mock Return action

class FileHasherTest : public ::testing::Test {
 public:
  FileHasherTest() :
    src_(new simple_file::MockFile()),
    dst_(new simple_file::MockFile()) { }
  ~FileHasherTest() { }
  void SetUp() {
    hasher_.reset(new verity::FileHasher());
  }
 protected:
  scoped_ptr<simple_file::MockFile> src_;
  scoped_ptr<simple_file::MockFile> dst_;
  scoped_ptr<verity::FileHasher> hasher_;
};

static char last_digest_match[256] = { 0 };

MATCHER_P(DigestMatch, a, "given hexdigest matches binary digest arg") {
  char *hexdigest = new char[strlen(a) + 1];
  verity_utils::to_hex(hexdigest, arg, strlen(a)/2);
  bool ok = !strcmp(a, hexdigest);
  LOG(INFO) << "DigestMatcher: "
            << hexdigest << (ok? " == " : " != ") << a;
  delete [] hexdigest;
  // Store this away globally so we can easily re use it.
  // Later this needs to be supported by a MockFile.
  if (sizeof(last_digest_match) > strlen(a)/2) {
    memcpy(last_digest_match, arg, strlen(a)/2);
  }
  return ok;
}

#if 0
TEST_F(FileHasherTest, EndToEnd) {
  EXPECT_TRUE(false);
}
#endif
