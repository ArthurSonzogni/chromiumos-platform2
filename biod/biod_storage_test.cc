// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_storage.h"

#include <sys/resource.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <testing/gtest/include/gtest/gtest.h>
#include <base/strings/string_util.h>
#include <base/files/important_file_writer.h>
#include <base/json/json_string_value_serializer.h>

namespace biod {

namespace {

const char kBiometricsManagerName[] = "BiometricsManager";

const base::FilePath kFilePath("TestFile");

constexpr int kInvalidRecordFormatVersion = -1;

const char kRecordId1[] = "00000000_0000_0000_0000_000000000001";
const char kUserId1[] = "0000000000000000000000000000000000000001";
const char kLabel1[] = "record1";
const std::vector<uint8_t> kValidationVal1 = {0x00, 0x01};
const char kData1[] = "Hello, world1!";

const char kRecordId2[] = "00000000_0000_0000_0000_000000000002";
const char kUserId2[] = "0000000000000000000000000000000000000002";
const char kLabel2[] = "record2";
const std::vector<uint8_t> kValidationVal2 = {0x00, 0x02};
const char kData2[] = "Hello, world2!";

const char kRecordId3[] = "00000000_0000_0000_0000_000000000003";
const char kLabel3[] = "record3";
const std::vector<uint8_t> kValidationVal3 = {0x00, 0x03};
const char kData3[] = "Hello, world3!";

constexpr int kPermissions600 =
    base::FILE_PERMISSION_READ_BY_USER | base::FILE_PERMISSION_WRITE_BY_USER;
constexpr int kPermissions700 = base::FILE_PERMISSION_USER_MASK;

const char kInvalidUTF8[] = "\xed\xa0\x80\xed\xbf\xbf";

constexpr int kFpc1145TemplateSizeBytes = 47616;
constexpr int kFpc1025TemplateSizeBytes = 5156;
constexpr int kElan80TemplateSizeBytes = 41024;
constexpr int kElan515TemplateSizeBytes = 67064;

/**
 * "Max locked memory" value from reading /proc/<PID>/limits on DUT
 *
 * This matches the default value in the kernel:
 * https://chromium.googlesource.com/chromiumos/third_party/kernel/+/a5746cdefaed35de0a85ede48a47e9a340a6f7e6/include/uapi/linux/resource.h#72
 *
 * The default can be overridden in /etc/security/limits.conf:
 * https://access.redhat.com/solutions/61334
 *
 * or in the upstart script http://upstart.ubuntu.com/cookbook/#limit:
 *
 * limit memlock <soft> <hard>
 */
constexpr int kRlimitMemlockBytes = 65536;

// Flag to control whether to run tests with positive match secret support.
// This can't be a member of the test fixture because it's accessed in the
// TestRecord class in anonymous namespace.
bool test_record_supports_positive_match_secret = true;

class TestRecord : public BiometricsManagerRecord {
 public:
  TestRecord(const std::string& id,
             const std::string& user_id,
             const std::string& label,
             const std::vector<uint8_t>& validation_val,
             const std::string& data)
      : id_(id),
        user_id_(user_id),
        label_(label),
        validation_val_(validation_val),
        data_(data) {}

  // BiometricsManager::Record overrides:
  const std::string& GetId() const override { return id_; }
  const std::string& GetUserId() const override { return user_id_; }
  const std::string& GetLabel() const override { return label_; }
  const std::vector<uint8_t>& GetValidationVal() const override {
    return validation_val_;
  }
  const std::string& GetData() const { return data_; }
  bool SetLabel(std::string label) override { return true; }
  bool Remove() override { return true; }
  bool SupportsPositiveMatchSecret() const override {
    return test_record_supports_positive_match_secret;
  }

  void ClearValidationValue() { validation_val_.clear(); }

  friend bool operator==(const TestRecord& lhs, const TestRecord& rhs) {
    return lhs.id_ == rhs.id_ && lhs.user_id_ == rhs.user_id_ &&
           lhs.validation_val_ == rhs.validation_val_ &&
           lhs.label_ == rhs.label_ && lhs.data_ == rhs.data_;
  }
  friend inline bool operator!=(const TestRecord& lhs, const TestRecord& rhs) {
    return !(lhs == rhs);
  }

