// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_SESSION_MANAGER_IMPL_H_
#define BIOD_CROS_FP_SESSION_MANAGER_IMPL_H_

#include "biod/cros_fp_session_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "biod/biod_storage.h"
#include "biod/cros_fp_record_manager.h"

namespace biod {

class CrosFpSessionManagerImpl : public CrosFpSessionManager {
 public:
  explicit CrosFpSessionManagerImpl(
      std::unique_ptr<CrosFpRecordManagerInterface> record_manager);
  CrosFpSessionManagerImpl(const CrosFpSessionManagerImpl&) = delete;
  CrosFpSessionManagerImpl& operator=(const CrosFpSessionManagerImpl&) = delete;
  ~CrosFpSessionManagerImpl() override = default;

  const std::optional<std::string>& GetUser() const override;
  bool LoadUser(std::string user_id) override;
  void UnloadUser() override;
  bool CreateRecord(const BiodStorageInterface::RecordMetadata& record,
                    std::unique_ptr<VendorTemplate> templ) override;
  bool UpdateRecord(const BiodStorageInterface::RecordMetadata& record_metadata,
                    std::unique_ptr<VendorTemplate> templ) override;
  bool HasRecordId(const std::string& record_id) override;
  bool DeleteRecord(const std::string& record_id) override;
  bool DeleteNotLoadedRecord(const std::string& user_id,
                             const std::string& record_id) override;
  std::vector<SessionRecord> GetRecords() override;
  std::optional<BiodStorageInterface::RecordMetadata> GetRecordMetadata(
      size_t idx) const override;
  size_t GetNumOfTemplates() override;

 private:
  std::unique_ptr<CrosFpRecordManagerInterface> record_manager_;
  std::optional<std::string> user_ = std::nullopt;
  std::vector<SessionRecord> records_;
};

}  // namespace biod

#endif  // BIOD_CROS_FP_SESSION_MANAGER_IMPL_H_
