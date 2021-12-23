// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "installer/efi_boot_management.cc"
#include "installer/efivar.cc"

using testing::Contains;
using testing::Key;
using testing::Pair;
using testing::Return;
using testing::UnorderedElementsAre;

namespace {

// Actual device data to satisfy checks libefivar does internally.
// Grabbed these from my vm.
const uint8_t kExampleDataQemuDisk[] =
    "\x01\x00\x00\x00\x1E\x00\x55\x00\x45\x00\x46\x00\x49\x00\x20\x00\x51\x00"
    "\x45\x00\x4D\x00\x55\x00\x20\x00\x48\x00\x41\x00\x52\x00\x44\x00\x44\x00"
    "\x49\x00\x53\x00\x4B\x00\x20\x00\x51\x00\x4D\x00\x30\x00\x30\x00\x30\x00"
    "\x30\x00\x31\x00\x20\x00\x00\x00\x02\x01\x0C\x00\xD0\x41\x03\x0A\x00\x00"
    "\x00\x00\x01\x01\x06\x00\x01\x01\x03\x01\x08\x00\x00\x00\x00\x00\x7F\xFF"
    "\x04\x00\x4E\xAC\x08\x81\x11\x9F\x59\x4D\x85\x0E\xE2\x1A\x52\x2C\x59\xB2";
const char kExampleDescriptionQemuDisk[] = "UEFI QEMU HARDDISK QM00001 ";
const uint8_t kExamplePathQemuDisk[] =
    "\x02\x01\x0C\x00\xD0\x41\x03\x0A\x00\x00\x00\x00\x01\x01\x06\x00\x01\x01"
    "\x03\x01\x08\x00\x00\x00\x00\x00\x7F\xFF\x04";

const uint8_t kExampleDataQemuPXE[] =
    "\x01\x00\x00\x00\x56\x00\x55\x00\x45\x00\x46\x00\x49\x00\x20\x00\x50\x00"
    "\x58\x00\x45\x00\x76\x00\x34\x00\x20\x00\x28\x00\x4d\x00\x41\x00\x43\x00"
    "\x3a\x00\x41\x00\x41\x00\x41\x00\x41\x00\x41\x00\x41\x00\x30\x00\x35\x00"
    "\x34\x00\x37\x00\x37\x00\x37\x00\x29\x00\x00\x00\x02\x01\x0c\x00\xd0\x41"
    "\x03\x0a\x00\x00\x00\x00\x01\x01\x06\x00\x00\x03\x03\x0b\x25\x00\xaa\xaa"
    "\xaa\x05\x47\x77\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x03\x0c\x1b\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x7f\xff\x04\x00\x4e\xac\x08\x81\x11\x9f\x59\x4d\x85\x0e"
    "\xe2\x1a\x52\x2c\x59\xb2";
const char kExampleDescriptionQemuPXE[] = "UEFI PXEv4 (MAC:AAAAAA054777)";
const uint8_t kExamplePathQemuPXE[] =
    "\x02\x01\x0c\x00\xd0\x41\x03\x0a\x00\x00\x00\x00\x01\x01\x06\x00\x00\x03"
    "\x03\x0b\x25\x00\xaa\xaa\xaa\x05\x47\x77\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x01\x03\x0c\x1b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7f\xff\x04";

const uint8_t kExampleDataLinux[] =
    "\x01\x00\x00\x00\x5C\x00\x4C\x00\x69\x00\x6E\x00\x75\x00\x78\x00\x00\x00"
    "\x04\x01\x2A\x00\x01\x00\x00\x00\x00\xA0\x4E\x00\x00\x00\x00\x00\x81\x30"
    "\x80\x00\x00\x00\x00\x00\x5A\x0C\x9F\x8D\x75\x4C\x44\x09\x86\xCD\x6E\x51"
    "\x01\xAC\xE7\x5A\x02\x02\x04\x04\x2E\x00\x5C\x00\x45\x00\x46\x00\x49\x00"
    "\x5C\x00\x47\x00\x65\x00\x6E\x00\x74\x00\x6F\x00\x6F\x00\x5C\x00\x67\x00"
    "\x72\x00\x75\x00\x62\x00\x2E\x00\x65\x00\x66\x00\x69\x00\x00\x00\x7F\xFF"
    "\x04";
const char kExampleDescriptionLinux[] = "Linux";

const uint8_t kExampleDataCros[] =
    "\x01\x00\x00\x00\x5E\x00\x43\x00\x68\x00\x72\x00\x6F\x00\x6D\x00\x69\x00"
    "\x75\x00\x6D\x00\x20\x00\x4F\x00\x53\x00\x00\x00\x04\x01\x2A\x00\x0C\x00"
    "\x00\x00\x00\x90\x01\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x00\x00"
    "\x34\xEB\x97\xB6\x17\xB3\x43\xA6\x97\xDE\x49\x70\x9D\xF0\xB6\x03\x02\x02"
    "\x04\x04\x30\x00\x5C\x00\x65\x00\x66\x00\x69\x00\x5C\x00\x62\x00\x6F\x00"
    "\x6F\x00\x74\x00\x5C\x00\x62\x00\x6F\x00\x6F\x00\x74\x00\x78\x00\x36\x00"
    "\x34\x00\x2E\x00\x65\x00\x66\x00\x69\x00\x00\x00\x7F\xFF\x04";
const char kExampleDescriptionCros[] = "Chromium OS";
const uint8_t kExamplePathCros[] =
    "\x04\x01\x2A\x00\x0C\x00\x00\x00\x00\x90\x01\x00\x00\x00\x00\x00\x00\x00"
    "\x02\x00\x00\x00\x00\x00\x34\xEB\x97\xB6\x17\xB3\x43\xA6\x97\xDE\x49\x70"
    "\x9D\xF0\xB6\x03\x02\x02\x04\x04\x30\x00\x5C\x00\x65\x00\x66\x00\x69\x00"
    "\x5C\x00\x62\x00\x6F\x00\x6F\x00\x74\x00\x5C\x00\x62\x00\x6F\x00\x6F\x00"
    "\x74\x00\x78\x00\x36\x00\x34\x00\x2E\x00\x65\x00\x66\x00\x69\x00\x00\x00"
    "\x7F\xFF\x04";

const uint8_t kExampleBootOrder123[] = "\x01\x00\x02\x00\x03\x00";
const uint8_t kExampleBootOrderDuplicate[] = "\x01\x00\x02\x00\x01\x00";
const uint8_t kRawBootOrderSentinel[] = "\xBA\xAD\xF0\x0D";

class EfiVarFake : public EfiVarInterface {
 public:
  bool EfiVariablesSupported() override { return true; }