 private:
  std::string id_;
  std::string user_id_;
  std::string label_;
  std::vector<uint8_t> validation_val_;
  std::string data_;
};

struct MemlockTestParams {
  int rlimit_bytes;
  int template_size_bytes;
};

}  // namespace

class BiodStorageBaseTest : public ::testing::Test {
 public:
  BiodStorageBaseTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    root_path_ = temp_dir_.GetPath().AppendASCII("biod_storage_unittest_root");
    biod_storage_ = std::make_unique<BiodStorage>(kBiometricsManagerName);
    // Since there is no session manager, allow accesses by default.
    biod_storage_->set_allow_access(true);
    biod_storage_->SetRootPathForTesting(root_path_);
  }
  BiodStorageBaseTest(const BiodStorageBaseTest&) = delete;
  BiodStorageBaseTest& operator=(const BiodStorageBaseTest&) = delete;

  ~BiodStorageBaseTest() override {
    EXPECT_TRUE(base::DeletePathRecursively(temp_dir_.GetPath()));
  }

  base::Value CreateRecordDictionary(
      const std::vector<uint8_t>& validation_val) {
    base::Value record_dictionary(base::Value::Type::DICTIONARY);
    std::string validation_value_str(validation_val.begin(),
                                     validation_val.end());
    base::Base64Encode(validation_value_str, &validation_value_str);
    record_dictionary.SetStringKey("match_validation_value",
                                   validation_value_str);
    return record_dictionary;
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath root_path_;
  std::unique_ptr<BiodStorage> biod_storage_;
};

class BiodStorageTest : public BiodStorageBaseTest,
                        public ::testing::WithParamInterface<bool> {
 public:
  BiodStorageTest() { test_record_supports_positive_match_secret = GetParam(); }
};

TEST_P(BiodStorageTest, WriteAndReadRecords) {
  const std::vector<uint8_t> kEmpty;
  const std::vector<TestRecord> kRecords = {
      TestRecord(
          kRecordId1, kUserId1, kLabel1,
          test_record_supports_positive_match_secret ? kValidationVal1 : kEmpty,
          kData1),
      TestRecord(
          kRecordId2, kUserId2, kLabel2,
          test_record_supports_positive_match_secret ? kValidationVal2 : kEmpty,
          kData2),
      TestRecord(
          kRecordId3, kUserId2, kLabel3,
          test_record_supports_positive_match_secret ? kValidationVal3 : kEmpty,
          kData3)};

  // Write the record.
  for (auto const& record : kRecords) {
    EXPECT_TRUE(
        biod_storage_->WriteRecord(record, base::Value(record.GetData())));
  }

  // Read the record.
  std::unordered_set<std::string> user_ids({kUserId1, kUserId2});
  auto read_result = biod_storage_->ReadRecords(user_ids);
  EXPECT_TRUE(read_result.invalid_records.empty());

  std::vector<TestRecord> records;
  for (const auto& record : read_result.valid_records) {
    records.emplace_back(
        TestRecord(record.metadata.record_id, record.metadata.user_id,
                   record.metadata.label, record.metadata.validation_val,
                   std::string(record.data.cbegin(), record.data.cend())));
  }
  EXPECT_EQ(records.size(), kRecords.size());
  EXPECT_TRUE(
      std::is_permutation(kRecords.begin(), kRecords.end(), records.begin()));
}

TEST_F(BiodStorageBaseTest, WriteRecord_InvalidAbsolutePath) {
  auto record =
      TestRecord(kRecordId1, "/absolutepath", kLabel1, kValidationVal1, kData1);

  EXPECT_FALSE(
      biod_storage_->WriteRecord(record, base::Value(record.GetData())));
}

TEST_F(BiodStorageBaseTest, WriteRecord_RecordIdNotUTF8) {
  EXPECT_FALSE(base::IsStringUTF8(kInvalidUTF8));

  auto record =
      TestRecord(kInvalidUTF8, kUserId1, kLabel1, kValidationVal1, kData1);

  EXPECT_FALSE(record.IsValidUTF8());
  EXPECT_FALSE(
      biod_storage_->WriteRecord(record, base::Value(record.GetData())));
}

TEST_F(BiodStorageBaseTest, WriteRecord_UserIdNotUTF8) {
  EXPECT_FALSE(base::IsStringUTF8(kInvalidUTF8));

  auto record =
      TestRecord(kRecordId1, kInvalidUTF8, kLabel1, kValidationVal1, kData1);

  EXPECT_FALSE(record.IsValidUTF8());
  EXPECT_FALSE(
      biod_storage_->WriteRecord(record, base::Value(record.GetData())));
}

TEST_F(BiodStorageBaseTest, WriteRecord_LabelNotUTF8) {
  EXPECT_FALSE(base::IsStringUTF8(kInvalidUTF8));

  auto record =
      TestRecord(kRecordId1, kUserId1, kInvalidUTF8, kValidationVal1, kData1);

  EXPECT_FALSE(record.IsValidUTF8());
  EXPECT_FALSE(
      biod_storage_->WriteRecord(record, base::Value(record.GetData())));
}

TEST_F(BiodStorageBaseTest, WriteRecord_CheckUmask) {
  auto record =
      TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1);

