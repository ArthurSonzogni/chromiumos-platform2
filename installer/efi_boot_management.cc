// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This implements management of EFI boot entries for systems where we manage
// those entries (UEFI systems not running Chrome OS firmware).
//
// The boot path on generic UEFI systems starts with the firmware deciding which
// efi binary to run. There is some variation in how this is implemented, and
// while most firmware will by default look at the GPT for an EFI System
// Partition and find the appropriate file located at
// `/efi/boot/boot{ia32|x64}.efi`, there are some implementations that don't.
// To ensure that we boot correctly after install on those systems we need to
// actively manage the boot entries.
//
// EFI boot selection is managed by a set of EFI variables.
// * Boot0000 through BootFFFF contain data about specific boot options that can
//   be tried or presented to the user.
// * BootOrder contains an ordered list of Boot#### entries to be tried when
//   booting, e.g. "try to boot from entry 2, and if that fails try entry 0"

#include <limits>
#include <map>
#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/containers/cxx20_erase.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "installer/efi_boot_management.h"
#include "installer/efivar.h"
#include "installer/inst_util.h"

namespace {
// Description of the managed boot entry.
// TODO(tbrandston): b/215430733 for making this overridable.
const char kCrosEfiDescription[] = "Chromium OS";

// The name of the efi variable where Boot order is stored.
const char kBootOrder[] = "BootOrder";

// The base for our standard error message.
const char kCantEnsureBoot[] = "Can't ensure successful boot: ";

// Returns true if the passed string matches the "Boot####" format:
// starts with "Boot", followed by four hex digits, with nothing trailing.
// Returns false otherwise.
bool IsBootNum(const std::string& name) {
  return (name.size() == 8 &&
          base::StartsWith(name, "Boot", base::CompareCase::SENSITIVE) &&
          // Safe because of the size() check.
          base::IsHexDigit(name[4]) && base::IsHexDigit(name[5]) &&
          base::IsHexDigit(name[6]) && base::IsHexDigit(name[7]));
}

// Get the size of the current EFI platform.
// Returns nullopt if the size could not be determined.
base::Optional<int> GetEfiPlatformSize() {
  const base::FilePath size_file("/sys/firmware/efi/fw_platform_size");

  // Read the EFI platform size to determine which loader to configure. It must
  // match the EFI implementation from the firmware not the running kernel.
  std::string size_string;
  if (!base::ReadFileToString(size_file, &size_string)) {
    // The proper target cannot be determined.
    // EFI services are likely not available.
    return base::nullopt;
  }

  int size;
  base::TrimWhitespaceASCII(size_string, base::TRIM_ALL, &size_string);
  if (!base::StringToInt(size_string, &size)) {
    return base::nullopt;
  }
  // Sanity check the size. It should only be one of these.
  if (size != 64 && size != 32) {
    return base::nullopt;
  }

  return size;
}

// EFI boot entries are named/numbered with the format Boot####,
// with 4 uppercase hex digits as the numeric portion.
// This class is a minimal wrapper around a boot entry number.
class EfiBootNumber {
 public:
  // Comparator for using EfiBootNumber as a key in a std::map.
  struct Comparator {
    bool operator()(const EfiBootNumber& a, const EfiBootNumber& b) const {
      return a.Number() < b.Number();
    }
  };

  explicit EfiBootNumber(uint16_t num) : boot_num_(num) {
    boot_name_ = base::StringPrintf("Boot%04X", boot_num_);
  }

  static base::Optional<EfiBootNumber> FromName(const std::string& name) {
    if (!IsBootNum(name)) {
      return base::nullopt;
    }

    const std::string hex_part = name.substr(4, 4);
    // There's no HexStringToUInt16 or similar, so we'll just cram a `uint32_t`
    // into our `uint16_t`, knowing that for all compliant UEFI implementations
    // this'll be fine. (Also, we're only converting four chars of hex, so it'll
    // never exceed 16 bits).
    uint32_t num;
    const bool success = base::HexStringToUInt(hex_part, &num);
    if (success) {
      const EfiBootNumber boot_num(base::checked_cast<uint16_t>(num));
      return boot_num;
    }

    return base::nullopt;
  }

