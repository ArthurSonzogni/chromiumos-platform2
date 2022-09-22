// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "frame.h"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace ipp {
namespace {

TEST(Frame, Constructor1) {
  Frame frame;
  EXPECT_EQ(frame.OperationIdOrStatusCode(), 0);
  EXPECT_EQ(frame.RequestId(), 0);
  EXPECT_EQ(static_cast<int>(frame.VersionNumber()), 0);
  EXPECT_TRUE(frame.Data().empty());
  for (GroupTag gt : kGroupTags) {
    EXPECT_TRUE(frame.GetGroups(gt).empty());
  }
}

TEST(Frame, Constructor2) {
  const Frame frame(Operation::Activate_Printer, Version::_2_1, 123);
  EXPECT_EQ(frame.OperationId(), Operation::Activate_Printer);
  EXPECT_EQ(frame.RequestId(), 123);
  EXPECT_EQ(frame.VersionNumber(), Version::_2_1);
  EXPECT_TRUE(frame.Data().empty());
  for (GroupTag gt : kGroupTags) {
    auto groups = frame.GetGroups(gt);
    if (gt == GroupTag::operation_attributes) {
      ASSERT_EQ(groups.size(), 1);
      auto att = groups[0]->GetAttribute("attributes-charset");
      ASSERT_NE(att, nullptr);
      std::string value;
      ASSERT_TRUE(att->GetValue(&value));
      EXPECT_EQ(value, "utf-8");
      att = groups[0]->GetAttribute("attributes-natural-language");
      ASSERT_NE(att, nullptr);
      ASSERT_TRUE(att->GetValue(&value));
      EXPECT_EQ(value, "en-us");
    } else {
      EXPECT_TRUE(groups.empty());
    }
  }
}

TEST(Frame, Constructor2empty) {
  const Frame frame(Operation::Activate_Printer, Version::_2_1, 123, false);
  EXPECT_EQ(frame.OperationId(), Operation::Activate_Printer);
  EXPECT_EQ(frame.RequestId(), 123);
  EXPECT_EQ(frame.VersionNumber(), Version::_2_1);
  EXPECT_TRUE(frame.Data().empty());
  for (GroupTag gt : kGroupTags) {
    EXPECT_TRUE(frame.GetGroups(gt).empty());
  }
}

TEST(Frame, Constructor3) {
  Frame frame(Status::client_error_gone, Version::_1_0, 123);
  EXPECT_EQ(frame.StatusCode(), Status::client_error_gone);
  EXPECT_EQ(frame.RequestId(), 123);
  EXPECT_EQ(frame.VersionNumber(), Version::_1_0);
  EXPECT_TRUE(frame.Data().empty());
  for (GroupTag gt : kGroupTags) {
    auto groups = frame.GetGroups(gt);
    if (gt == GroupTag::operation_attributes) {
      ASSERT_EQ(groups.size(), 1);
      auto att = groups[0]->GetAttribute("attributes-charset");
      ASSERT_NE(att, nullptr);
      std::string value;
      ASSERT_TRUE(att->GetValue(&value));
      EXPECT_EQ(value, "utf-8");
      att = groups[0]->GetAttribute("attributes-natural-language");
      ASSERT_NE(att, nullptr);
      ASSERT_TRUE(att->GetValue(&value));
      EXPECT_EQ(value, "en-us");
      att = groups[0]->GetAttribute("status-message");
      ASSERT_NE(att, nullptr);
      ASSERT_TRUE(att->GetValue(&value));
      EXPECT_EQ(value, "client-error-gone");
    } else {
      EXPECT_TRUE(groups.empty());
    }
  }
}

TEST(Frame, Constructor3empty) {
  const Frame frame(Status::client_error_gone, Version::_2_1, 123, false);
  EXPECT_EQ(frame.StatusCode(), Status::client_error_gone);
  EXPECT_EQ(frame.RequestId(), 123);
  EXPECT_EQ(frame.VersionNumber(), Version::_2_1);
  EXPECT_TRUE(frame.Data().empty());
  for (GroupTag gt : kGroupTags) {
    EXPECT_TRUE(frame.GetGroups(gt).empty());
  }
}

TEST(Frame, Data) {
  Frame frame;
  std::vector<uint8_t> raw = {0x01, 0x02, 0x03, 0x04};
  EXPECT_EQ(frame.SetData(std::move(raw)), Code::kOK);
  EXPECT_EQ(frame.Data(), std::vector<uint8_t>({0x01, 0x02, 0x03, 0x04}));
  auto raw2 = frame.TakeData();
  EXPECT_EQ(raw2, std::vector<uint8_t>({0x01, 0x02, 0x03, 0x04}));
  EXPECT_TRUE(frame.Data().empty());
}

TEST(Frame, GetGroups) {
  Frame frame(Operation::Cancel_Job);
  EXPECT_EQ(frame.GetGroups(GroupTag::operation_attributes).size(), 1);
  EXPECT_EQ(frame.GetGroups(static_cast<GroupTag>(0x00)).size(), 0);
  EXPECT_EQ(frame.GetGroups(static_cast<GroupTag>(0x0f)).size(), 0);
}

TEST(Frame, GetGroupsConst) {
  const Frame frame(Operation::Cancel_Job);
  EXPECT_EQ(frame.GetGroups(GroupTag::operation_attributes).size(), 1);
  EXPECT_EQ(frame.GetGroups(static_cast<GroupTag>(0x00)).size(), 0);
  EXPECT_EQ(frame.GetGroups(static_cast<GroupTag>(0x0f)).size(), 0);
}

TEST(Frame, GetGroup) {
  Frame frame(Operation::Print_Job);
  EXPECT_NE(frame.GetGroup(GroupTag::operation_attributes, 0), nullptr);
  EXPECT_EQ(frame.GetGroup(static_cast<GroupTag>(0x03), 0), nullptr);
  EXPECT_EQ(frame.GetGroup(GroupTag::operation_attributes, 1), nullptr);
}

TEST(Frame, GetGroupConst) {
  const Frame frame(Operation::Print_Job);
  EXPECT_NE(frame.GetGroup(GroupTag::operation_attributes, 0), nullptr);
  EXPECT_EQ(frame.GetGroup(static_cast<GroupTag>(0x03), 0), nullptr);
  EXPECT_EQ(frame.GetGroup(GroupTag::operation_attributes, 1), nullptr);
}

TEST(Frame, AddGroup) {
  Frame frame(Operation::Cancel_Job, Version::_2_0);
  Collection* grp1 = nullptr;
  Collection* grp2 = nullptr;
  Collection* grp3 = nullptr;
  EXPECT_EQ(frame.AddGroup(GroupTag::document_attributes, &grp1), Code::kOK);
  EXPECT_EQ(frame.AddGroup(GroupTag::job_attributes, &grp2), Code::kOK);
  EXPECT_EQ(frame.AddGroup(GroupTag::document_attributes, &grp3), Code::kOK);
  EXPECT_NE(grp1, nullptr);
  EXPECT_NE(grp2, nullptr);
  EXPECT_NE(grp3, nullptr);
  EXPECT_EQ(grp1, frame.GetGroup(GroupTag::document_attributes, 0));
  EXPECT_EQ(grp2, frame.GetGroup(GroupTag::job_attributes, 0));
  EXPECT_EQ(grp3, frame.GetGroup(GroupTag::document_attributes, 1));
  EXPECT_EQ(frame.GetGroups(GroupTag::document_attributes),
            std::vector<Collection*>({grp1, grp3}));
  EXPECT_EQ(frame.GetGroups(GroupTag::job_attributes),
            std::vector<Collection*>({grp2}));
}

TEST(Frame, AddGroupErrorCodes) {
  Collection* grp = nullptr;
  Frame frame(Operation::Cancel_Job, Version::_2_0);
  EXPECT_EQ(frame.AddGroup(GroupTag::document_attributes, &grp), Code::kOK);
  EXPECT_NE(grp, nullptr);
  grp = nullptr;
  EXPECT_EQ(frame.AddGroup(static_cast<GroupTag>(0x10), &grp),
            Code::kInvalidGroupTag);
  EXPECT_EQ(grp, nullptr);
}

}  // namespace
}  // namespace ipp