  base::Optional<std::string> GetNextVariableName() override {
    if (variable_names_.size() == 0) {
      return base::nullopt;
    }

    base::Optional<std::string> result(variable_names_.back());
    variable_names_.pop_back();
    return result;
  }

  bool GetVariable(const std::string& name,
                   Bytes& output_data,
                   size_t* data_size) override {
    auto pair = data_.find(name);

    if (pair == data_.end()) {
      return false;
    }

    auto value = pair->second;

    *data_size = value.size();
    uint8_t* data_ptr = reinterpret_cast<uint8_t*>(malloc(value.size()));
    std::copy(value.begin(), value.end(), data_ptr);
    output_data.reset(data_ptr);

    return true;
  }

  bool SetVariable(const std::string& name,
                   uint32_t attributes,
                   std::vector<uint8_t>& data) override {
    // store in data_ for checking later.
    data_.insert_or_assign(name, data);
    return true;
  }

  bool DelVariable(const std::string& name) override {
    data_.erase(name);
    return true;
  }

  bool GenerateFileDevicePathFromEsp(
      const std::string& device_path,
      int esp_partition,
      const std::string& boot_file,
      std::vector<uint8_t>& efidp_data) override {
    // Put our "cros" data in there
    efidp_data.assign(kExamplePathCros,
                      kExamplePathCros + sizeof(kExamplePathCros));
    return true;
  }

  void SetData(const std::map<std::string, std::vector<uint8_t>>& data) {
    data_ = data;
    for (const auto& [key, _] : data_) {
      variable_names_.push_back(key);
    }
  }