  std::string Name() const { return boot_name_; }

  uint16_t Number() const { return boot_num_; }

 private:
  uint16_t boot_num_;
  std::string boot_name_;
};

// EFI boot entries contain attributes, a description/label, a device path,
// and optional data. We only care about the label and the path.
class EfiBootEntryContents {
 public:
  EfiBootEntryContents(const std::string& description,
                       const std::vector<uint8_t>& device_path)
      : description_(description), device_path_(device_path) {}

  // Checks if this is an entry we manage by comparing against our
  // description constant.
  bool IsCrosEntry() const {
    return description_ == std::string(kCrosEfiDescription);
  }

  bool operator==(const EfiBootEntryContents& other) const {
    return description_ == other.description_ &&
           device_path_ == other.device_path_;
  }

  std::string ToString() const {
    return base::StringPrintf(
        "description: '%s'\npath_data: %s", description_.c_str(),
        base::HexEncode(device_path_.data(), device_path_.size()).c_str());
  }

  std::string Description() const { return description_; }

  std::vector<uint8_t> DevicePath() const { return device_path_; }

 private:
  // This is sometimes called 'description', sometimes 'label':
  // The user-friendly name of the entry.
  std::string description_;

  // device_path_ stores the data represented by libefivar's efidp.
  // In our case it will describe the hardware location of the storage media,
  // plus the on-disk location of the efi file to load.
  std::vector<uint8_t> device_path_;
};

// A wrapper around the BootOrder EFI variable,
// an ordered list of boot entries to be tried, stored as 16-bit uints.
class BootOrder {
 public:
  BootOrder() = default;

  // Read and store the data, an array of 16-bit uints.
  // If we can't read it, we'll need to write a new one.
  void Load(EfiVarInterface& efivar) {
    EfiVarInterface::Bytes data;
    size_t data_size;
    if (efivar.GetVariable(kBootOrder, data, &data_size)) {
      boot_order_.assign(reinterpret_cast<uint16_t*>(data.get()),
                         reinterpret_cast<uint16_t*>(data.get()) +
                             (data_size / sizeof(uint16_t)));

      // Happy-path logging of the loaded order. If things go wrong later this
      // can make it much easier to see why.
      LOG(INFO) << "Loaded BootOrder: " << ToString();
    } else {
      // We couldn't read the boot order, so we need to write a new one.
      boot_order_.clear();
      needs_write_ = true;

      LOG(INFO) << "Creating new BootOrder.";
    }
  }

  // Write the data back to the EFI variable, but only if
  // we've made any modifications to the boot order.
  // Returns false on errors writing the data, true otherwise.
  bool WriteIfNeeded(EfiVarInterface& efivar) {
    if (!needs_write_) {
      LOG(INFO) << "BootOrder: No write needed.";
      return true;
    }

    // Copy into `uint8_t`s for writing out.
    std::vector<uint8_t> out;
    out.assign(reinterpret_cast<uint8_t*>(boot_order_.data()),
               reinterpret_cast<uint8_t*>(boot_order_.data()) +
                   (boot_order_.size() * sizeof(uint16_t)));
    if (!efivar.SetVariable(kBootOrder, kBootVariableAttributes, out)) {
      // SetVariable logs errors, but lets add our view of the boot order:
      LOG(INFO) << "Unwritten BootOrder: " << ToString();
      return false;
    }

    needs_write_ = false;
    return true;
  }

  bool Contains(const EfiBootNumber& entry) const {
    return base::Contains(boot_order_, entry.Number());
  }

  // Adds an entry to the beginning of the boot order, making a write necessary.
  void Add(const EfiBootNumber& entry) {
    boot_order_.insert(boot_order_.begin(), entry.Number());
    needs_write_ = true;
  }