  const base::FilePath kRecordStorageFilename =
      root_path_.Append("biod")
          .Append(record.GetUserId())
          .Append(kBiometricsManagerName)
          .Append("Record" + record.GetId());

  ASSERT_FALSE(base::PathExists(kRecordStorageFilename));
  ASSERT_FALSE(base::PathExists(kRecordStorageFilename.DirName()));

  EXPECT_TRUE(
      biod_storage_->WriteRecord(record, base::Value(record.GetData())));

  // Check permissions of directory
  int actual_permissions;
  EXPECT_TRUE(base::GetPosixFilePermissions(kRecordStorageFilename.DirName(),
                                            &actual_permissions));
  EXPECT_EQ(kPermissions700, actual_permissions);

  // Check permissions of record
  EXPECT_TRUE(base::GetPosixFilePermissions(kRecordStorageFilename,
                                            &actual_permissions));
  EXPECT_EQ(kPermissions600, actual_permissions);
}

TEST_P(BiodStorageTest, DeleteRecord) {
  const std::vector<uint8_t> kEmpty;
  const TestRecord kRecord(
      kRecordId1, kUserId1, kLabel1,
      test_record_supports_positive_match_secret ? kValidationVal1 : kEmpty,
      kData1);

  // Delete a non-existent record.
  EXPECT_TRUE(biod_storage_->DeleteRecord(kUserId1, kRecordId1));

  EXPECT_TRUE(
      biod_storage_->WriteRecord(kRecord, base::Value(kRecord.GetData())));

  // Check this record is properly written.
  std::unordered_set<std::string> user_ids({kUserId1});
  auto read_result = biod_storage_->ReadRecords(user_ids);
  EXPECT_TRUE(read_result.invalid_records.empty());
  EXPECT_EQ(read_result.valid_records.size(), 1);
  const auto& record = read_result.valid_records[0];
  auto test_record =
      TestRecord(record.metadata.record_id, record.metadata.user_id,
                 record.metadata.label, record.metadata.validation_val,
                 std::string(record.data.cbegin(), record.data.cend()));
  EXPECT_EQ(test_record, kRecord);

  EXPECT_TRUE(biod_storage_->DeleteRecord(kUserId1, kRecordId1));

  // Check this record is properly deleted.
  read_result = biod_storage_->ReadRecords(user_ids);
  EXPECT_TRUE(read_result.valid_records.empty());
  EXPECT_TRUE(read_result.invalid_records.empty());
}

TEST_F(BiodStorageBaseTest, GenerateNewRecordId) {
  // Check the two record ids are different.
  std::string record_id1(BiodStorage::GenerateNewRecordId());
  std::string record_id2(BiodStorage::GenerateNewRecordId());
  EXPECT_NE(record_id1, record_id2);
}

TEST_F(BiodStorageBaseTest, TestEqualOperator) {
  EXPECT_EQ(TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1),
            TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1));

  EXPECT_NE(TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1),
            TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal2, kData1));
}

TEST_F(BiodStorageBaseTest, TestReadValidationValueFromRecord) {
  auto record_dictionary = CreateRecordDictionary(kValidationVal1);
  auto ret = biod_storage_->ReadValidationValueFromRecord(
      kRecordFormatVersion, record_dictionary, kFilePath);
  EXPECT_TRUE(ret != nullptr);
  EXPECT_EQ(*ret, kValidationVal1);
}

TEST_F(BiodStorageBaseTest, TestReadValidationValueFromRecordOldVersion) {
  auto record_dictionary = CreateRecordDictionary(kValidationVal1);
  std::vector<uint8_t> empty;
  auto ret = biod_storage_->ReadValidationValueFromRecord(
      kRecordFormatVersionNoValidationValue, record_dictionary, kFilePath);
  EXPECT_TRUE(ret != nullptr);
  EXPECT_EQ(*ret, empty);
}

TEST_F(BiodStorageBaseTest, TestReadValidationValueFromRecordInvalidVersion) {
  auto record_dictionary = CreateRecordDictionary(kValidationVal1);
  std::vector<uint8_t> empty;
  auto ret = biod_storage_->ReadValidationValueFromRecord(
      kInvalidRecordFormatVersion, record_dictionary, kFilePath);
  EXPECT_EQ(ret, nullptr);
}

