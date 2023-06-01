// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_session_manager_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_number_conversions.h>
#include <base/base64.h>
#include <gtest/gtest.h>

#include "biod/mock_cros_fp_record_manager.h"

namespace biod {

using SessionRecord = CrosFpSessionManager::SessionRecord;

using testing::Pointee;
using testing::Return;

class CrosFpSessionManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto mock_record_manager = std::make_unique<MockCrosFpRecordManager>();
    mock_record_manager_ = mock_record_manager.get();

    session_manager_ = std::make_unique<CrosFpSessionManagerImpl>(
        std::move(mock_record_manager));
  }

 protected:
  void CheckTemplatesEqual(const std::vector<Record> records) {
    const std::vector<SessionRecord> current_records =
        session_manager_->GetRecords();
    ASSERT_EQ(current_records.size(), records.size());
    // Test behavior of GetNumOfTemplates.
    EXPECT_EQ(session_manager_->GetNumOfTemplates(), records.size());

    for (int i = 0; i < current_records.size(); i++) {
      SessionRecord session_record = current_records[i];
      Record record = records[i];
      EXPECT_EQ(session_record.record_metadata, record.metadata);
      EXPECT_EQ(base::Base64Encode(session_record.tmpl), record.data);
      EXPECT_EQ(*session_manager_->GetRecordMetadata(i), record.metadata);
    }
    EXPECT_FALSE(session_manager_->GetRecordMetadata(current_records.size())
                     .has_value());
  }

  MockCrosFpRecordManager* mock_record_manager_;
  std::unique_ptr<CrosFpSessionManagerImpl> session_manager_;
};

TEST_F(CrosFpSessionManagerTest, LoadUnloadUser) {
  const std::string kUser("testuser");

  EXPECT_FALSE(session_manager_->GetUser().has_value());

  EXPECT_CALL(*mock_record_manager_, GetRecordsForUser(kUser));

  EXPECT_TRUE(session_manager_->LoadUser(kUser));
  const std::optional<std::string>& user = session_manager_->GetUser();
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(*user, kUser);

  EXPECT_CALL(*mock_record_manager_, RemoveRecordsFromMemory);

  session_manager_->UnloadUser();
  EXPECT_FALSE(session_manager_->GetUser().has_value());
}

TEST_F(CrosFpSessionManagerTest, GetRecords) {
  const std::string kUser("testuser");
  const std::vector<Record> kOriginalRecords{
      Record{
          .metadata =
              RecordMetadata{
                  .record_id = "record_id_1",
                  .user_id = kUser,
              },
          .data = base::Base64Encode(brillo::Blob(32, 1)),
      },
      Record{
          .metadata =
              RecordMetadata{
                  .record_id = "record_id_2",
                  .user_id = kUser,
              },
          .data = base::Base64Encode(brillo::Blob(32, 2)),
      },
      Record{
          .metadata =
              RecordMetadata{
                  .record_id = "record_id_3",
                  .user_id = kUser,
              },
          .data = base::Base64Encode(brillo::Blob(32, 3)),
      }};
  const RecordMetadata kNewRecordMetadata{.record_id = "record_id_4",
                                          .user_id = kUser};
  const VendorTemplate kNewTemplate(32, 4);

  EXPECT_CALL(*mock_record_manager_, GetRecordsForUser(kUser))
      .WillOnce(Return(kOriginalRecords));

  EXPECT_TRUE(session_manager_->LoadUser(kUser));
  CheckTemplatesEqual(kOriginalRecords);

  session_manager_->UnloadUser();
  EXPECT_FALSE(session_manager_->GetUser().has_value());
  CheckTemplatesEqual(std::vector<Record>());

  // Create/Update operations should directly fail without calling record
  // manager.
  EXPECT_CALL(*mock_record_manager_, CreateRecord).Times(0);
  EXPECT_CALL(*mock_record_manager_, UpdateRecord).Times(0);

  EXPECT_FALSE(session_manager_->CreateRecord(
      kNewRecordMetadata, std::make_unique<VendorTemplate>(kNewTemplate)));
  EXPECT_FALSE(session_manager_->UpdateRecord(
      kNewRecordMetadata, std::make_unique<VendorTemplate>(kNewTemplate)));
}