  std::map<std::string, std::vector<uint8_t>> data_;
  // Hang onto these for GetNextVariableName
  std::vector<std::string> variable_names_;
};

// Helpers for quick/clear construction of test data.
std::vector<uint8_t> VecU8From(const uint8_t* ex, const size_t size) {
  return std::vector<uint8_t>(ex, ex + size);
}

std::pair<EfiBootNumber, EfiBootEntryContents> BootPair(
    uint16_t num,
    const std::string& desc,
    const std::vector<uint8_t>& device_path) {
  return std::make_pair(EfiBootNumber(num),
                        EfiBootEntryContents(desc, device_path));
}

BootOrder BootOrderFromExample(const uint8_t* ex, const size_t size) {
  EfiVarFake efivar;
  BootOrder boot_order;

  efivar.data_.insert({"BootOrder", VecU8From(ex, size)});

  boot_order.Load(efivar);

  return boot_order;
}

std::vector<uint8_t> BootOrderData(const std::vector<uint16_t>& input) {
  return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(input.data()),
                              reinterpret_cast<const uint8_t*>(input.data()) +
                                  (input.size() * sizeof(uint16_t)));
}

}  // namespace

class EfiBootEntryContentsTest : public ::testing::Test {};

TEST(EfiBootEntryContentsTest, IsCrosEntry) {
  EfiBootEntryContents entryQemuDisk(kExampleDescriptionQemuDisk, {});
  EfiBootEntryContents entryLinux(kExampleDescriptionLinux, {});
  EfiBootEntryContents entryCros(kExampleDescriptionCros, {});

  EXPECT_FALSE(entryQemuDisk.IsCrosEntry());
  EXPECT_FALSE(entryLinux.IsCrosEntry());

  EXPECT_TRUE(entryCros.IsCrosEntry());
}

TEST(EfiBootEntryContentsTest, Equals) {
  EfiBootEntryContents entryLinux(
      kExampleDescriptionQemuPXE,
      VecU8From(kExamplePathQemuPXE, sizeof(kExamplePathQemuPXE)));
  EfiBootEntryContents entryCrosA(
      kExampleDescriptionCros,
      VecU8From(kExamplePathCros, sizeof(kExamplePathCros)));
  EfiBootEntryContents entryCrosB(
      kExampleDescriptionCros,
      VecU8From(kExamplePathCros, sizeof(kExamplePathCros)));

  EXPECT_FALSE(entryCrosA == entryLinux);
  EXPECT_TRUE(entryCrosA == entryCrosB);
}

class BootOrderTest : public ::testing::Test {
 protected:
  BootOrderTest() : efivar_(), boot_order_() {}

  EfiVarFake efivar_;
  BootOrder boot_order_;
};

TEST_F(BootOrderTest, Load) {
  efivar_.SetData({{"BootOrder", VecU8From(kExampleBootOrder123,
                                           sizeof(kExampleBootOrder123))}});

  boot_order_.Load(efivar_);

  EXPECT_EQ(boot_order_.Data(), std::vector<uint16_t>({1, 2, 3}));
}

TEST_F(BootOrderTest, LoadNothing) {
  efivar_.SetData({});

  boot_order_.Load(efivar_);

  EXPECT_EQ(boot_order_.Data(), std::vector<uint16_t>());
}

TEST_F(BootOrderTest, NoWriteNeeded) {
  efivar_.SetData({{"BootOrder", VecU8From(kExampleBootOrder123,
                                           sizeof(kExampleBootOrder123))}});

  boot_order_.Load(efivar_);

  // Clear with sentinel
  efivar_.SetData({{"BootOrder", VecU8From(kRawBootOrderSentinel,
                                           sizeof(kRawBootOrderSentinel))}});

  bool result = boot_order_.WriteIfNeeded(efivar_);
  EXPECT_TRUE(result);
  // Confirm it's still set to the sentinel.
  EXPECT_THAT(
      efivar_.data_,
      Contains(Pair("BootOrder", VecU8From(kRawBootOrderSentinel,
                                           sizeof(kRawBootOrderSentinel)))));
}