INSTANTIATE_TEST_SUITE_P(RecordsSupportPositiveMatchSecret,
                         BiodStorageTest,
                         ::testing::Values(true, false));

/**
 * Tests for invalid records. In general records will be correctly formatted
 * since we follow a specific format when writing them, but we should be able
 * to handle invalid records from bugs, disk corruption, etc.
 */
class BiodStorageInvalidRecordTest : public ::testing::Test {
 public:
  BiodStorageInvalidRecordTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    root_path_ = temp_dir_.GetPath().AppendASCII(
        "biod_storage_invalid_record_test_root");
    biod_storage_ = std::make_unique<BiodStorage>(kBiometricsManagerName);
    // Since there is no session manager, allow accesses by default.
    biod_storage_->set_allow_access(true);
    biod_storage_->SetRootPathForTesting(root_path_);

    auto record =
        TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1);
    record_name_ = biod_storage_->GetRecordFilename(record);
    EXPECT_FALSE(record_name_.empty());
    EXPECT_TRUE(base::CreateDirectory(record_name_.DirName()));
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath root_path_;
  base::FilePath record_name_;
  std::unique_ptr<BiodStorageInterface> biod_storage_;
};

TEST_F(BiodStorageInvalidRecordTest, InvalidJSON) {
  EXPECT_TRUE(base::ImportantFileWriter::WriteFileAtomically(record_name_,
                                                             "this is not "
                                                             "JSON"));
  auto read_result = biod_storage_->ReadRecordsForSingleUser(kUserId1);
  EXPECT_EQ(read_result.valid_records.size(), 0);
  EXPECT_EQ(read_result.invalid_records.size(), 1);
}

TEST_F(BiodStorageInvalidRecordTest, MissingFormatVersion) {
  auto record = R"({
    "record_id": "1234",
    "label": "some_label",
    "data": "some_data",
    "match_validation_value": "4567"
  })";

  EXPECT_TRUE(
      base::ImportantFileWriter::WriteFileAtomically(record_name_, record));

  auto read_result = biod_storage_->ReadRecordsForSingleUser(kUserId1);
  EXPECT_EQ(read_result.valid_records.size(), 0);
  EXPECT_EQ(read_result.invalid_records.size(), 1);
}

TEST_F(BiodStorageInvalidRecordTest, MissingRecordId) {
  auto record = R"({
    "label": "some_label",
    "data": "some_data",
    "match_validation_value": "4567",
    "version": 2
  })";

  EXPECT_TRUE(
      base::ImportantFileWriter::WriteFileAtomically(record_name_, record));

  auto read_result = biod_storage_->ReadRecordsForSingleUser(kUserId1);
  EXPECT_EQ(read_result.valid_records.size(), 0);
  EXPECT_EQ(read_result.invalid_records.size(), 1);
}

TEST_F(BiodStorageInvalidRecordTest, MissingRecordLabel) {
  auto record = R"({
    "record_id": "1234",
    "data": "some_data",
    "match_validation_value": "4567",
    "version": 2
  })";

  EXPECT_TRUE(
      base::ImportantFileWriter::WriteFileAtomically(record_name_, record));

  auto read_result = biod_storage_->ReadRecordsForSingleUser(kUserId1);
  EXPECT_EQ(read_result.valid_records.size(), 0);
  EXPECT_EQ(read_result.invalid_records.size(), 1);
}

TEST_F(BiodStorageInvalidRecordTest, MissingRecordData) {
  auto record = R"({
    "record_id": "1234",
    "label": "some_label",
    "match_validation_value": "4567",
    "version": 2
  })";

  EXPECT_TRUE(
      base::ImportantFileWriter::WriteFileAtomically(record_name_, record));

  auto read_result = biod_storage_->ReadRecordsForSingleUser(kUserId1);
  EXPECT_EQ(read_result.valid_records.size(), 0);
  EXPECT_EQ(read_result.invalid_records.size(), 1);
}

TEST_F(BiodStorageInvalidRecordTest, MissingValidationValue) {
  auto record = R"({
    "record_id": "1234",
    "label": "some_label",
    "data": "some_data",
    "version": 2
  })";

  EXPECT_TRUE(
      base::ImportantFileWriter::WriteFileAtomically(record_name_, record));

  auto read_result = biod_storage_->ReadRecordsForSingleUser(kUserId1);
  EXPECT_EQ(read_result.valid_records.size(), 0);
  EXPECT_EQ(read_result.invalid_records.size(), 1);
}

