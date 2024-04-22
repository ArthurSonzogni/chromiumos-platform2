// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/journal.h"

#include <optional>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/types/optional_util.h>
#include <base/unguessable_token.h>
#include <brillo/proto_file_io.h>
#include <chromeos/switches/modemfwd_switches.h>

#include "modemfwd/firmware_file.h"
#include "modemfwd/logging.h"
#include "modemfwd/modem_helper.h"
#include "modemfwd/proto_bindings/journal_entry.pb.h"
#include "modemfwd/scoped_temp_file.h"

namespace modemfwd {

namespace {

std::string JournalTypeToFirmwareType(int t) {
  switch (t) {
    case JournalEntryType::MAIN:
      return modemfwd::kFwMain;
    case JournalEntryType::CARRIER:
      return modemfwd::kFwCarrier;
    case JournalEntryType::OEM:
      return modemfwd::kFwOem;
    default:
      return std::string();
  }
}

JournalEntryType FirmwareTypeToJournalType(std::string fw_type) {
  if (fw_type == modemfwd::kFwMain)
    return JournalEntryType::MAIN;
  else if (fw_type == modemfwd::kFwCarrier)
    return JournalEntryType::CARRIER;
  else if (fw_type == modemfwd::kFwOem)
    return JournalEntryType::OEM;
  else
    return JournalEntryType::UNKNOWN;
}

struct JournalEntryWithId {
  std::string id;
  JournalEntry entry;
};

// Returns true if the operation was restarted successfully or false if it
// failed.
bool RestartOperation(const JournalEntry& entry,
                      FirmwareDirectory* firmware_dir,
                      ModemHelperDirectory* helper_dir) {
  ModemHelper* helper = helper_dir->GetHelperForDeviceId(entry.device_id());
  if (!helper) {
    LOG(ERROR) << "Journal contained unfinished operation for device with ID \""
               << entry.device_id()
               << "\" but no helper was found to restart it";
    return false;
  }

  std::string carrier_id(entry.carrier_id());
  FirmwareDirectory::Files res = firmware_dir->FindFirmware(
      entry.device_id(), carrier_id.empty() ? nullptr : &carrier_id);

  std::vector<FirmwareConfig> flashed_fw;
  std::vector<std::string> paths_for_logging;
  // Keep a reference to all temporary uncompressed files.
  std::vector<std::unique_ptr<FirmwareFile>> all_files;
  for (const auto& entry_type : entry.type()) {
    std::string fw_type = JournalTypeToFirmwareType(entry_type);
    FirmwareFileInfo* info = nullptr;
    base::FilePath fw_path;
    std::string fw_version;

    if (fw_type.empty())
      continue;

    switch (entry_type) {
      case JournalEntryType::MAIN:
        info = base::OptionalToPtr<FirmwareFileInfo>(res.main_firmware);
        break;
      case JournalEntryType::CARRIER:
        info = base::OptionalToPtr<FirmwareFileInfo>(res.carrier_firmware);
        break;
      case JournalEntryType::OEM:
        info = base::OptionalToPtr<FirmwareFileInfo>(res.oem_firmware);
        break;
    }

    auto firmware_file = std::make_unique<FirmwareFile>();
    if (info == nullptr ||
        !firmware_file->PrepareFrom(firmware_dir->GetFirmwarePath(), *info)) {
      LOG(ERROR) << "Unfinished \"" << fw_type
                 << "\" firmware flash for device with ID \""
                 << entry.device_id() << "\" but no firmware was found";
      continue;
    }

    flashed_fw.push_back(
        {fw_type, firmware_file->path_on_filesystem(), info->version});
    paths_for_logging.push_back(firmware_file->path_for_logging().value());
    all_files.push_back(std::move(firmware_file));

    // Main firmware may also include associated firmware payloads that we will
    // simply reflash as well.
    if (entry_type == JournalEntryType::MAIN) {
      for (const auto& assoc_entry : res.assoc_firmware) {
        auto assoc_file = std::make_unique<FirmwareFile>();
        if (!assoc_file->PrepareFrom(firmware_dir->GetFirmwarePath(),
                                     assoc_entry.second)) {
          LOG(ERROR) << "Unfinished \"" << fw_type
                     << "\" firmware flash for device with ID \""
                     << entry.device_id() << "\" but no firmware was found";
          continue;
        }

        flashed_fw.push_back({assoc_entry.first,
                              assoc_file->path_on_filesystem(),
                              assoc_entry.second.version});
        paths_for_logging.push_back(assoc_file->path_for_logging().value());
      }
    }
  }
  if (flashed_fw.size() != entry.type_size() || !flashed_fw.size()) {
    LOG(ERROR) << "Malformed journal entry with invalid types.";
    return false;
  }

  ELOG(INFO) << "Journal reflashing firmwares: "
             << base::JoinString(paths_for_logging, ",");
  return helper->FlashFirmwares(flashed_fw);
}

std::optional<JournalLog> ParseJournal(base::File& journal_file) {
  JournalLog log;

  if (brillo::ReadTextProtobuf(journal_file.GetPlatformFile(), &log)) {
    return log;
  }

  // Old versions of the journal may have just a single entry in the file.
  journal_file.Seek(base::File::FROM_BEGIN, 0);
  JournalEntry entry;
  if (brillo::ReadTextProtobuf(journal_file.GetPlatformFile(), &entry)) {
    *log.add_entry() = entry;
    return log;
  }

  LOG(WARNING) << "Failed to parse journal";
  return std::nullopt;
}

class JournalImpl : public Journal {
 public:
  explicit JournalImpl(const base::FilePath& journal_path)
      : journal_path_(journal_path) {}
  JournalImpl(const JournalImpl&) = delete;
  JournalImpl& operator=(const JournalImpl&) = delete;

