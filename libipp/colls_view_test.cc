// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "colls_view.h"

#include <string>
#include <vector>

#include "attribute.h"
#include "frame.h"
#include <gtest/gtest.h>

namespace ipp {
namespace {

TEST(CollsViewTest, FrameGroups) {
  Frame frame = Frame(Operation::Cancel_Job);
  EXPECT_EQ(frame.Groups(GroupTag::operation_attributes).size(), 1);
  EXPECT_EQ(frame.Groups(static_cast<GroupTag>(0x00)).size(), 0);
  EXPECT_EQ(frame.Groups(static_cast<GroupTag>(0x0f)).size(), 0);
  EXPECT_FALSE(frame.Groups(GroupTag::operation_attributes).empty());
  EXPECT_TRUE(frame.Groups(static_cast<GroupTag>(0x00)).empty());
  EXPECT_TRUE(frame.Groups(static_cast<GroupTag>(0x0f)).empty());
  EXPECT_EQ(frame.Groups(static_cast<GroupTag>(0x00)).begin(),
            frame.Groups(static_cast<GroupTag>(0x00)).end());
  EXPECT_EQ(frame.Groups(static_cast<GroupTag>(0x0f)).cbegin(),
            frame.Groups(static_cast<GroupTag>(0x0f)).cend());
}

TEST(CollsViewTest, FrameGroupsConst) {
  const Frame frame = Frame(Operation::Cancel_Job);
  EXPECT_EQ(frame.Groups(GroupTag::operation_attributes).size(), 1);
  EXPECT_EQ(frame.Groups(static_cast<GroupTag>(0x00)).size(), 0);
  EXPECT_EQ(frame.Groups(static_cast<GroupTag>(0x0f)).size(), 0);
  EXPECT_FALSE(frame.Groups(GroupTag::operation_attributes).empty());
  EXPECT_TRUE(frame.Groups(static_cast<GroupTag>(0x00)).empty());
  EXPECT_TRUE(frame.Groups(static_cast<GroupTag>(0x0f)).empty());
  EXPECT_EQ(frame.Groups(static_cast<GroupTag>(0x00)).begin(),
            frame.Groups(static_cast<GroupTag>(0x00)).end());
  EXPECT_EQ(frame.Groups(static_cast<GroupTag>(0x0f)).cbegin(),
            frame.Groups(static_cast<GroupTag>(0x0f)).cend());
}

TEST(CollsViewTest, AttributeColls) {
  Frame frame = Frame(Operation::Cancel_Job);
  Collection* coll = frame.GetGroup(GroupTag::operation_attributes);
  std::vector<Collection*> attr_colls(4, nullptr);
  Code result = coll->AddAttr("test", attr_colls);
  ASSERT_EQ(result, Code::kOK);
  Attribute* attr = coll->GetAttribute("test");
  ASSERT_TRUE(attr);
  size_t index = 0;
  for (Collection& subcoll : attr->Colls()) {
    EXPECT_EQ(attr_colls[index], &subcoll);
    EXPECT_EQ(attr_colls[index], &attr->Colls()[index]);
    ++index;
  }
}

TEST(CollsViewTest, AttributeCollsConst) {
  Frame frame = Frame(Operation::Cancel_Job);
  Collection* coll = frame.GetGroup(GroupTag::operation_attributes);
  std::vector<Collection*> attr_colls(4, nullptr);
  Code result = coll->AddAttr("test", attr_colls);
  ASSERT_EQ(result, Code::kOK);
  const Attribute* attr = coll->GetAttribute("test");
  ASSERT_TRUE(attr);
  size_t index = 0;
  for (const Collection& subcoll : attr->Colls()) {
    EXPECT_EQ(attr_colls[index], &subcoll);
    EXPECT_EQ(attr_colls[index], &attr->Colls()[index]);
    ++index;
  }
}

TEST(CollsViewTest, FrameGroupsReverseIteration) {
  Frame frame = Frame(Operation::Cancel_Job);
  Collection* colls[3];
  frame.AddGroup(GroupTag::job_attributes, &colls[0]);
  frame.AddGroup(GroupTag::job_attributes, &colls[1]);
  frame.AddGroup(GroupTag::job_attributes, &colls[2]);
  CollsView::iterator it = frame.Groups(GroupTag::job_attributes).end();
  CollsView::const_iterator itc;
  itc = it;
  int index = 3;
  while (it != frame.Groups(GroupTag::job_attributes).begin()) {
    --index;
    EXPECT_TRUE(it == itc);
    EXPECT_TRUE(itc == it);
    EXPECT_FALSE(it != itc);
    EXPECT_FALSE(itc != it);
    EXPECT_EQ(colls[index], &*(--it));
    EXPECT_EQ(colls[index], &*(--itc));
  }
  EXPECT_EQ(index, 0);
  EXPECT_EQ(itc, frame.Groups(GroupTag::job_attributes).cbegin());
}

TEST(CollsViewTest, FrameGroupsReverseIterationConst) {
  Frame frame = Frame(Operation::Cancel_Job);
  Collection* colls[3];
  frame.AddGroup(GroupTag::job_attributes, &colls[0]);
  frame.AddGroup(GroupTag::job_attributes, &colls[1]);
  frame.AddGroup(GroupTag::job_attributes, &colls[2]);
  const Frame& frame2 = frame;
  ConstCollsView::const_iterator itc =
      frame2.Groups(GroupTag::job_attributes).end();
  int index = 3;
  while (itc != frame.Groups(GroupTag::job_attributes).begin()) {
    --index;
    EXPECT_EQ(colls[index], &*(--itc));
  }
  EXPECT_EQ(index, 0);
  EXPECT_EQ(itc, frame.Groups(GroupTag::job_attributes).cbegin());
}

}  // namespace
}  // namespace ipp