TEST_F(BootOrderTest, Remove) {
  efivar_.SetData({{"BootOrder", VecU8From(kExampleBootOrder123,
                                           sizeof(kExampleBootOrder123))}});

  boot_order_.Load(efivar_);
  boot_order_.Remove(EfiBootNumber(1));

  bool result = boot_order_.WriteIfNeeded(efivar_);
  EXPECT_TRUE(result);
  EXPECT_THAT(efivar_.data_,
              Contains(Pair("BootOrder", BootOrderData({2, 3}))));
}

TEST_F(BootOrderTest, RemoveDuplicate) {
  efivar_.SetData(
      {{"BootOrder", VecU8From(kExampleBootOrderDuplicate,
                               sizeof(kExampleBootOrderDuplicate))}});

  boot_order_.Load(efivar_);
  boot_order_.Remove(EfiBootNumber(1));

  bool result = boot_order_.WriteIfNeeded(efivar_);
  EXPECT_TRUE(result);
  EXPECT_THAT(efivar_.data_, Contains(Pair("BootOrder", BootOrderData({2}))));
}

TEST_F(BootOrderTest, Add) {
  efivar_.SetData({{"BootOrder", VecU8From(kExampleBootOrder123,
                                           sizeof(kExampleBootOrder123))}});

  boot_order_.Load(efivar_);
  boot_order_.Add(EfiBootNumber(4));

  bool result = boot_order_.WriteIfNeeded(efivar_);
  EXPECT_TRUE(result);
  EXPECT_THAT(efivar_.data_,
              Contains(Pair("BootOrder", BootOrderData({4, 1, 2, 3}))));
}

TEST_F(BootOrderTest, Contains) {
  efivar_.SetData({{"BootOrder", VecU8From(kExampleBootOrder123,
                                           sizeof(kExampleBootOrder123))}});

  boot_order_.Load(efivar_);

  EXPECT_FALSE(boot_order_.Contains(EfiBootNumber(0)));
  EXPECT_TRUE(boot_order_.Contains(EfiBootNumber(1)));
  EXPECT_TRUE(boot_order_.Contains(EfiBootNumber(3)));
  EXPECT_FALSE(boot_order_.Contains(EfiBootNumber(9)));
}

class EfiBootManagerTest : public ::testing::Test {
 protected:
  EfiBootManagerTest() : efivar_(), efi_boot_manager_(efivar_) {}

  EfiVarFake efivar_;
  EfiBootManager efi_boot_manager_;
};

TEST_F(EfiBootManagerTest, LoadEntry) {
  efivar_.SetData({{"BootFFFF", VecU8From(kExampleDataQemuDisk,
                                          sizeof(kExampleDataQemuDisk))}});

  auto result = efi_boot_manager_.LoadEntry(EfiBootNumber(0xFFFF));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->Description(), kExampleDescriptionQemuDisk);
  EXPECT_EQ(result->DevicePath(),
            VecU8From(kExamplePathQemuDisk, sizeof(kExamplePathQemuDisk)));
}

TEST_F(EfiBootManagerTest, LoadNonDiskEntry) {
  efivar_.SetData({{"BootFFFF", VecU8From(kExampleDataQemuPXE,
                                          sizeof(kExampleDataQemuPXE))}});

  auto result = efi_boot_manager_.LoadEntry(EfiBootNumber(0xFFFF));

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->Description(), kExampleDescriptionQemuPXE);
  EXPECT_EQ(result->DevicePath(),
            VecU8From(kExamplePathQemuPXE, sizeof(kExamplePathQemuPXE)));
}

TEST_F(EfiBootManagerTest, LoadEntryFail) {
  // Don't inject anything, so it fails.
  auto result = efi_boot_manager_.LoadEntry(EfiBootNumber(0xFFFF));

  EXPECT_FALSE(result.has_value());
}

TEST_F(EfiBootManagerTest, EntryRoundTrip) {
  efivar_.SetData(
      {{"BootFFFF", VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))}});

  auto contents = efi_boot_manager_.LoadEntry(EfiBootNumber(0xFFFF));
  ASSERT_TRUE(contents.has_value());

  // Clear so that we can check what gets written to it.
  efivar_.data_.clear();

  bool result =
      efi_boot_manager_.WriteEntry(EfiBootNumber(0xFFFF), contents.value());
  ASSERT_TRUE(result);
  EXPECT_THAT(efivar_.data_,
              Contains(Pair("BootFFFF", VecU8From(kExampleDataLinux,
                                                  sizeof(kExampleDataLinux)))));
}