  std::optional<std::string> MarkStartOfFlashingFirmware(
      const std::vector<std::string>& firmware_types,
      const std::string& device_id,
      const std::string& carrier_id) override {
    JournalEntry entry;
    entry.set_device_id(device_id);
    entry.set_carrier_id(carrier_id);
    for (const auto& t : firmware_types) {
      entry.add_type(FirmwareTypeToJournalType(t));
    }

    std::string entry_id = base::UnguessableToken().Create().ToString();
    entries_.emplace_back(entry_id, entry);
    if (!SerializeJournal()) {
      LOG(INFO) << __func__ << ": failed to serialize journal";
      return std::nullopt;
    }

    return entry_id;
  }

  void MarkEndOfFlashingFirmware(const std::string& entry_id) override {
    std::erase_if(
        entries_,
        [&entry_id](const std::pair<std::string, JournalEntry>& entry) {
          return entry_id == entry.first;
        });
    if (!SerializeJournal()) {
      LOG(INFO) << __func__ << ": failed to serialize journal";
    }
  }

 private:
  bool SerializeJournal() {
    JournalLog log;
    for (const auto& entry : entries_) {
      *log.add_entry() = entry.second;
    }

    // Replace the file atomically.
    auto temp_file = ScopedTempFile::Create();
    if (!temp_file) {
      LOG(ERROR) << "Couldn't create temp file";
      return false;
    }
    base::File new_journal(temp_file->path(), base::File::FLAG_CREATE_ALWAYS |
                                                  base::File::FLAG_WRITE);
    if (!brillo::WriteTextProtobuf(new_journal.GetPlatformFile(), log)) {
      LOG(ERROR) << "Couldn't write new journal to temp file";
      return false;
    }

    if (!base::Move(temp_file->path(), journal_path_)) {
      LOG(ERROR) << "Couldn't replace journal file";
      return false;
    }

    return true;
  }

  std::vector<std::pair<std::string, JournalEntry>> entries_;
  base::FilePath journal_path_;
};

}  // namespace

std::unique_ptr<Journal> OpenJournal(const base::FilePath& journal_path,
                                     FirmwareDirectory* firmware_dir,
                                     ModemHelperDirectory* helper_dir) {
  base::File journal_file(journal_path, base::File::FLAG_OPEN_ALWAYS |
                                            base::File::FLAG_READ |
                                            base::File::FLAG_WRITE);
  if (!journal_file.IsValid()) {
    LOG(ERROR) << "Could not open journal file";
    return nullptr;
  }

  // Check to see if we have uncommitted operations to restart.
  JournalLog entries_to_restart;
  if (journal_file.GetLength() != 0) {
    std::optional<JournalLog> log = ParseJournal(journal_file);
    if (log.has_value()) {
      entries_to_restart = *log;
    }
  }
  for (const auto& entry : entries_to_restart.entry()) {
    if (!RestartOperation(entry, firmware_dir, helper_dir)) {
      LOG(ERROR) << "Failed to restart uncommitted operation";
      // Note that we don't stop here; we will try to commit every
      // operation in the journal.
    }
  }

  // Clearing the journal prevents it from growing without bound but also
  // ensures that if we crash after this point, we won't try to restart
  // any operations an extra time.
  journal_file.SetLength(0);
  journal_file.Seek(base::File::FROM_BEGIN, 0);
  journal_file.Flush();

  return std::make_unique<JournalImpl>(journal_path);
}

}  // namespace modemfwd
