// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_SESSION_MANAGER_H_
#define BIOD_CROS_FP_SESSION_MANAGER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "biod/biod_storage.h"
#include "biod/cros_fp_record_manager.h"

namespace biod {

class CrosFpSessionManager {
 public:
  struct SessionRecord {
    BiodStorageInterface::RecordMetadata record_metadata;
    VendorTemplate tmpl;
  };

  virtual ~CrosFpSessionManager() = default;

  // Get the user id of the current session.
  virtual const std::optional<std::string>& GetUser() const = 0;

  // Start a user session for the specified user id. Old session will be wiped.
  virtual bool LoadUser(std::string user_id) = 0;

  // Wipe the current user session.
  virtual void UnloadUser() = 0;

  // Add a record to the current user session. It should persist the record both
  // on disk and in memory. It will fail if no user session exists.
  virtual bool CreateRecord(const BiodStorageInterface::RecordMetadata& record,
                            std::unique_ptr<VendorTemplate> templ) = 0;

  // Update a record that belongs to the current user session. It should modify
  // the record both on disk and in memory. It will fail if no user session
  // exists.
  virtual bool UpdateRecord(
      const BiodStorageInterface::RecordMetadata& record_metadata,
      std::unique_ptr<VendorTemplate> templ) = 0;

  // Return whether a record with |record_id| exists for the current user.
  virtual bool HasRecordId(const std::string& record_id) = 0;

  // Delete the record with |record_id|. It will fail if no user session exists.
  virtual bool DeleteRecord(const std::string& record_id) = 0;

  // Delete a record from persistent storage directly. The record mustn't be in
  // the in-memory records, so that we don't lose sync with disk.
  virtual bool DeleteNotLoadedRecord(const std::string& user_id,
                                     const std::string& record_id) = 0;

  // Get all of the templates that belong to the current user session. It will
  // return an empty list if no user session exists.
  virtual std::vector<SessionRecord> GetRecords() = 0;

  // Get the idx-th record metadata.
  virtual std::optional<BiodStorageInterface::RecordMetadata> GetRecordMetadata(
      size_t idx) const = 0;

  // Get the number of templates that belong to the current user session. It
  // will return 0 no user session exists.
  virtual size_t GetNumOfTemplates() = 0;
};

}  // namespace biod

#endif  // BIOD_CROS_FP_SESSION_MANAGER_H_