  // Completely removes an entry from boot order, making a write necessary if
  // the entry was actually present.
  void Remove(const EfiBootNumber& entry) {
    // Only need to write back out if we successfully erase anything.
    if (base::Erase(boot_order_, entry.Number()) > 0) {
      needs_write_ = true;
    }
  }

  std::string ToString() const {
    std::string str;
    for (const auto x : boot_order_) {
      base::StringAppendF(&str, "%04X ", x);
    }
    return str;
  }

  std::vector<uint16_t> Data() const { return boot_order_; }

 private:
  std::vector<uint16_t> boot_order_;
  bool needs_write_ = false;
};

// Manages the list of EFI boot entries and the BootOrder.
class EfiBootManager {
 public:
  using EntriesMap =
      std::map<EfiBootNumber, EfiBootEntryContents, EfiBootNumber::Comparator>;

  explicit EfiBootManager(EfiVarInterface& efivar) : efivar_(&efivar) {}

  // Wrapper around libefivar's variable iteration to filter only Boot* entries.
  // Returns each Boot entry name in turn until all are read, nullopt after.
  base::Optional<EfiBootNumber> GetNextBootNum() {
    base::Optional<std::string> name;
    while ((name = efivar_->GetNextVariableName())) {
      base::Optional<EfiBootNumber> entry_number =
          EfiBootNumber::FromName(name.value());
      if (entry_number) {
        return entry_number;
      }
    }

    return base::nullopt;
  }

  // Load all the Boot* entries into our map.
  // Returns false if any boot number doesn't meet our expectations or if any
  // entry fails to load, returning true when all entries are stored.
  bool LoadBootEntries() {
    base::Optional<EfiBootNumber> entry_number;
    while ((entry_number = GetNextBootNum())) {
      base::Optional<EfiBootEntryContents> entry_contents =
          LoadEntry(entry_number.value());
      if (!entry_contents) {
        // LoadEntry logs errors for us.
        return false;
      }

      entries_.emplace(entry_number.value(), entry_contents.value());
    }

    return true;
  }

  // Loads the data for a single boot entry, returning it if correctly loaded.
  // Returns nullopt on error.
  base::Optional<EfiBootEntryContents> LoadEntry(const EfiBootNumber& number) {
    EfiVarInterface::Bytes data;
    size_t data_size;
    if (!efivar_->GetVariable(number.Name(), data, &data_size)) {
      // GetVariable logs errors for us.
      return base::nullopt;
    }

    std::string description = efivar_->LoadoptDesc(data.get(), data_size);
    std::vector<uint8_t> device_path =
        efivar_->LoadoptPath(data.get(), data_size);

    return base::Optional<EfiBootEntryContents>(std::in_place, description,
                                                device_path);
  }

  // Writes the boot entry contents to a boot number.
  // Returns false for errors writing, true otherwise.
  bool WriteEntry(const EfiBootNumber& number,
                  const EfiBootEntryContents& contents) {
    std::vector<uint8_t> entry_data;
    // Format the entry data:

    // UEFI spec v2.9 section 3.1.3 lists the possible attributes.
    // 1 means "active". We always create active entries.
    const uint32_t attributes = 1;
    auto device_path = contents.DevicePath();
    auto description = contents.Description();
    if (!efivar_->LoadoptCreate(attributes, device_path, description,
                                &entry_data)) {
      LOG(ERROR) << "Error formatting entry contents for " << number.Name();
      return false;
    }

    if (!efivar_->SetVariable(number.Name(), kBootVariableAttributes,
                              entry_data)) {
      // SetVariable logs errors for us.
      return false;
    }

    return true;
  }

  // Deletes the entry from disk and if successful removes it from the boot
  // order. Returns false for errors deleting from disk, true otherwise.
  bool RemoveEntry(const EfiBootNumber& number) {
    if (!efivar_->DelVariable(number.Name())) {
      // DelVariable logs errors for us.
      return false;
    }

    boot_order_.Remove(number);

    return true;
  }

