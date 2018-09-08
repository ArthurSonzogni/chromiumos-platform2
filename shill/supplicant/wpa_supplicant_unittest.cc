// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/supplicant/wpa_supplicant.h"

#include <gtest/gtest.h>

#include "shill/mock_log.h"

using std::map;
using std::string;
using testing::_;
using testing::EndsWith;

namespace shill {

class WPASupplicantTest : public testing::Test {
 public:
  WPASupplicantTest() {}
  virtual ~WPASupplicantTest() {}

 protected:
  KeyValueStore property_map_;
};

TEST_F(WPASupplicantTest, ExtractRemoteCertificationEmpty) {
  string subject;
  uint32_t depth = 0;
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _, EndsWith("no depth parameter.")));
  EXPECT_FALSE(WPASupplicant::ExtractRemoteCertification(
      property_map_, &subject, &depth));
  EXPECT_EQ("", subject);
  EXPECT_EQ(0, depth);
}

TEST_F(WPASupplicantTest, ExtractRemoteCertificationDepthOnly) {
  string subject;
  const uint32_t kDepthValue = 100;
  uint32_t depth = kDepthValue - 1;
  property_map_.SetUint(WPASupplicant::kInterfacePropertyDepth, kDepthValue);
  ScopedMockLog log;
  EXPECT_CALL(log,
              Log(logging::LOG_ERROR, _, EndsWith("no subject parameter.")));
  EXPECT_FALSE(WPASupplicant::ExtractRemoteCertification(
      property_map_, &subject, &depth));
  EXPECT_EQ("", subject);
  EXPECT_NE(kDepthValue, depth);
}

TEST_F(WPASupplicantTest, ExtractRemoteCertificationSubjectOnly) {
  const char kSubjectName[] = "subject-name";
  string subject;
  uint32_t depth = 0;
  property_map_.SetString(WPASupplicant::kInterfacePropertySubject,
                          kSubjectName);
  ScopedMockLog log;
  EXPECT_CALL(log, Log(logging::LOG_ERROR, _, EndsWith("no depth parameter.")));
  EXPECT_FALSE(WPASupplicant::ExtractRemoteCertification(
      property_map_, &subject, &depth));
  EXPECT_EQ("", subject);
  EXPECT_EQ(0, depth);
}

TEST_F(WPASupplicantTest, ExtractRemoteCertificationSubjectAndDepth) {
  const char kSubjectName[] = "subject-name";
  string subject;
  const uint32_t kDepthValue = 100;
  uint32_t depth = 0;
  property_map_.SetString(WPASupplicant::kInterfacePropertySubject,
                          kSubjectName);
  property_map_.SetUint(WPASupplicant::kInterfacePropertyDepth, kDepthValue);
  EXPECT_TRUE(WPASupplicant::ExtractRemoteCertification(
      property_map_, &subject, &depth));
  EXPECT_EQ(kSubjectName, subject);
  EXPECT_EQ(kDepthValue, depth);
}

}  // namespace shill