TEST_F(EfiBootManagerTest, NextAvailableBootNum) {
  base::Optional<EfiBootNumber> boot_num;
  // Test an empty list.
  efi_boot_manager_.SetEntries({});
  boot_num = efi_boot_manager_.NextAvailableBootNum();
  EXPECT_TRUE(boot_num.has_value());
  EXPECT_EQ(boot_num->Number(), 0);
  // Test that it picks an available number.
  efi_boot_manager_.SetEntries({BootPair(0, {}, {})});
  boot_num = efi_boot_manager_.NextAvailableBootNum();
  EXPECT_TRUE(boot_num.has_value());
  EXPECT_EQ(boot_num->Number(), 1);
  // Test that it picks the lowest available.
  efi_boot_manager_.SetEntries(
      {BootPair(0, {}, {}), BootPair(1, {}, {}), BootPair(9, {}, {})});
  boot_num = efi_boot_manager_.NextAvailableBootNum();
  EXPECT_TRUE(boot_num.has_value());
  EXPECT_EQ(boot_num->Number(), 2);

  // Test that it handles none available.
  // No hardware we're likely to run on will be able to hit this state.
  EfiBootManager::EntriesMap full;
  for (uint16_t num = 0; num < 0xFFFF; ++num) {
    full.emplace(EfiBootNumber(num), EfiBootEntryContents({}, {}));
  }

  efi_boot_manager_.SetEntries(full);
  boot_num = efi_boot_manager_.NextAvailableBootNum();
  EXPECT_FALSE(boot_num.has_value());
}

TEST_F(EfiBootManagerTest, FindContentsInBootOrder) {
  const EfiBootEntryContents desired(
      kCrosEfiDescription,
      VecU8From(kExamplePathCros, sizeof(kExamplePathCros)));
  base::Optional<EfiBootNumber> entry;

  // Desired not present in entries
  efi_boot_manager_.SetBootOrder(
      BootOrderFromExample(kExampleBootOrder123, sizeof(kExampleBootOrder123)));
  efi_boot_manager_.SetEntries({
      BootPair(1, kExampleDescriptionQemuDisk,
               VecU8From(kExamplePathQemuDisk, sizeof(kExamplePathQemuDisk))),
      BootPair(2, kExampleDescriptionQemuPXE,
               VecU8From(kExamplePathQemuPXE, sizeof(kExamplePathQemuPXE))),
  });
  entry = efi_boot_manager_.FindContentsInBootOrder(desired);
  EXPECT_FALSE(entry.has_value());

  // Desired is present in entries, but not boot order
  efi_boot_manager_.SetBootOrder(
      BootOrderFromExample(kExampleBootOrder123, sizeof(kExampleBootOrder123)));
  efi_boot_manager_.SetEntries({
      BootPair(1, kExampleDescriptionQemuDisk,
               VecU8From(kExamplePathQemuDisk, sizeof(kExamplePathQemuDisk))),
      BootPair(2, kExampleDescriptionQemuPXE,
               VecU8From(kExamplePathQemuPXE, sizeof(kExamplePathQemuPXE))),
      BootPair(4, kExampleDescriptionCros,
               VecU8From(kExamplePathCros, sizeof(kExamplePathCros))),
  });
  entry = efi_boot_manager_.FindContentsInBootOrder(desired);
  EXPECT_FALSE(entry.has_value());

  // Desired is present in entries and boot order
  efi_boot_manager_.SetBootOrder(
      BootOrderFromExample(kExampleBootOrder123, sizeof(kExampleBootOrder123)));
  efi_boot_manager_.SetEntries({
      BootPair(1, kExampleDescriptionQemuDisk,
               VecU8From(kExamplePathQemuDisk, sizeof(kExamplePathQemuDisk))),
      BootPair(2, kExampleDescriptionQemuPXE,
               VecU8From(kExamplePathQemuPXE, sizeof(kExamplePathQemuPXE))),
      BootPair(3, kExampleDescriptionCros,
               VecU8From(kExamplePathCros, sizeof(kExamplePathCros))),
  });
  EfiBootNumber entry_num(3);

  entry = efi_boot_manager_.FindContentsInBootOrder(desired);
  EXPECT_TRUE(entry.has_value());
  EXPECT_EQ(entry.value().Number(), 3);
}