TEST_F(CrosFpSessionManagerTest, CreateRecord) {
  const std::string kUser("testuser");
  const std::vector<Record> kOriginalRecords{Record{
      .metadata =
          RecordMetadata{
              .record_id = "record_id_1",
              .user_id = kUser,
          },
      .data = base::Base64Encode(brillo::Blob(32, 1)),
  }};
  const RecordMetadata kNewRecordMetadata{.record_id = "record_id_2",
                                          .user_id = kUser};
  const VendorTemplate kNewTemplate(32, 2);

  EXPECT_CALL(*mock_record_manager_, GetRecordsForUser(kUser))
      .WillOnce(Return(kOriginalRecords));

  EXPECT_TRUE(session_manager_->LoadUser(kUser));
  CheckTemplatesEqual(kOriginalRecords);

  EXPECT_CALL(*mock_record_manager_,
              CreateRecord(kNewRecordMetadata, Pointee(kNewTemplate)))
      .WillOnce(Return(true));

  EXPECT_TRUE(session_manager_->CreateRecord(
      kNewRecordMetadata, std::make_unique<VendorTemplate>(kNewTemplate)));
  std::vector<Record> new_records = kOriginalRecords;
  new_records.push_back(Record{.metadata = kNewRecordMetadata,
                               .data = base::Base64Encode(kNewTemplate)});
  CheckTemplatesEqual(new_records);

  session_manager_->UnloadUser();
  EXPECT_FALSE(session_manager_->GetUser().has_value());
  CheckTemplatesEqual(std::vector<Record>());
}

TEST_F(CrosFpSessionManagerTest, UpdateRecord) {
  const std::string kUser("testuser");
  const std::vector<Record> kOriginalRecords{Record{
      .metadata =
          RecordMetadata{
              .record_id = "record_id_1",
              .user_id = kUser,
          },
      .data = base::Base64Encode(brillo::Blob(32, 1)),
  }};
  const RecordMetadata kNewRecordMetadata{.record_id = "record_id_1",
                                          .user_id = kUser};
  const VendorTemplate kNewTemplate(32, 2);

  EXPECT_CALL(*mock_record_manager_, GetRecordsForUser(kUser))
      .WillOnce(Return(kOriginalRecords));

  EXPECT_TRUE(session_manager_->LoadUser(kUser));
  CheckTemplatesEqual(kOriginalRecords);

  EXPECT_CALL(*mock_record_manager_,
              UpdateRecord(kNewRecordMetadata, Pointee(kNewTemplate)))
      .WillOnce(Return(true));

  EXPECT_TRUE(session_manager_->UpdateRecord(
      kNewRecordMetadata, std::make_unique<VendorTemplate>(kNewTemplate)));
  std::vector<Record> new_records = kOriginalRecords;
  new_records[0] = Record{.metadata = kNewRecordMetadata,
                          .data = base::Base64Encode(kNewTemplate)};
  CheckTemplatesEqual(new_records);

  session_manager_->UnloadUser();
  EXPECT_FALSE(session_manager_->GetUser().has_value());
  CheckTemplatesEqual(std::vector<Record>());
}

TEST_F(CrosFpSessionManagerTest, CreateRecordFailed) {
  const std::string kUser("testuser");
  const std::vector<Record> kOriginalRecords{Record{
      .metadata =
          RecordMetadata{
              .record_id = "record_id_1",
              .user_id = kUser,
          },
      .data = base::Base64Encode(brillo::Blob(32, 1)),
  }};
  const RecordMetadata kNewRecordMetadata{.record_id = "record_id_2",
                                          .user_id = kUser};
  const VendorTemplate kNewTemplate(32, 2);

  EXPECT_CALL(*mock_record_manager_, GetRecordsForUser(kUser))
      .WillOnce(Return(kOriginalRecords));

  EXPECT_TRUE(session_manager_->LoadUser(kUser));
  CheckTemplatesEqual(kOriginalRecords);

  EXPECT_CALL(*mock_record_manager_, CreateRecord).WillOnce(Return(false));

  EXPECT_FALSE(session_manager_->CreateRecord(
      kNewRecordMetadata, std::make_unique<VendorTemplate>(kNewTemplate)));
  CheckTemplatesEqual(kOriginalRecords);

  session_manager_->UnloadUser();
  EXPECT_FALSE(session_manager_->GetUser().has_value());
  CheckTemplatesEqual(std::vector<Record>());
}

TEST_F(CrosFpSessionManagerTest, UpdateRecordFailed) {
  const std::string kUser("testuser");
  const std::vector<Record> kOriginalRecords{Record{
      .metadata =
          RecordMetadata{
              .record_id = "record_id_1",
              .user_id = kUser,
          },
      .data = base::Base64Encode(brillo::Blob(32, 1)),
  }};
  const RecordMetadata kNewRecordMetadata{.record_id = "record_id_1",
                                          .user_id = kUser};
  const VendorTemplate kNewTemplate(32, 2);

  EXPECT_CALL(*mock_record_manager_, GetRecordsForUser(kUser))
      .WillOnce(Return(kOriginalRecords));

  EXPECT_TRUE(session_manager_->LoadUser(kUser));
  CheckTemplatesEqual(kOriginalRecords);

  EXPECT_CALL(*mock_record_manager_, UpdateRecord).WillOnce(Return(false));

  EXPECT_FALSE(session_manager_->UpdateRecord(
      kNewRecordMetadata, std::make_unique<VendorTemplate>(kNewTemplate)));
  CheckTemplatesEqual(kOriginalRecords);

  session_manager_->UnloadUser();
  EXPECT_FALSE(session_manager_->GetUser().has_value());
  CheckTemplatesEqual(std::vector<Record>());
}

}  // namespace biod