  // Attempts to define the boot entry we want, for matching against or writing.
  // Determines the device path and the description we want, based on:
  // * disk
  // * partition
  // * 32/64-bit EFI
  // Returns nullopt for any failure to collect this info.
  base::Optional<EfiBootEntryContents> BuildDesiredEntry(
      const Partition& boot_dev, int efi_size) {
    // Select the target boot file based on the platform.
    std::string boot_file = "/efi/boot/bootx64.efi";
    if (efi_size == 32) {
      boot_file = "/efi/boot/bootia32.efi";
    }

    std::vector<uint8_t> efidp;

    if (!efivar_->GenerateFileDevicePathFromEsp(
            boot_dev.base_device(), boot_dev.number(), boot_file, efidp)) {
      LOG(ERROR)
          << "Can't decide on desired entry: couldn't determine device path";
      return base::nullopt;
    }

    return EfiBootEntryContents(kCrosEfiDescription, efidp);
  }

  // Returns an entry with desired contents that also appears in the boot order,
  // if one can be found. nullopt otherwise.
  base::Optional<EfiBootNumber> FindContentsInBootOrder(
      const EfiBootEntryContents& desired_contents) {
    for (const auto num : boot_order_.Data()) {
      const EfiBootNumber entry(num);

      auto value = entries_.find(entry);
      if (value != entries_.end() && value->second == desired_contents) {
        return entry;
      }
    }

    return base::nullopt;
  }

  // Returns an entry with desired contents, if one can be found.
  // nullopt otherwise.
  base::Optional<EfiBootNumber> FindContents(
      const EfiBootEntryContents& desired_contents) {
    for (auto const& [key, value] : entries_) {
      if (value == desired_contents) {
        return key;
      }
    }

    return base::nullopt;
  }

  // Best-effort removal from disk and boot order for all entries with
  // "our description", i.e. managed by us. We only do best-effort because
  // entries left behind shouldn't interfere with future boots.
  void RemoveAllCrosEntries() {
    auto entry_iter = entries_.begin();
    while (entry_iter != entries_.end()) {
      if (entry_iter->second.IsCrosEntry()) {
        // Best effort removal, including from boot order.
        LOG(INFO) << "Trying to remove " << entry_iter->first.Name();
        if (RemoveEntry(entry_iter->first)) {
          // Drop from container if successful so we know the bootnum is
          // available.
          entry_iter = entries_.erase(entry_iter);

          // The `erase` increments our iter for us.
          continue;
        }
      }
      entry_iter++;
    }
  }

  // Finds the lowest available boot number, returning it if found and an empty
  // optional if all 65536 boot numbers are taken (which shouldn't happen on
  // any hardware I'm aware of).
  base::Optional<EfiBootNumber> NextAvailableBootNum() {
    // Four hex chars fit perfectly in a `uint16_t`.
    uint16_t free_num = 0;
    uint16_t max = std::numeric_limits<decltype(free_num)>::max();

    for (free_num = 0; free_num < max; ++free_num) {
      EfiBootNumber entry(free_num);
      if (!base::Contains(entries_, entry)) {
        return entry;
      }
    }
    return base::nullopt;
  }