TEST_F(EfiBootManagerTest, FindContents) {
  const EfiBootEntryContents desired(
      kCrosEfiDescription,
      VecU8From(kExamplePathCros, sizeof(kExamplePathCros)));
  base::Optional<EfiBootNumber> entry;

  // Desired not present in entries
  efi_boot_manager_.SetEntries({
      BootPair(1, {},
               VecU8From(kExamplePathQemuDisk, sizeof(kExamplePathQemuDisk))),
      BootPair(2, {},
               VecU8From(kExamplePathQemuPXE, sizeof(kExamplePathQemuPXE))),
  });
  entry = efi_boot_manager_.FindContents(desired);
  EXPECT_FALSE(entry.has_value());

  // Desired is present in entries
  efi_boot_manager_.SetEntries({
      BootPair(1, kExampleDescriptionQemuDisk,
               VecU8From(kExamplePathQemuDisk, sizeof(kExamplePathQemuDisk))),
      BootPair(2, kExampleDescriptionQemuPXE,
               VecU8From(kExamplePathQemuPXE, sizeof(kExamplePathQemuPXE))),
      BootPair(3, kExampleDescriptionCros,
               VecU8From(kExamplePathCros, sizeof(kExamplePathCros))),
  });
  EfiBootNumber entry_num(3);

  entry = efi_boot_manager_.FindContents(desired);
  EXPECT_TRUE(entry.has_value());
  EXPECT_EQ(entry.value().Number(), 3);
}

TEST_F(EfiBootManagerTest, RemoveAllCrosEntries) {
  efi_boot_manager_.SetBootOrder(
      BootOrderFromExample(kExampleBootOrder123, sizeof(kExampleBootOrder123)));

  // Set up to also empty the boot order.
  efi_boot_manager_.SetEntries({
      BootPair(0x0001, kCrosEfiDescription, {}),
      BootPair(0xA000, "Chromium", {}),
      BootPair(0x0002, kCrosEfiDescription, {}),
      BootPair(0xB000, "ChromiumOS", {}),
      BootPair(0xC000, "something", {}),
      BootPair(0x0003, kCrosEfiDescription, {}),
      BootPair(0xD000, "Linux", {}),
      BootPair(0xE000, "Linux", {}),
  });

  // Set these to check for erasure/nonerasure
  efivar_.SetData({
      {"Boot0001", {}},
      {"Boot0002", {}},
      {"Boot0003", {}},
      {"BootA000", {}},
      {"BootB000", {}},
      {"BootC000", {}},
      {"BootD000", {}},
      {"BootE000", {}},
  });

  efi_boot_manager_.RemoveAllCrosEntries();

  EXPECT_THAT(
      efivar_.data_,
      UnorderedElementsAre(Key("BootA000"), Key("BootB000"), Key("BootC000"),
                           Key("BootD000"), Key("BootE000")));

  EXPECT_TRUE(efi_boot_manager_.Order().Data().empty());
}

TEST_F(EfiBootManagerTest, UpdateEfiBootEntries_NoBootEntries) {
  efivar_.SetData({{"BootOrder", {}}});
  InstallConfig install_config;

  bool success = efi_boot_manager_.UpdateEfiBootEntries(install_config, 64);

  EXPECT_TRUE(success);
  EXPECT_THAT(efivar_.data_, Contains(Pair("BootOrder", BootOrderData({0}))));
  EXPECT_THAT(efivar_.data_, Contains(Key("Boot0000")));
}

