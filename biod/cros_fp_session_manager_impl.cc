// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_session_manager_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/base64.h>

#include "biod/biod_storage.h"
#include "biod/cros_fp_record_manager.h"

namespace biod {

using SessionRecord = CrosFpSessionManager::SessionRecord;

CrosFpSessionManagerImpl::CrosFpSessionManagerImpl(
    std::unique_ptr<CrosFpRecordManagerInterface> record_manager)
    : record_manager_(std::move(record_manager)) {}

const std::optional<std::string>& CrosFpSessionManagerImpl::GetUser() const {
  return user_;
}

bool CrosFpSessionManagerImpl::LoadUser(std::string user_id) {
  if (user_.has_value()) {
    return false;
  }
  std::vector<Record> records = record_manager_->GetRecordsForUser(user_id);
  user_ = std::move(user_id);

  for (auto record : std::move(records)) {
    std::string tmpl_data_str;
    base::Base64Decode(record.data, &tmpl_data_str);
    VendorTemplate tmpl(tmpl_data_str.begin(), tmpl_data_str.end());
    records_.push_back(CrosFpSessionManagerImpl::SessionRecord{
        .record_metadata = std::move(record.metadata),
        .tmpl = std::move(tmpl),
    });
  }

  return true;
}

void CrosFpSessionManagerImpl::UnloadUser() {
  user_.reset();
  records_.clear();
  record_manager_->RemoveRecordsFromMemory();
}

bool CrosFpSessionManagerImpl::CreateRecord(
    const BiodStorageInterface::RecordMetadata& record,
    std::unique_ptr<VendorTemplate> templ) {
  if (!user_.has_value() || record.user_id != user_) {
    LOG(ERROR) << "Can't create record when there is no active user.";
    return false;
  }

  VendorTemplate vendor_templ = *templ;
  if (!record_manager_->CreateRecord(record, std::move(templ))) {
    LOG(ERROR) << "Failed to create and persist the record.";
    return false;
  }

  records_.push_back(CrosFpSessionManagerImpl::SessionRecord{
      .record_metadata = record,
      .tmpl = std::move(vendor_templ),
  });

  return true;
}

bool CrosFpSessionManagerImpl::UpdateRecord(
    const BiodStorageInterface::RecordMetadata& record_metadata,
    std::unique_ptr<VendorTemplate> templ) {
  const auto& record_id = record_metadata.record_id;
  const auto& user_id = record_metadata.user_id;

  if (!user_.has_value() || user_id != user_) {
    LOG(ERROR) << "Can't update record when there is no active user.";
    return false;
  }

  VendorTemplate vendor_templ = *templ;
  if (!record_manager_->UpdateRecord(record_metadata, std::move(templ))) {
    LOG(ERROR) << "Failed to update and persist the record.";
    return false;
  }

  std::optional<size_t> match_idx = std::nullopt;
  for (size_t i = 0; i < records_.size(); i++) {
    if (records_[i].record_metadata.record_id == record_id) {
      match_idx = i;
      break;
    }
  }
  CHECK(match_idx.has_value());

  records_[*match_idx].record_metadata = record_metadata;
  records_[*match_idx].tmpl = std::move(vendor_templ);

  return true;
}

std::vector<SessionRecord> CrosFpSessionManagerImpl::GetRecords() {
  return records_;
}

std::optional<BiodStorageInterface::RecordMetadata>
CrosFpSessionManagerImpl::GetRecordMetadata(size_t idx) const {
  if (idx < records_.size()) {
    return records_[idx].record_metadata;
  }
  return std::nullopt;
}

size_t CrosFpSessionManagerImpl::GetNumOfTemplates() {
  return records_.size();
}

}  // namespace biod
