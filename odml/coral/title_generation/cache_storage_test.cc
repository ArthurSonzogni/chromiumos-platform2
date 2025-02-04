// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/title_generation/cache_storage.h"

#include <memory>
#include <unordered_set>

#include <base/containers/lru_cache.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace coral {

namespace {

using testing::UnorderedElementsAre;

constexpr size_t kCacheMaxSize = 4;

constexpr char kSet1Group1Title[] = "Travel to Japan";
constexpr char kSet1Group1Entity1[] =
    "JNTO - Official Tourism Guide for Japan Travel";
constexpr char kSet1Group1Entity2[] = "Cheap flights to Tokyo";
constexpr char kSet1Group2Title[] = "C++ Reference";
constexpr char kSet1Group2Entity1[] = "gMock Cookbook";
constexpr char kSet1Group2Entity2[] = "std::multiset";
constexpr char kSet1Group2Entity3[] = "gMock Cheat Sheet | GoogleTest";

constexpr char kSet2Group1Title[] = "Gardening Tips";
constexpr char kSet2Group1Entity1[] = "How to Grow Roses";
constexpr char kSet2Group1Entity2[] = "Best Fertilizer for Tomatoes";
constexpr char kSet2Group2Title[] = "Baking Recipes";
constexpr char kSet2Group2Entity1[] = "Chocolate Cake Recipe";
constexpr char kSet2Group2Entity2[] = "Sourdough Bread Starter";

}  // namespace

class CacheStorageTest : public testing::Test {
 public:
  void SetUp() override {
    temp_dir_ = std::make_unique<base::ScopedTempDir>();
    ASSERT_TRUE(temp_dir_->CreateUniqueTempDir());
    cache_storage_ = std::make_unique<TitleCacheStorage>(temp_dir_->GetPath());
  };

  void TearDown() override {
    temp_dir_.reset();
    cache_storage_.reset();
  };

 protected:
  base::HashingLRUCache<std::string, TitleCacheEntry> GetContentSet1() {
    base::HashingLRUCache<std::string, TitleCacheEntry> result =
        base::HashingLRUCache<std::string, TitleCacheEntry>(kCacheMaxSize);

    std::unordered_multiset<std::string> entities1;
    entities1.insert(kSet1Group1Entity1);
    entities1.insert(kSet1Group1Entity2);
    result.Put(kSet1Group1Title,
               TitleCacheEntry{.entity_titles = std::move(entities1)});

    std::unordered_multiset<std::string> entities2;
    entities2.insert(kSet1Group2Entity1);
    entities2.insert(kSet1Group2Entity2);
    entities2.insert(kSet1Group2Entity3);
    result.Put(kSet1Group2Title,
               TitleCacheEntry{.entity_titles = std::move(entities2)});
    return result;
  }

  void AssertContentSet1(
      const base::HashingLRUCache<std::string, TitleCacheEntry>& cache) {
    ASSERT_EQ(cache.size(), 2);
    auto itr = cache.begin();
    ASSERT_EQ(itr->first, kSet1Group1Title);
    ASSERT_THAT(itr->second.entity_titles,
                UnorderedElementsAre(kSet1Group1Entity1, kSet1Group1Entity2));

    itr++;
    ASSERT_EQ(itr->first, kSet1Group2Title);
    ASSERT_THAT(itr->second.entity_titles,
                UnorderedElementsAre(kSet1Group2Entity1, kSet1Group2Entity2,
                                     kSet1Group2Entity3));
  }

  base::HashingLRUCache<std::string, TitleCacheEntry> GetContentSet2() {
    base::HashingLRUCache<std::string, TitleCacheEntry> result =
        base::HashingLRUCache<std::string, TitleCacheEntry>(kCacheMaxSize);

    std::unordered_multiset<std::string> entities1;
    entities1.insert(kSet2Group1Entity1);
    entities1.insert(kSet2Group1Entity2);
    result.Put(kSet2Group1Title,
               TitleCacheEntry{.entity_titles = std::move(entities1)});

    std::unordered_multiset<std::string> entities2;
    entities2.insert(kSet2Group2Entity1);
    entities2.insert(kSet2Group2Entity2);
    result.Put(kSet2Group2Title,
               TitleCacheEntry{.entity_titles = std::move(entities2)});
    return result;
  }