TEST_F(EfiBootManagerTest, UpdateEfiBootEntries_NoCrosEntry) {
  efivar_.SetData({
      {"BootOrder", BootOrderData({0})},
      {"Boot0000", VecU8From(kExampleDataQemuPXE, sizeof(kExampleDataQemuPXE))},
      {"Boot0001", VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))},
  });
  InstallConfig install_config;

  bool success = efi_boot_manager_.UpdateEfiBootEntries(install_config, 64);

  EXPECT_TRUE(success);
  EXPECT_THAT(efivar_.data_,
              UnorderedElementsAre(
                  Pair("BootOrder", BootOrderData({2, 0})),
                  Pair("Boot0000", VecU8From(kExampleDataQemuPXE,
                                             sizeof(kExampleDataQemuPXE))),
                  Pair("Boot0001",
                       VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))),
                  Key("Boot0002")));
}

TEST_F(EfiBootManagerTest, UpdateEfiBootEntries_CrosEntryNotInBootOrder) {
  efivar_.SetData({
      {"BootOrder", BootOrderData({1, 0})},
      {"Boot0000", VecU8From(kExampleDataQemuPXE, sizeof(kExampleDataQemuPXE))},
      {"Boot0001", VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))},
      {"Boot0002", VecU8From(kExampleDataCros, sizeof(kExampleDataCros))},
  });
  InstallConfig install_config;

  bool success = efi_boot_manager_.UpdateEfiBootEntries(install_config, 64);

  EXPECT_TRUE(success);
  EXPECT_THAT(efivar_.data_,
              UnorderedElementsAre(
                  Pair("BootOrder", BootOrderData({2, 1, 0})),
                  Pair("Boot0000", VecU8From(kExampleDataQemuPXE,
                                             sizeof(kExampleDataQemuPXE))),
                  Pair("Boot0001",
                       VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))),
                  Pair("Boot0002",
                       VecU8From(kExampleDataCros, sizeof(kExampleDataCros)))));
}

TEST_F(EfiBootManagerTest, UpdateEfiBootEntries_CrosInBootOrder) {
  efivar_.SetData({
      {"BootOrder", BootOrderData({1, 0, 2})},
      {"Boot0000", VecU8From(kExampleDataQemuPXE, sizeof(kExampleDataQemuPXE))},
      {"Boot0001", VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))},
      {"Boot0002", VecU8From(kExampleDataCros, sizeof(kExampleDataCros))},
  });
  InstallConfig install_config;

  bool success = efi_boot_manager_.UpdateEfiBootEntries(install_config, 64);

  EXPECT_TRUE(success);
  EXPECT_THAT(efivar_.data_,
              UnorderedElementsAre(
                  Pair("BootOrder", BootOrderData({1, 0, 2})),
                  Pair("Boot0000", VecU8From(kExampleDataQemuPXE,
                                             sizeof(kExampleDataQemuPXE))),
                  Pair("Boot0001",
                       VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))),
                  Pair("Boot0002",
                       VecU8From(kExampleDataCros, sizeof(kExampleDataCros)))));
}

TEST_F(EfiBootManagerTest, UpdateEfiBootEntries_ExcessCrosEntries) {
  efivar_.SetData({
      {"BootOrder", BootOrderData({1, 0, 2})},
      {"Boot0001", VecU8From(kExampleDataCros, sizeof(kExampleDataCros))},
      {"Boot0002", VecU8From(kExampleDataQemuPXE, sizeof(kExampleDataQemuPXE))},
      {"Boot0003", VecU8From(kExampleDataCros, sizeof(kExampleDataCros))},
      {"Boot0004", VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))},
      {"Boot0005", VecU8From(kExampleDataCros, sizeof(kExampleDataCros))},
  });
  InstallConfig install_config;

  bool success = efi_boot_manager_.UpdateEfiBootEntries(install_config, 64);

  EXPECT_TRUE(success);
  EXPECT_THAT(efivar_.data_,
              UnorderedElementsAre(
                  Pair("BootOrder", BootOrderData({1, 0, 2})),
                  Pair("Boot0002", VecU8From(kExampleDataQemuPXE,
                                             sizeof(kExampleDataQemuPXE))),
                  Pair("Boot0004",
                       VecU8From(kExampleDataLinux, sizeof(kExampleDataLinux))),
                  Pair(testing::_,
                       VecU8From(kExampleDataCros, sizeof(kExampleDataCros)))));
}