TEST_F(BiodStorageInvalidRecordTest, ValidationValueNotBase64) {
  auto record = R"({
    "record_id": "1234",
    "label": "some_label",
    "data": "some_data",
    "match_validation_value": "not valid base64",
    "version": 2
  })";

  EXPECT_TRUE(
      base::ImportantFileWriter::WriteFileAtomically(record_name_, record));

  auto read_result = biod_storage_->ReadRecordsForSingleUser(kUserId1);
  EXPECT_EQ(read_result.valid_records.size(), 0);
  EXPECT_EQ(read_result.invalid_records.size(), 1);
}

/**
 * Tests to make sure we do not crash due to hitting the RLIMIT_MEMLOCK limit.
 * See http://b/181281782, http://b/175158241, and http://b/173655013.
 */
class BiodStorageMemlockTest
    : public testing::TestWithParam<MemlockTestParams> {
 public:
  BiodStorageMemlockTest() : params_(GetParam()) {
    CHECK(temp_dir_.CreateUniqueTempDir());
    root_path_ =
        temp_dir_.GetPath().AppendASCII("biod_storage_memlock_test_root");
    biod_storage_ = std::make_unique<BiodStorage>(kBiometricsManagerName);
    // Since there is no session manager, allow accesses by default.
    biod_storage_->set_allow_access(true);
    biod_storage_->SetRootPathForTesting(root_path_);

    auto record =
        TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1);
    record_name_ = biod_storage_->GetRecordFilename(record);
    EXPECT_FALSE(record_name_.empty());
    EXPECT_TRUE(base::CreateDirectory(record_name_.DirName()));

    struct rlimit limit;

    EXPECT_EQ(getrlimit(RLIMIT_MEMLOCK, &limit), 0);
    orig_limit_ = limit;

    limit.rlim_cur = params_.rlimit_bytes;
    EXPECT_LT(limit.rlim_cur, limit.rlim_max);
    EXPECT_EQ(setrlimit(RLIMIT_MEMLOCK, &limit), 0);

    EXPECT_EQ(getrlimit(RLIMIT_MEMLOCK, &limit), 0);
    EXPECT_EQ(limit.rlim_cur, params_.rlimit_bytes);
  }

  ~BiodStorageMemlockTest() override {
    // Restore original limits.
    EXPECT_EQ(setrlimit(RLIMIT_MEMLOCK, &orig_limit_), 0);
  }

 protected:
  const MemlockTestParams params_;
  base::ScopedTempDir temp_dir_;
  base::FilePath root_path_;
  base::FilePath record_name_;
  std::unique_ptr<BiodStorageInterface> biod_storage_;
  struct rlimit orig_limit_;
};

TEST_P(BiodStorageMemlockTest, ReadReadRecords) {
  base::Value record_value(base::Value::Type::DICTIONARY);
  record_value.SetStringKey("record_id", "1234");
  record_value.SetStringKey("label", "some_label");
  record_value.SetStringKey("match_validation_value", "4567");
  record_value.SetIntKey("version", 2);
  std::vector<uint8_t> data(params_.template_size_bytes, 'a');
  record_value.SetStringKey("data", base::Base64Encode(data));

  std::string record;
  JSONStringValueSerializer json_serializer(&record);
  EXPECT_TRUE(json_serializer.Serialize(record_value));

  EXPECT_TRUE(
      base::ImportantFileWriter::WriteFileAtomically(record_name_, record));
  auto read_result = biod_storage_->ReadRecordsForSingleUser(kUserId1);
  EXPECT_EQ(read_result.valid_records.size(), 1);
  EXPECT_EQ(read_result.invalid_records.size(), 0);
}

INSTANTIATE_TEST_SUITE_P(
    BiodStorageMemlock,
    BiodStorageMemlockTest,
    testing::Values(
        MemlockTestParams{
            .rlimit_bytes = kRlimitMemlockBytes,
            .template_size_bytes = kElan515TemplateSizeBytes,
        },
        MemlockTestParams{
            .rlimit_bytes = kRlimitMemlockBytes,
            .template_size_bytes = kElan80TemplateSizeBytes,
        },
        MemlockTestParams{
            .rlimit_bytes = kRlimitMemlockBytes,
            .template_size_bytes = kFpc1145TemplateSizeBytes,
        },
        MemlockTestParams{
            .rlimit_bytes = kRlimitMemlockBytes,
            .template_size_bytes = kFpc1025TemplateSizeBytes,
        }));

}  // namespace biod