  void AssertContentSet2(
      const base::HashingLRUCache<std::string, TitleCacheEntry>& cache) {
    ASSERT_EQ(cache.size(), 2);
    auto itr = cache.begin();
    ASSERT_EQ(itr->first, kSet2Group1Title);
    ASSERT_THAT(itr->second.entity_titles,
                UnorderedElementsAre(kSet2Group1Entity1, kSet2Group1Entity2));

    itr++;
    ASSERT_EQ(itr->first, kSet2Group2Title);
    ASSERT_THAT(itr->second.entity_titles,
                UnorderedElementsAre(kSet2Group2Entity1, kSet2Group2Entity2));
  }

  base::FilePath GetPath(odml::SessionStateManagerInterface::User user) {
    return temp_dir_->GetPath().Append(user.hash).Append("coral").Append(
        "title_cache");
  }

  odml::SessionStateManagerInterface::User user1_ = {
      .name = "test", .hash = "0123456789abcde0123456789abcde"};
  odml::SessionStateManagerInterface::User user2_ = {
      .name = "example", .hash = "aaaaaaaabbbbbbbb0000000011111111"};
  std::unique_ptr<base::ScopedTempDir> temp_dir_;
  std::unique_ptr<TitleCacheStorage> cache_storage_;
};

TEST_F(CacheStorageTest, Success) {
  base::HashingLRUCache<std::string, TitleCacheEntry> content1 =
      GetContentSet1();
  base::HashingLRUCache<std::string, TitleCacheEntry> loaded_content1 =
      base::HashingLRUCache<std::string, TitleCacheEntry>(kCacheMaxSize);

  ASSERT_TRUE(cache_storage_->Save(user1_, content1));
  ASSERT_TRUE(cache_storage_->Load(user1_, loaded_content1));

  AssertContentSet1(loaded_content1);

  base::FilePath path1 = GetPath(user1_);
  EXPECT_TRUE(base::PathExists(path1));
}

TEST_F(CacheStorageTest, MultiuserSuccess) {
  base::HashingLRUCache<std::string, TitleCacheEntry> content1 =
      GetContentSet1();
  base::HashingLRUCache<std::string, TitleCacheEntry> content2 =
      GetContentSet2();
  base::HashingLRUCache<std::string, TitleCacheEntry> loaded_content1(
      kCacheMaxSize);
  base::HashingLRUCache<std::string, TitleCacheEntry> loaded_content2(
      kCacheMaxSize);

  ASSERT_TRUE(cache_storage_->Save(user1_, content1));
  ASSERT_TRUE(cache_storage_->Save(user2_, content2));

  ASSERT_TRUE(cache_storage_->Load(user1_, loaded_content1));
  ASSERT_TRUE(cache_storage_->Load(user2_, loaded_content2));

  AssertContentSet1(loaded_content1);
  AssertContentSet2(loaded_content2);
}

TEST_F(CacheStorageTest, EmptyFileSuccess) {
  base::HashingLRUCache<std::string, TitleCacheEntry> loaded_content(
      kCacheMaxSize);

  ASSERT_TRUE(cache_storage_->Load(user1_, loaded_content));

  EXPECT_EQ(loaded_content.size(), 0);
}

TEST_F(CacheStorageTest, CorruptFile) {
  base::HashingLRUCache<std::string, TitleCacheEntry> content1 =
      GetContentSet1();
  base::HashingLRUCache<std::string, TitleCacheEntry> loaded_content1 =
      base::HashingLRUCache<std::string, TitleCacheEntry>(kCacheMaxSize);

  ASSERT_TRUE(cache_storage_->Save(user1_, content1));
  ASSERT_TRUE(cache_storage_->Load(user1_, loaded_content1));

  base::FilePath path1 = GetPath(user1_);
  ASSERT_TRUE(base::WriteFile(path1, "corrupted"));

  ASSERT_FALSE(cache_storage_->Load(user1_, loaded_content1));
  EXPECT_EQ(loaded_content1.size(), 0);

  ASSERT_TRUE(cache_storage_->Save(user1_, content1));
  ASSERT_TRUE(cache_storage_->Load(user1_, loaded_content1));
  AssertContentSet1(loaded_content1);
}

}  // namespace coral