  // This is the high level logic of how we maintain our boot entries:
  // 1. Figure out what an entry pointing at our install would look like.
  //    This should be the same for slot A/B.
  // 2. Look for an existing entry that matches it.
  //    If found make sure it's in the boot order.
  // 3. Remove any "extra" entries that have the same description,
  //    assuming that we're responsible for managing all entries with our name.
  // 4. If no existing entry found then make one:
  //    - Pick the lowest available boot number.
  //    - Write an entry pointing at our install to that number.
  //    - Add it to the boot order.
  bool UpdateEfiBootEntries(const InstallConfig& install_config, int efi_size) {
    if (!LoadBootEntries()) {
      LOG(ERROR) << kCantEnsureBoot << "need to know what boot entries exist.";
      return false;
    }

    boot_order_.Load(*efivar_);

    // Figure out what a "correct" boot entry would look like.
    const base::Optional<EfiBootEntryContents> desired_contents =
        BuildDesiredEntry(install_config.boot, efi_size);
    if (!desired_contents) {
      LOG(ERROR) << kCantEnsureBoot
                 << "need to know what our entry should look like.";
      return false;
    }
    LOG(INFO) << "Looking for an entry matching: "
              << desired_contents->ToString();

    base::Optional<EfiBootNumber> found_entry =
        FindContentsInBootOrder(desired_contents.value());

    if (!found_entry) {
      found_entry = FindContents(desired_contents.value());

      if (found_entry) {
        // We found a good entry, but it's not in the boot order. Fix that.
        boot_order_.Add(found_entry.value());
      }
    }

    if (found_entry) {
      LOG(INFO) << "Found matching entry, no need to create one.";

      // If we found something drop it from the list so we don't have to avoid
      // deleting it in RemoveAllCrosEntries.
      entries_.erase(found_entry.value());
    }

    // Any remaining cros entries don't match what we want, and should be
    // removed.
    RemoveAllCrosEntries();

    // If we didn't find an existing one, we'll need to create a new entry.
    if (!found_entry) {
      LOG(INFO) << "Creating EFI boot entry.";
      // Try to pick a number.
      const base::Optional<EfiBootNumber> desired_num = NextAvailableBootNum();

      // If we didn't get a number, we've got to bail.
      if (!desired_num) {
        LOG(ERROR) << kCantEnsureBoot
                   << "need an available boot number, all are taken.";
        return false;
      }

      if (!WriteEntry(desired_num.value(), desired_contents.value())) {
        LOG(ERROR) << kCantEnsureBoot << "need to write boot entry.";
        return false;
      }

      boot_order_.Add(desired_num.value());
    }

    // This will be needed if we deleted any entries that were in the boot order
    // or if we wrote a new one.
    if (!boot_order_.WriteIfNeeded(*efivar_)) {
      LOG(ERROR) << kCantEnsureBoot << "need to write boot order.";
      return false;
    }

    return true;
  }

  // For testing.
  EntriesMap Entries() const { return entries_; }
  void SetEntries(const EntriesMap& entries) { entries_ = entries; }

  BootOrder Order() const { return boot_order_; }
  void SetBootOrder(const BootOrder& order) { boot_order_ = order; }

 private:
  // An interface around libefivar, handles the actual writing/reading to
  // sysfs and other filesystem access.
  EfiVarInterface* efivar_;

  // Container for our entries, mapping boot numbers to entry contents.
  EntriesMap entries_;

  BootOrder boot_order_;
};

}  // namespace

// Wraps some advance checks and final logging around the actual logic in Impl.
bool UpdateEfiBootEntries(const InstallConfig& install_config) {
  EfiVarImpl efivar;
  if (!efivar.EfiVariablesSupported()) {
    LOG(INFO) << "EFI runtime services not available."
                 " Assuming called from a Legacy context or on a device that"
                 " intentionally blocks efi runtime services.";
    return true;
  } else {
    LOG(INFO) << "Adding EFI Boot entry.";
  }

  // Select the target boot file based on the platform.
  base::Optional<int> efi_size = GetEfiPlatformSize();
  if (!efi_size.has_value()) {
    LOG(ERROR)
        << "Can't determine EFI platform size, so can't make a boot entry.";
    return false;
  }

  EfiBootManager efi_boot_manager(efivar);
  if (!efi_boot_manager.UpdateEfiBootEntries(install_config,
                                             efi_size.value())) {
    LOG(ERROR) << "Failed to manage EFI boot entries, can't continue.";
    return false;
  }

  return true;
}
