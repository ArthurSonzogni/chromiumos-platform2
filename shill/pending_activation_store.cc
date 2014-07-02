// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/pending_activation_store.h"

#include "shill/key_file_store.h"
#include "shill/logging.h"

using base::FilePath;
using std::string;

namespace shill {

const char PendingActivationStore::kIccidGroupId[] = "iccid_list";
const char PendingActivationStore::kMeidGroupId[] = "meid_list";
// We're keeping the old file name here for backwards compatibility.
const char PendingActivationStore::kStorageFileName[] =
    "activating_iccid_store.profile";

PendingActivationStore::PendingActivationStore() {}

PendingActivationStore::~PendingActivationStore() {
  if (storage_.get())
    storage_->Flush();  // Make certain that everything is persisted.
}

namespace {

string StateToString(PendingActivationStore::State state) {
  switch (state) {
    case PendingActivationStore::kStateUnknown:
      return "Unknown";
    case PendingActivationStore::kStatePending:
      return "Pending";
    case PendingActivationStore::kStateActivated:
      return "Activated";
    case PendingActivationStore::kStatePendingTimeout:
      return "PendingTimeout";
    default:
      return "Invalid";
  }
}

string FormattedIdentifier(PendingActivationStore::IdentifierType type,
                           const string &identifier) {
  string label;
  switch (type) {
    case PendingActivationStore::kIdentifierICCID:
      label = "ICCID";
      break;
    case PendingActivationStore::kIdentifierMEID:
      label = "MEID";
      break;
    default:
      NOTREACHED();
  }
  return "[" + label + "=" + identifier + "]";
}

}  // namespace

// static
string PendingActivationStore::IdentifierTypeToGroupId(IdentifierType type) {
  switch (type) {
    case kIdentifierICCID:
      return kIccidGroupId;
    case kIdentifierMEID:
      return kMeidGroupId;
    default:
      SLOG(Cellular, 2) << "Incorrect identifier type: " << type;
      return "";
  }
}

bool PendingActivationStore::InitStorage(
    GLib *glib,
    const FilePath &storage_path) {
  // Close the current file.
  if (storage_.get()) {
    storage_->Flush();
    storage_.reset();  // KeyFileStore closes the file in its destructor.
  }
  if (!glib) {
    LOG(ERROR) << "Null pointer passed for |glib|.";
    return false;
  }
  if (storage_path.empty()) {
    LOG(ERROR) << "Empty storage directory path provided.";
    return false;
  }
  FilePath path = storage_path.Append(kStorageFileName);
  scoped_ptr<KeyFileStore> storage(new KeyFileStore(glib));
  storage->set_path(path);
  bool already_exists = storage->IsNonEmpty();
  if (!storage->Open()) {
    LOG(ERROR) << "Failed to open file at '" << path.AsUTF8Unsafe()  << "'";
    if (already_exists)
      storage->MarkAsCorrupted();
    return false;
  }
  if (!already_exists)
    storage->SetHeader("Identifiers pending cellular activation.");
  storage_.reset(storage.release());
  return true;
}

PendingActivationStore::State PendingActivationStore::GetActivationState(
    IdentifierType type,
    const string &identifier) const {
  string formatted_identifier = FormattedIdentifier(type, identifier);
  SLOG(Cellular, 2) << __func__ << ": " << formatted_identifier;
  if (!storage_.get()) {
    LOG(ERROR) << "Underlying storage not initialized.";
    return kStateUnknown;
  }
  int state = 0;
  if (!storage_->GetInt(IdentifierTypeToGroupId(type), identifier, &state)) {
    SLOG(Cellular, 2) << "No entry exists for " << formatted_identifier;
    return kStateUnknown;
  }
  if (state <= 0 || state >= kStateMax) {
    SLOG(Cellular, 2) << "State value read for " << formatted_identifier
                      << " is invalid.";
    return kStateUnknown;
  }
  return static_cast<State>(state);
}

bool PendingActivationStore::SetActivationState(
    IdentifierType type,
    const string &identifier,
    State state) {
  SLOG(Cellular, 2) << __func__ << ": State=" << StateToString(state) << ", "
                    << FormattedIdentifier(type, identifier);
  if (!storage_.get()) {
    LOG(ERROR) << "Underlying storage not initialized.";
    return false;
  }
  if (state == kStateUnknown) {
    SLOG(Cellular, 2) << "kStateUnknown cannot be used as a value.";
    return false;
  }
  if (state < 0 || state >= kStateMax) {
    SLOG(Cellular, 2) << "Cannot set state to \"" << StateToString(state)
                      << "\"";
    return false;
  }
  if (!storage_->SetInt(
      IdentifierTypeToGroupId(type), identifier, static_cast<int>(state))) {
    SLOG(Cellular, 2) << "Failed to store the given identifier and state "
                      << "values.";
    return false;
  }
  return storage_->Flush();
}

bool PendingActivationStore::RemoveEntry(IdentifierType type,
                                         const std::string &identifier) {
  SLOG(Cellular, 2) << __func__ << ": "
                    << FormattedIdentifier(type, identifier);
  if (!storage_.get()) {
    LOG(ERROR) << "Underlying storage not initialized.";
    return false;
  }
  if (!storage_->DeleteKey(IdentifierTypeToGroupId(type), identifier)) {
    SLOG(Cellular, 2) << "Failed to remove the given identifier.";
    return false;
  }
  return storage_->Flush();
}

}  // namespace shill
