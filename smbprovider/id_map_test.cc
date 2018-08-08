// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <gtest/gtest.h>

#include "smbprovider/id_map.h"

namespace smbprovider {

class IdMapTest : public testing::Test {
 public:
  IdMapTest() = default;
  ~IdMapTest() override = default;

 protected:
  void ExpectFound(int32_t id, std::string expected) {
    auto iter = map_.Find(id);
    EXPECT_NE(map_.End(), iter);
    EXPECT_TRUE(map_.Contains(id));
    EXPECT_EQ(expected, iter->second);
  }

  void ExpectNotFound(int32_t id) {
    auto iter = map_.Find(id);
    EXPECT_EQ(map_.End(), iter);
    EXPECT_FALSE(map_.Contains(id));
  }

  IdMap<const std::string> map_;
  DISALLOW_COPY_AND_ASSIGN(IdMapTest);
};

TEST_F(IdMapTest, FindOnEmpty) {
  EXPECT_EQ(0, map_.Count());
  ExpectNotFound(0);
}

TEST_F(IdMapTest, TestInsertandFind) {
  const std::string expected = "Foo";
  const int32_t id = map_.Insert(expected);

  EXPECT_GE(id, 0);
  ExpectFound(id, expected);
  EXPECT_EQ(1, map_.Count());
}

TEST_F(IdMapTest, TestInsertAndContains) {
  const std::string expected = "Foo";
  const int32_t id = map_.Insert(expected);

  EXPECT_GE(id, 0);
  EXPECT_TRUE(map_.Contains(id));
  EXPECT_FALSE(map_.Contains(id + 1));
}

TEST_F(IdMapTest, TestInsertandFindNonExistant) {
  const std::string expected = "Foo";
  const int32_t id = map_.Insert(expected);

  EXPECT_GE(id, 0);
  ExpectFound(id, expected);
  ExpectNotFound(id + 1);
}

TEST_F(IdMapTest, TestInsertMultipleAndFind) {
  const std::string expected1 = "Foo1";
  const std::string expected2 = "Foo2";
  const int32_t id1 = map_.Insert(expected1);
  EXPECT_EQ(1, map_.Count());
  const int32_t id2 = map_.Insert(expected2);
  EXPECT_EQ(2, map_.Count());

  // Both ids are >= 0 and not the same.
  EXPECT_GE(id1, 0);
  EXPECT_GE(id2, 0);
  EXPECT_NE(id1, id2);

  ExpectFound(id1, expected1);
  ExpectFound(id2, expected2);
}

TEST_F(IdMapTest, TestRemoveOnEmpty) {
  EXPECT_FALSE(map_.Remove(0));
}

TEST_F(IdMapTest, TestRemoveNonExistant) {
  const std::string expected = "Foo";
  const int32_t id = map_.Insert(expected);

  EXPECT_GE(id, 0);
  ExpectFound(id, expected);
  ExpectNotFound(id + 1);
  EXPECT_FALSE(map_.Remove(id + 1));
}

TEST_F(IdMapTest, TestInsertAndRemove) {
  const std::string expected = "Foo";
  const int32_t id = map_.Insert(expected);

  EXPECT_GE(id, 0);
  EXPECT_TRUE(map_.Contains(id));
  EXPECT_EQ(1, map_.Count());

  EXPECT_TRUE(map_.Remove(id));
  ExpectNotFound(id);
  EXPECT_EQ(0, map_.Count());
}

TEST_F(IdMapTest, TestInsertRemoveInsertRemove) {
  const std::string expected = "Foo";
  const int32_t id1 = map_.Insert(expected);

  EXPECT_GE(id1, 0);
  EXPECT_TRUE(map_.Contains(id1));
  EXPECT_EQ(1, map_.Count());

  EXPECT_TRUE(map_.Remove(id1));
  ExpectNotFound(id1);
  EXPECT_EQ(0, map_.Count());

  const int32_t id2 = map_.Insert(expected);
  EXPECT_GE(id2, 0);
  EXPECT_TRUE(map_.Contains(id2));
  EXPECT_EQ(1, map_.Count());

  EXPECT_TRUE(map_.Remove(id2));
  ExpectNotFound(id2);
  EXPECT_EQ(0, map_.Count());
}

TEST_F(IdMapTest, TestIdReuse) {
  const int32_t id1 = map_.Insert("Foo");
  const int32_t id2 = map_.Insert("Bar");

  EXPECT_GE(id1, 0);
  EXPECT_GE(id2, 0);
  EXPECT_NE(id1, id2);

  // Remove the id and check that it is reused.
  map_.Remove(id2);
  const int32_t id3 = map_.Insert("Baz");
  EXPECT_EQ(id3, id2);

  // Get another unused id.
  const int32_t id4 = map_.Insert("Qux");
  EXPECT_GE(id4, 0);
  EXPECT_NE(id1, id4);
  EXPECT_NE(id3, id4);
}

TEST_F(IdMapTest, TestInsertWithSpecificId) {
  const int32_t specific_id = 5;
  map_.InsertWithSpecificId(specific_id, "Foo");

  // Subsequent id's will be higher than the specific id.
  const int32_t id2 = map_.Insert("Bar");
  EXPECT_GT(id2, specific_id);

  // The specific id can be reused though.
  EXPECT_TRUE(map_.Remove(specific_id));
  const int32_t id3 = map_.Insert("Baz");
  EXPECT_EQ(specific_id, id3);
}

}  // namespace smbprovider
