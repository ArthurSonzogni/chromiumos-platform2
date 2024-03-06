// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.

#include <gtest/gtest.h>

#include "verity/dm_verity_table.h"

namespace verity {

namespace {

constexpr DmVerityTable::RootDigestType kRootDigest = {
    '2', '1', 'f', '0', '2', '6', '8', 'f', '4', 'a', '2', '9', '3',
    'd', '8', '1', '1', '0', '0', '7', '4', 'c', '6', '7', '8', 'a',
    '6', '5', '1', 'c', '6', '3', '8', 'd', '5', '6', 'a', '6', '1',
    '0', 'd', 'd', '2', '6', '6', '2', '9', '7', '5', 'a', '3', '5',
    'd', '4', '5', '1', 'd', '3', '2', '5', '8', '0', '1', '8', 0,
};

constexpr DmVerityTable::SaltType kSalt = {
    'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '2', '3', '4', '5', '6',
    '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '0',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd',
    'e', 'f', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '\0',
};

}  // namespace

TEST(DmVerityTableTest, ChromeOSFormatColocated) {
  DmVerityTable table(
      /*alg=*/"sha256",
      /*root_digest=*/kRootDigest,
      /*salt=*/kSalt,
      DmVerityTable::DevInfo{
          .dev = "ROOT_DEV",
          .block_count = 2,
      },
      DmVerityTable::DevInfo{
          .dev = "HASH_DEV",
      },
      /*hash_placement=*/DmVerityTable::HashPlacement::COLOCATED);
  auto s = table.Print(/*format=*/DmVerityTable::Format::CROS);
  ASSERT_TRUE(s);
  EXPECT_EQ(
      "0 16 verity payload=ROOT_DEV hashtree=HASH_DEV hashstart=16 "
      "alg=sha256 root_hexdigest=21f0268f4a293d8110074c678a651c638d"
      "56a610dd2662975a35d451d3258018 salt=abcdef0123456789abcdef01"
      "23456789abcdef0123456789abcdef0123456789",
      s.value());
}

TEST(DmVerityTableTest, ChromeOSFormatNotColocated) {
  DmVerityTable table(
      /*alg=*/"sha256",
      /*root_digest=*/kRootDigest,
      /*salt=*/kSalt,
      DmVerityTable::DevInfo{
          .dev = "ROOT_DEV",
          .block_count = 2,
      },
      DmVerityTable::DevInfo{
          .dev = "HASH_DEV",
      },
      /*hash_placement=*/DmVerityTable::HashPlacement::SEPARATE);
  auto s = table.Print(/*format=*/DmVerityTable::Format::CROS);
  ASSERT_TRUE(s);
  EXPECT_EQ(
      "0 16 verity payload=ROOT_DEV hashtree=HASH_DEV hashstart=0 "
      "alg=sha256 root_hexdigest=21f0268f4a293d8110074c678a651c638d"
      "56a610dd2662975a35d451d3258018 salt=abcdef0123456789abcdef01"
      "23456789abcdef0123456789abcdef0123456789",
      s.value());
}

TEST(DmVerityTableTest, VanillaFormatColocated) {
  DmVerityTable table(
      /*alg=*/"sha256",
      /*root_digest=*/kRootDigest,
      /*salt=*/kSalt,
      DmVerityTable::DevInfo{
          .dev = "ROOT_DEV",
          .block_count = 2,
      },
      DmVerityTable::DevInfo{
          .dev = "HASH_DEV",
      },
      /*hash_placement=*/DmVerityTable::HashPlacement::COLOCATED);
  auto s = table.Print(/*format=*/DmVerityTable::Format::VANILLA);
  ASSERT_TRUE(s);
  EXPECT_EQ(
      "0 16 verity 0 ROOT_DEV HASH_DEV 4096 4096 "
      "2 2 sha256 21f0268f4a293d8110074c678a651c638d"
      "56a610dd2662975a35d451d3258018 abcdef0123456789abcdef01"
      "23456789abcdef0123456789abcdef0123456789",
      s.value());
}

TEST(DmVerityTableTest, VanillaFormatNotColocated) {
  DmVerityTable table(
      /*alg=*/"sha256",
      /*root_digest=*/kRootDigest,
      /*salt=*/kSalt,
      DmVerityTable::DevInfo{
          .dev = "ROOT_DEV",
          .block_count = 2,
      },
      DmVerityTable::DevInfo{
          .dev = "HASH_DEV",
      },
      /*hash_placement=*/DmVerityTable::HashPlacement::SEPARATE);
  auto s = table.Print(/*format=*/DmVerityTable::Format::VANILLA);
  ASSERT_TRUE(s);
  EXPECT_EQ(
      "0 16 verity 0 ROOT_DEV HASH_DEV 4096 4096 "
      "2 0 sha256 21f0268f4a293d8110074c678a651c638d"
      "56a610dd2662975a35d451d3258018 abcdef0123456789abcdef01"
      "23456789abcdef0123456789abcdef0123456789",
      s.value());
}

TEST(DmVerityTableTest, ChromeOSFormatParse) {
  constexpr char kTable[] =
      ("0 16 verity payload=ROOT_DEV hashtree=HASH_DEV hashstart=16 "
       "alg=sha256 root_hexdigest=21f0268f4a293d8110074c678a651c638d"
       "56a610dd2662975a35d451d3258018 salt=abcdef0123456789abcdef01"
       "23456789abcdef0123456789abcdef0123456789");
  auto dm_verity_table =
      DmVerityTable::Parse(kTable, DmVerityTable::Format::CROS);
  ASSERT_TRUE(dm_verity_table);
  EXPECT_EQ(kTable,
            dm_verity_table->Print(DmVerityTable::Format::CROS).value_or(""));
}

TEST(DmVerityTableTest, ChromeOSFormatNoSaltParse) {
  constexpr char kTable[] =
      ("0 16 verity payload=ROOT_DEV hashtree=HASH_DEV hashstart=16 "
       "alg=sha256 root_hexdigest=21f0268f4a293d8110074c678a651c638d"
       "56a610dd2662975a35d451d3258018");
  auto dm_verity_table =
      DmVerityTable::Parse(kTable, DmVerityTable::Format::CROS);
  ASSERT_TRUE(dm_verity_table);
  EXPECT_EQ(kTable,
            dm_verity_table->Print(DmVerityTable::Format::CROS).value_or(""));
}

TEST(DmVerityTableTest, VanillaFormatParse) {
  constexpr char kTable[] =
      ("0 16 verity 0 ROOT_DEV HASH_DEV 4096 4096 "
       "2 2 sha256 21f0268f4a293d8110074c678a651c638d"
       "56a610dd2662975a35d451d3258018 abcdef0123456789abcdef01"
       "23456789abcdef0123456789abcdef0123456789");
  auto dm_verity_table =
      DmVerityTable::Parse(kTable, DmVerityTable::Format::VANILLA);
  ASSERT_TRUE(dm_verity_table);
  EXPECT_EQ(
      kTable,
      dm_verity_table->Print(DmVerityTable::Format::VANILLA).value_or(""));
}

TEST(DmVerityTableTest, VanillaFormatNoSaltParse) {
  constexpr char kTable[] =
      ("0 16 verity 0 ROOT_DEV HASH_DEV 4096 4096 "
       "2 2 sha256 21f0268f4a293d8110074c678a651c638d"
       "56a610dd2662975a35d451d3258018 -");
  auto dm_verity_table =
      DmVerityTable::Parse(kTable, DmVerityTable::Format::VANILLA);
  ASSERT_TRUE(dm_verity_table);
  EXPECT_EQ(
      kTable,
      dm_verity_table->Print(DmVerityTable::Format::VANILLA).value_or(""));
}

TEST(DmVerityTableTest, Getters) {
  constexpr char kAlg[] = "sha256";
  const DmVerityTable::DevInfo kDataDev{
      .dev = "ROOT_DEV",
      .block_count = 2,
  };
  const DmVerityTable::DevInfo kHashDev{
      .dev = "HASH_DEV",
  };
  constexpr auto kHashPlacement(DmVerityTable::HashPlacement::COLOCATED);
  const DmVerityTable kDmVerityTable(kAlg, kRootDigest, kSalt, kDataDev,
                                     kHashDev, kHashPlacement);
  EXPECT_EQ(kAlg, kDmVerityTable.GetAlgorithm());
  EXPECT_EQ(kRootDigest, kDmVerityTable.GetRootDigest());
  EXPECT_EQ(kSalt, kDmVerityTable.GetSalt());
  EXPECT_EQ(kDataDev, kDmVerityTable.GetDataDevice());
  EXPECT_EQ(kHashDev, kDmVerityTable.GetHashDevice());
  EXPECT_EQ(kHashPlacement, kDmVerityTable.GetHashPlacement());
}

}  // namespace verity
