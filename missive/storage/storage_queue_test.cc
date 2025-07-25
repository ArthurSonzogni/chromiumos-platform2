// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage_queue.h"

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/hash/hash.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/thread_pool.h>
#include <base/test/task_environment.h>
#include <base/threading/sequence_bound.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <brillo/files/file_util.h>
#include <crypto/sha2.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/analytics/metrics.h"
#include "missive/analytics/metrics_test_util.h"
#include "missive/compression/compression_module.h"
#include "missive/compression/decompression.h"
#include "missive/encryption/test_encryption_module.h"
#include "missive/health/health_module.h"
#include "missive/health/health_module_delegate_mock.h"
#include "missive/proto/record.pb.h"
#include "missive/resources/resource_manager.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_util.h"
#include "missive/util/file.h"
#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/statusor.h"
#include "missive/util/test_support_callbacks.h"

using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::AtMost;
using ::testing::Between;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::Invoke;
using ::testing::Ne;
using ::testing::Property;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::StrEq;
using ::testing::WithArg;
using ::testing::WithoutArgs;

namespace reporting {
namespace {

// Test uploader counter - for generation of unique ids.
std::atomic<int64_t> next_uploader_id{0};

constexpr size_t kCompressionThreshold = 2;
const CompressionInformation::CompressionAlgorithm kCompressionType =
    CompressionInformation::COMPRESSION_SNAPPY;

// Forbidden file/folder names
const char kInvalidFilePrefix[] = "..";

// UMA Id for the test.
constexpr char kUmaId[] = "SomeUmaId";

// Ensure files as specified by the parameters are deleted. Take the same
// parameters as base::FileEnumerator().
template <typename... FileEnumeratorParams>
void EnsureDeletingFiles(FileEnumeratorParams... file_enum_params) {
  base::FileEnumerator dir_enum(file_enum_params...);
  ASSERT_TRUE(DeleteFilesWarnIfFailed(dir_enum));
  // Ensure that the files have been deleted
  ASSERT_TRUE(base::FileEnumerator(
                  std::forward<FileEnumeratorParams>(file_enum_params)...)
                  .Next()
                  .empty());
}

class StorageQueueTest
    : public ::testing::TestWithParam<testing::tuple<size_t /*file_size*/,
                                                     std::string /*dm_token*/,
                                                     bool /*is_debugging*/>> {
 protected:
  void SetUp() override {
    ASSERT_TRUE(location_.CreateUniqueTempDir());
    dm_token_ = testing::get<1>(GetParam());
    options_.set_directory(base::FilePath(location_.GetPath()));

    // Ignore collector UMA unless set explicitly.
    ON_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
            SendToUMA)
        .WillByDefault(Return(true));
    ON_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
            SendPercentageToUMA)
        .WillByDefault(Return(true));
    ON_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
            SendLinearToUMA)
        .WillByDefault(Return(true));

    // Turn uploads to no-ops unless other expectation is set (any later
    // EXPECT_CALL will take precedence over this one).
    EXPECT_CALL(set_mock_uploader_expectations_, Call(_))
        .WillRepeatedly(Invoke([this](UploaderInterface::UploadReason reason) {
          return TestUploader::SetUpDummy(this);
        }));
  }

  void TearDown() override {
    ResetTestStorageQueue();
    // Log next uploader id for possible verification.
    LOG(ERROR) << "Next uploader id=" << next_uploader_id.load();
  }

  // Mock class used for setting upload expectations on it.
  class MockUpload {
   public:
    MockUpload() = default;
    virtual ~MockUpload() = default;
    MOCK_METHOD(void,
                EncounterSeqId,
                (int64_t /*uploader_id*/, int64_t),
                (const));
    MOCK_METHOD(bool,
                UploadRecord,
                (int64_t /*uploader_id*/, int64_t, std::string_view),
                (const));
    MOCK_METHOD(bool,
                UploadRecordFailure,
                (int64_t /*uploader_id*/, int64_t, Status),
                (const));
    MOCK_METHOD(bool,
                UploadGap,
                (int64_t /*uploader_id*/, int64_t, uint64_t),
                (const));
    MOCK_METHOD(void,
                HasUnencryptedCopy,
                (int64_t /*uploader_id*/, Destination, std::string_view),
                (const));
    MOCK_METHOD(void,
                UploadComplete,
                (int64_t /*uploader_id*/, Status),
                (const));
  };

  // Helper class to be wrapped in SequenceBound<..>, in order to make sure
  // all its methods are run on a main sequential task wrapper. As a result,
  // collected information and EXPECT_CALLs to MockUpload are safe - executed on
  // the main test thread.
  class SequenceBoundUpload {
   public:
    explicit SequenceBoundUpload(std::unique_ptr<const MockUpload> mock_upload)
        : mock_upload_(std::move(mock_upload)) {
      DETACH_FROM_SEQUENCE(scoped_checker_);
      upload_progress_.assign("\nStart\n");
    }
    SequenceBoundUpload(const SequenceBoundUpload& other) = delete;
    SequenceBoundUpload& operator=(const SequenceBoundUpload& other) = delete;
    ~SequenceBoundUpload() { DCHECK_CALLED_ON_VALID_SEQUENCE(scoped_checker_); }

    void DoEncounterSeqId(int64_t uploader_id,
                          int64_t sequencing_id,
                          int64_t generation_id) {
      DCHECK_CALLED_ON_VALID_SEQUENCE(scoped_checker_);
      upload_progress_.append("SeqId: ")
          .append(base::NumberToString(sequencing_id))
          .append("/")
          .append(base::NumberToString(generation_id))
          .append("\n");
      mock_upload_->EncounterSeqId(uploader_id, sequencing_id);
    }

    void DoUploadRecord(int64_t uploader_id,
                        int64_t sequencing_id,
                        int64_t generation_id,
                        const Record& record,
                        const std::optional<const Record>& possible_record_copy,
                        base::OnceCallback<void(bool)> processed_cb) {
      DoEncounterSeqId(uploader_id, sequencing_id, generation_id);
      DCHECK_CALLED_ON_VALID_SEQUENCE(scoped_checker_);
      upload_progress_.append("Record: ")
          .append(base::NumberToString(sequencing_id))
          .append("/")
          .append(base::NumberToString(generation_id))
          .append(" '")
          .append(record.data().data(), record.data().size())
          .append("'\n");
      bool success =
          mock_upload_->UploadRecord(uploader_id, sequencing_id, record.data());
      if (success && possible_record_copy.has_value()) {
        const auto& record_copy = possible_record_copy.value();
        upload_progress_.append("Has unencrypted copy: ")
            .append(record_copy.data().data(), record_copy.data().size())
            .append("'\n");
        mock_upload_->HasUnencryptedCopy(uploader_id, record_copy.destination(),
                                         record_copy.data());
      }
      std::move(processed_cb).Run(success);
    }

    void DoUploadRecordFailure(int64_t uploader_id,
                               int64_t sequencing_id,
                               int64_t generation_id,
                               Status status,
                               base::OnceCallback<void(bool)> processed_cb) {
      DCHECK_CALLED_ON_VALID_SEQUENCE(scoped_checker_);
      upload_progress_.append("Failure: ")
          .append(base::NumberToString(sequencing_id))
          .append("/")
          .append(base::NumberToString(generation_id))
          .append(" '")
          .append(status.ToString())
          .append("'\n");
      std::move(processed_cb)
          .Run(mock_upload_->UploadRecordFailure(uploader_id, sequencing_id,
                                                 status));
    }

    void DoUploadGap(int64_t uploader_id,
                     int64_t sequencing_id,
                     int64_t generation_id,
                     uint64_t count,
                     base::OnceCallback<void(bool)> processed_cb) {
      DCHECK_CALLED_ON_VALID_SEQUENCE(scoped_checker_);
      for (uint64_t c = 0; c < count; ++c) {
        DoEncounterSeqId(uploader_id, sequencing_id + static_cast<int64_t>(c),
                         generation_id);
      }
      upload_progress_.append("Gap: ")
          .append(base::NumberToString(sequencing_id))
          .append("/")
          .append(base::NumberToString(generation_id))
          .append(" (")
          .append(base::NumberToString(count))
          .append(")\n");
      std::move(processed_cb)
          .Run(mock_upload_->UploadGap(uploader_id, sequencing_id, count));
    }

    void DoUploadComplete(int64_t uploader_id, Status status) {
      DCHECK_CALLED_ON_VALID_SEQUENCE(scoped_checker_);
      upload_progress_.append("Complete: ")
          .append(status.ToString())
          .append("\n");
      LOG(ERROR) << "TestUploader: " << upload_progress_ << "End\n";
      mock_upload_->UploadComplete(uploader_id, status);
    }

   private:
    const std::unique_ptr<const MockUpload> mock_upload_;

    SEQUENCE_CHECKER(scoped_checker_);

    // Snapshot of data received in this upload (for debug purposes).
    std::string upload_progress_;
  };

  // Uploader interface implementation to be assigned to tests.
  // Note that Storage guarantees that all APIs are executed on the same
  // sequenced task runner (not the main test thread!).
  class TestUploader : public UploaderInterface {
   public:
    // Mapping of <generation id, sequencing id> to matching record digest.
    // Whenever a record is uploaded and includes last record digest, this map
    // should have that digest already recorded. Only the first record in a
    // generation is uploaded without last record digest. "Optional" is set to
    // no-value if there was a gap record instead of a real one.
    struct LastRecordDigest {
      struct Hash {
        size_t operator()(
            const std::pair<int64_t /*generation id */,
                            int64_t /*sequencing id*/>& v) const noexcept {
          const auto& [generation_id, sequencing_id] = v;
          return base::HashCombine(0uL, generation_id, sequencing_id);
        }
      };
      using Map = std::unordered_map<
          std::pair<int64_t /*generation id */, int64_t /*sequencing id*/>,
          std::optional<std::string /*digest*/>,
          Hash>;
    };

    // Helper class for setting up mock uploader expectations of a successful
    // completion.
    class SetUp {
     public:
      SetUp(test::TestCallbackWaiter* waiter, StorageQueueTest* self)
          : uploader_(std::make_unique<TestUploader>(self)),
            uploader_id_(uploader_->uploader_id_),
            waiter_(waiter) {}
      SetUp(const SetUp& other) = delete;
      SetUp& operator=(const SetUp& other) = delete;
      ~SetUp() { CHECK(!uploader_) << "Missed 'Complete' call"; }

      std::unique_ptr<TestUploader> Complete(
          Status status = Status::StatusOK()) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(*uploader_->mock_upload_,
                    UploadComplete(Eq(uploader_id_), Eq(status)))
            .InSequence(uploader_->test_upload_sequence_,
                        uploader_->test_encounter_sequence_)
            .WillOnce(DoAll(
                WithoutArgs(Invoke(waiter_, &test::TestCallbackWaiter::Signal)),
                WithArg<1>(Invoke([](Status status) {
                  LOG(ERROR) << "Completion signaled with status=" << status;
                })),
                WithoutArgs(
                    Invoke([]() { LOG(ERROR) << "Completion signaled"; }))));
        return std::move(uploader_);
      }

      SetUp& Required(int64_t sequencing_id, std::string_view value) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(*uploader_->mock_upload_,
                    UploadRecord(Eq(uploader_id_), Eq(sequencing_id),
                                 StrEq(std::string(value))))
            .InSequence(uploader_->test_upload_sequence_)
            .WillOnce(Return(true));
        return *this;
      }

      SetUp& Possible(int64_t sequencing_id, std::string_view value) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(*uploader_->mock_upload_,
                    UploadRecord(Eq(uploader_id_), Eq(sequencing_id),
                                 StrEq(std::string(value))))
            .Times(Between(0, 1))
            .InSequence(uploader_->test_upload_sequence_)
            .WillRepeatedly(Return(true));
        return *this;
      }

      SetUp& RequiredGap(int64_t sequencing_id, uint64_t count) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(*uploader_->mock_upload_,
                    UploadGap(Eq(uploader_id_), Eq(sequencing_id), Eq(count)))
            .InSequence(uploader_->test_upload_sequence_)
            .WillOnce(Return(true));
        return *this;
      }

      SetUp& PossibleGap(int64_t sequencing_id, uint64_t count) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(*uploader_->mock_upload_,
                    UploadGap(Eq(uploader_id_), Eq(sequencing_id), Eq(count)))
            .Times(Between(0, 1))
            .InSequence(uploader_->test_upload_sequence_)
            .WillRepeatedly(Return(true));
        return *this;
      }

      SetUp& HasUnencryptedCopy(int64_t sequencing_id,
                                Destination destination,
                                std::string_view value) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(*uploader_->mock_upload_,
                    HasUnencryptedCopy(Eq(uploader_id_), Eq(destination),
                                       StrEq(std::string(value))))
            .Times(1)
            .InSequence(uploader_->test_upload_sequence_);
        return *this;
      }

      SetUp& Failure(int64_t sequencing_id, Status error) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(
            *uploader_->mock_upload_,
            UploadRecordFailure(Eq(uploader_id_), Eq(sequencing_id), Eq(error)))
            .InSequence(uploader_->test_upload_sequence_)
            .WillOnce(Return(true));
        return *this;
      }

      // The following two expectations refer to the fact that specific
      // sequencing ids have been encountered, regardless of whether they
      // belonged to records or gaps. The expectations are set on a separate
      // test sequence.
      SetUp& RequiredSeqId(int64_t sequencing_id) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(*uploader_->mock_upload_,
                    EncounterSeqId(Eq(uploader_id_), Eq(sequencing_id)))
            .Times(1)
            .InSequence(uploader_->test_encounter_sequence_);
        return *this;
      }

      SetUp& PossibleSeqId(int64_t sequencing_id) {
        CHECK(uploader_) << "'Complete' already called";
        EXPECT_CALL(*uploader_->mock_upload_,
                    EncounterSeqId(Eq(uploader_id_), Eq(sequencing_id)))
            .Times(Between(0, 1))
            .InSequence(uploader_->test_encounter_sequence_);
        return *this;
      }

     private:
      std::unique_ptr<TestUploader> uploader_;
      const int64_t uploader_id_;
      test::TestCallbackWaiter* const waiter_;
    };

    explicit TestUploader(StorageQueueTest* self)
        : uploader_id_(next_uploader_id.fetch_add(1)),
          last_upload_generation_id_(&self->last_upload_generation_id_),
          last_record_digest_map_(&self->last_record_digest_map_),
          // Allocate MockUpload as raw pointer and immediately wrap it in
          // unique_ptr and pass to SequenceBoundUpload to own.
          // MockUpload outlives TestUploader and is destructed together with
          // SequenceBoundUpload (on a sequenced task runner).
          mock_upload_(new ::testing::NiceMock<const MockUpload>()),
          sequence_bound_upload_(self->main_task_runner_,
                                 base::WrapUnique(mock_upload_)) {
      DETACH_FROM_SEQUENCE(test_uploader_checker_);
    }

    ~TestUploader() override {
      DCHECK_CALLED_ON_VALID_SEQUENCE(test_uploader_checker_);
    }

    void ProcessRecord(EncryptedRecord encrypted_record,
                       ScopedReservation scoped_reservation,
                       base::OnceCallback<void(bool)> processed_cb) override {
      DCHECK_CALLED_ON_VALID_SEQUENCE(test_uploader_checker_);
      auto sequence_information = encrypted_record.sequence_information();
      // Decompress encrypted_wrapped_record if is was compressed.
      WrappedRecord wrapped_record;
      ASSERT_TRUE(encrypted_record.has_compression_information());
      std::string decompressed_record =
          test::DecompressRecord(encrypted_record.encrypted_wrapped_record(),
                                 encrypted_record.compression_information());
      encrypted_record.set_encrypted_wrapped_record(decompressed_record);
      ASSERT_TRUE(wrapped_record.ParseFromString(
          encrypted_record.encrypted_wrapped_record()));

      // Verify compression information is present.

      std::optional<Record> possible_record_copy;
      if (encrypted_record.has_record_copy()) {
        possible_record_copy = encrypted_record.record_copy();
      }
      VerifyRecord(std::move(sequence_information), std::move(wrapped_record),
                   std::move(possible_record_copy), std::move(processed_cb));
    }

    void ProcessGap(SequenceInformation sequence_information,
                    uint64_t count,
                    base::OnceCallback<void(bool)> processed_cb) override {
      DCHECK_CALLED_ON_VALID_SEQUENCE(test_uploader_checker_);
      // Verify generation match.
      if (generation_id_.has_value() &&
          generation_id_.value() != sequence_information.generation_id()) {
        sequence_bound_upload_
            .AsyncCall(&SequenceBoundUpload::DoUploadRecordFailure)
            .WithArgs(uploader_id_, sequence_information.sequencing_id(),
                      sequence_information.generation_id(),
                      Status(error::DATA_LOSS,
                             base::StrCat(
                                 {"Generation id mismatch, expected=",
                                  base::NumberToString(generation_id_.value()),
                                  " actual=",
                                  base::NumberToString(
                                      sequence_information.generation_id())})),
                      std::move(processed_cb));
        return;
      }
      if (!generation_id_.has_value()) {
        generation_id_ = sequence_information.generation_id();
        *last_upload_generation_id_ = sequence_information.generation_id();
      }

      last_record_digest_map_->emplace(
          std::make_pair(sequence_information.sequencing_id(),
                         sequence_information.generation_id()),
          std::nullopt);

      sequence_bound_upload_.AsyncCall(&SequenceBoundUpload::DoUploadGap)
          .WithArgs(uploader_id_, sequence_information.sequencing_id(),
                    sequence_information.generation_id(), count,
                    std::move(processed_cb));
    }

    void Completed(Status status) override {
      DCHECK_CALLED_ON_VALID_SEQUENCE(test_uploader_checker_);
      sequence_bound_upload_.AsyncCall(&SequenceBoundUpload::DoUploadComplete)
          .WithArgs(uploader_id_, status);
    }

    // Helper method for setting up dummy mock uploader expectations.
    // To be used only for uploads that we want to just ignore and do not care
    // about their outcome.
    static std::unique_ptr<TestUploader> SetUpDummy(StorageQueueTest* self) {
      auto uploader = std::make_unique<TestUploader>(self);
      // Any Record, RecordFailure of Gap could be encountered, and
      // returning false will cut the upload short.
      EXPECT_CALL(*uploader->mock_upload_,
                  UploadRecord(Eq(uploader->uploader_id_), _, _))
          .InSequence(uploader->test_upload_sequence_)
          .WillRepeatedly(Return(false));
      EXPECT_CALL(*uploader->mock_upload_,
                  UploadRecordFailure(Eq(uploader->uploader_id_), _, _))
          .InSequence(uploader->test_upload_sequence_)
          .WillRepeatedly(Return(false));
      EXPECT_CALL(*uploader->mock_upload_,
                  UploadGap(Eq(uploader->uploader_id_), _, _))
          .InSequence(uploader->test_upload_sequence_)
          .WillRepeatedly(Return(false));
      // Complete will always happen last (whether records/gaps were
      // encountered or not).
      EXPECT_CALL(*uploader->mock_upload_,
                  UploadComplete(Eq(uploader->uploader_id_), _))
          .Times(1)
          .InSequence(uploader->test_upload_sequence_);
      return uploader;
    }

   private:
    void VerifyRecord(SequenceInformation sequence_information,
                      WrappedRecord wrapped_record,
                      std::optional<const Record> possible_record_copy,
                      base::OnceCallback<void(bool)> processed_cb) {
      DCHECK_CALLED_ON_VALID_SEQUENCE(test_uploader_checker_);
      // Verify generation match.
      if (generation_id_.has_value() &&
          generation_id_.value() != sequence_information.generation_id()) {
        sequence_bound_upload_
            .AsyncCall(&SequenceBoundUpload::DoUploadRecordFailure)
            .WithArgs(uploader_id_, sequence_information.sequencing_id(),
                      sequence_information.generation_id(),
                      Status(error::DATA_LOSS,
                             base::StrCat(
                                 {"Generation id mismatch, expected=",
                                  base::NumberToString(generation_id_.value()),
                                  " actual=",
                                  base::NumberToString(
                                      sequence_information.generation_id())})),
                      std::move(processed_cb));
        return;
      }
      if (!generation_id_.has_value()) {
        generation_id_ = sequence_information.generation_id();
        *last_upload_generation_id_ = sequence_information.generation_id();
      }

      // Verify local elements are not included in Record.
      CHECK_EQ(wrapped_record.record().has_reserved_space(), 0);
      CHECK(!wrapped_record.record().needs_local_unencrypted_copy());

      // Verify digest and its match.
      {
        std::string serialized_record;
        wrapped_record.record().SerializeToString(&serialized_record);
        const auto record_digest = crypto::SHA256HashString(serialized_record);
        CHECK_EQ(record_digest.size(), crypto::kSHA256Length);
        if (record_digest != wrapped_record.record_digest()) {
          sequence_bound_upload_
              .AsyncCall(&SequenceBoundUpload::DoUploadRecordFailure)
              .WithArgs(uploader_id_, sequence_information.sequencing_id(),
                        sequence_information.generation_id(),
                        Status(error::DATA_LOSS, "Record digest mismatch"),
                        std::move(processed_cb));
          return;
        }
        // Store record digest for the next record in sequence to
        // verify.
        last_record_digest_map_->emplace(
            std::make_pair(sequence_information.sequencing_id(),
                           sequence_information.generation_id()),
            record_digest);
        // If last record digest is present, match it and validate,
        // unless previous record was a gap.
        if (wrapped_record.has_last_record_digest()) {
          auto it = last_record_digest_map_->find(
              std::make_pair(sequence_information.sequencing_id() - 1,
                             sequence_information.generation_id()));
          if (it == last_record_digest_map_->end() ||
              (it->second.has_value() &&
               it->second.value() != wrapped_record.last_record_digest())) {
            sequence_bound_upload_
                .AsyncCall(&SequenceBoundUpload::DoUploadRecordFailure)
                .WithArgs(
                    uploader_id_, sequence_information.sequencing_id(),
                    sequence_information.generation_id(),
                    Status(error::DATA_LOSS, "Last record digest mismatch"),
                    std::move(processed_cb));
            return;
          }
        }
      }

      sequence_bound_upload_.AsyncCall(&SequenceBoundUpload::DoUploadRecord)
          .WithArgs(uploader_id_, sequence_information.sequencing_id(),
                    sequence_information.generation_id(),
                    wrapped_record.record(), possible_record_copy,
                    std::move(processed_cb));
    }

    SEQUENCE_CHECKER(test_uploader_checker_);

    // Unique ID of the uploader - even if the uploader is allocated
    // on the same address as an earlier one (already released),
    // it will get a new id and thus will ensure the expectations
    // match the expected uploader.
    const int64_t uploader_id_;

    std::optional<int64_t> generation_id_;
    std::optional<int64_t>* const last_upload_generation_id_;
    LastRecordDigest::Map* const last_record_digest_map_;

    const MockUpload* const mock_upload_;
    const base::SequenceBound<SequenceBoundUpload> sequence_bound_upload_;

    Sequence test_encounter_sequence_;
    Sequence test_upload_sequence_;
  };

  void CreateTestStorageQueueOrDie(const QueueOptions& options) {
    ASSERT_FALSE(storage_queue_) << "TestStorageQueue already assigned";
    auto storage_queue_result = CreateTestStorageQueue(options);
    ASSERT_OK(storage_queue_result)
        << "Failed to create TestStorageQueue, error="
        << storage_queue_result.error();
    storage_queue_ = std::move(storage_queue_result.value());
  }

  void CreateTestEncryptionModuleOrDie() {
    test_encryption_module_ =
        base::MakeRefCounted<test::TestEncryptionModule>(/*is_enabled=*/true);
    test::TestEvent<Status> key_update_event;
    test_encryption_module_->UpdateAsymmetricKey("DUMMY KEY", 0,
                                                 key_update_event.cb());
    const auto status = key_update_event.result();
    ASSERT_OK(status) << status;
  }

  // Tries to create a new storage queue by building the test encryption
  // module and returns the corresponding result of the operation.
  StatusOr<scoped_refptr<StorageQueue>> CreateTestStorageQueue(
      const QueueOptions& options,
      const Status& create_directory_status = Status::StatusOK(),
      StorageQueue::InitRetryCb init_retry_cb = base::BindRepeating(
          [](Status init_status,
             size_t retry_count) -> StatusOr<base::TimeDelta> {
            // Do not allow initialization retries.
            return base::unexpected(std::move(init_status));
          })) {
    CreateTestEncryptionModuleOrDie();
    health_module_ =
        HealthModule::Create(std::make_unique<HealthModuleDelegateMock>());
    // Just to check everything works identically with debugging active.
    health_module_->set_debugging(testing::get<2>(GetParam()));
    test::TestEvent<Status> initialized_event;
    const auto storage_queue = StorageQueue::Create({
        .generation_guid = "GENERATION_GUID",
        .options = options,
        .async_start_upload_cb = base::BindRepeating(
            &StorageQueueTest::AsyncStartMockUploader, base::Unretained(this)),
        .degradation_candidates_cb = base::BindRepeating(
            [](scoped_refptr<StorageQueue> queue,
               base::OnceCallback<void(std::queue<scoped_refptr<StorageQueue>>)>
                   result_cb) {
              // Returns empty candidates queue - no degradation allowed.
              std::move(result_cb).Run({});
            }),
        .disconnect_queue_cb = base::BindRepeating(
            [](GenerationGuid generation_guid, base::OnceClosure done_cb) {
              // Finished disconnect.
              std::move(done_cb).Run();
            }),
        .encryption_module = test_encryption_module_,
        .compression_module = CompressionModule::Create(
            /*is_enabled=*/true, kCompressionThreshold, kCompressionType),
        .uma_id = kUmaId,
    });
    auto inject = std::make_unique<::testing::MockFunction<Status(
        test::StorageQueueOperationKind, int64_t)>>();
    // By default return OK status - no error injected.
    EXPECT_CALL(*inject, Call(_, _))
        .WillRepeatedly(WithoutArgs(Return(Status::StatusOK())));
    if (!create_directory_status.ok()) {
      test::TestCallbackAutoWaiter waiter;
      storage_queue->TestInjectErrorsForOperation(
          base::BindOnce(&test::TestCallbackAutoWaiter::Signal,
                         base::Unretained(&waiter)),
          base::BindRepeating(
              &::testing::MockFunction<Status(test::StorageQueueOperationKind,
                                              int64_t)>::Call,
              base::Unretained(inject.get())));
      // Inject simulated failure
      EXPECT_CALL(
          *inject,
          Call(Eq(test::StorageQueueOperationKind::kCreateDirectory), _))
          .WillRepeatedly(WithoutArgs(Invoke([create_directory_status]() {
            return create_directory_status;
          })));
    }

    storage_queue->Init(init_retry_cb, initialized_event.cb());
    RETURN_IF_ERROR_STATUS(base::unexpected(initialized_event.result()));
    return storage_queue;
  }

  void ResetTestStorageQueue() {
    if (storage_queue_) {
      // StorageQueue is destructed on thread, wait for it to finish.
      test::TestCallbackAutoWaiter waiter;
      storage_queue_->RegisterCompletionCallback(base::BindOnce(
          &test::TestCallbackAutoWaiter::Signal, base::Unretained(&waiter)));
      storage_queue_.reset();
    }
    health_module_.reset();
    // Let remaining asynchronous activity finish.
    // TODO(b/254418902): The next line is not logically necessary, but for
    // unknown reason the tests becomes flaky without it, keeping it for now.
    task_environment_.RunUntilIdle();
    // Make sure all memory is deallocated.
    EXPECT_THAT(options_.memory_resource()->GetUsed(), Eq(0u));
    // Make sure all disk is not reserved (files remain, but Storage is not
    // responsible for them anymore).
    EXPECT_THAT(options_.disk_space_resource()->GetUsed(), Eq(0u));
  }

  // Informs the queue about cached events.
  void InformAboutCachedUploads(std::list<int64_t> cached_events_seq_ids) {
    test::TestCallbackAutoWaiter waiter;
    storage_queue_->InformAboutCachedUploads(
        std::move(cached_events_seq_ids),
        base::BindOnce(&test::TestCallbackAutoWaiter::Signal,
                       base::Unretained(&waiter)));
  }

  std::unique_ptr<
      ::testing::MockFunction<Status(test::StorageQueueOperationKind, int64_t)>>
  InjectFailures() {
    auto inject = std::make_unique<::testing::MockFunction<Status(
        test::StorageQueueOperationKind, int64_t)>>();
    // By default return OK status - no error injected.
    EXPECT_CALL(*inject, Call(_, _))
        .WillRepeatedly(WithoutArgs(Return(Status::StatusOK())));
    {
      test::TestCallbackAutoWaiter waiter;
      storage_queue_->TestInjectErrorsForOperation(
          base::BindOnce(&test::TestCallbackAutoWaiter::Signal,
                         base::Unretained(&waiter)),
          base::BindRepeating(
              &::testing::MockFunction<Status(test::StorageQueueOperationKind,
                                              int64_t)>::Call,
              base::Unretained(inject.get())));
    }
    return inject;
  }

  HealthModule::Recorder NewRecorder() { return health_module_->NewRecorder(); }

  QueueOptions BuildStorageQueueOptionsImmediate() const {
    return QueueOptions(options_)
        .set_subdirectory("D1")
        .set_file_prefix("F0001")
        .set_upload_retry_delay(base::TimeDelta())  // No retry by default.
        .set_max_single_file_size(testing::get<0>(GetParam()));
  }

  QueueOptions BuildStorageQueueOptionsPeriodic(
      base::TimeDelta upload_period = base::Seconds(1)) const {
    return BuildStorageQueueOptionsImmediate().set_upload_period(upload_period);
  }

  QueueOptions BuildStorageQueueOptionsOnlyManual() const {
    return BuildStorageQueueOptionsPeriodic(base::TimeDelta::Max());
  }

  void AsyncStartMockUploader(
      UploaderInterface::UploadReason reason,
      UploaderInterface::InformAboutCachedUploadsCb inform_cb,  // unused
      UploaderInterface::UploaderInterfaceResultCb start_uploader_cb) {
    main_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](UploaderInterface::UploadReason reason,
               UploaderInterface::UploaderInterfaceResultCb start_uploader_cb,
               StorageQueueTest* self) {
              LOG(ERROR) << "Attempt upload, reason="
                         << UploaderInterface::ReasonToString(reason);
              auto result = self->set_mock_uploader_expectations_.Call(reason);
              if (!result.has_value()) {
                LOG(ERROR) << "Upload not allowed, reason="
                           << UploaderInterface::ReasonToString(reason) << " "
                           << result.error();
                std::move(start_uploader_cb)
                    .Run(base::unexpected(result.error()));
                return;
              }
              auto uploader = std::move(result.value());
              std::move(start_uploader_cb).Run(std::move(uploader));
            },
            reason, std::move(start_uploader_cb), base::Unretained(this)));
  }

  Status WriteString(std::string_view data) {
    Record record;
    record.set_data(std::string(data));
    record.set_destination(UPLOAD_EVENTS);
    if (!dm_token_.empty()) {
      record.set_dm_token(dm_token_);
    }

    return WriteRecord(std::move(record));
  }

  Status WriteRecord(const Record record) {
    EXPECT_TRUE(storage_queue_) << "StorageQueue not created yet";
    test::TestEvent<Status> write_event;
    LOG(ERROR) << "Write data='" << record.data() << "'";
    storage_queue_->Write(std::move(record), NewRecorder(), write_event.cb());
    return write_event.result();
  }

  void WriteStringOrDie(std::string_view data) {
    const Status write_result = WriteString(data);
    ASSERT_OK(write_result) << write_result;
  }

  void FlushOrDie() {
    test::TestEvent<Status> flush_event;
    storage_queue_->Flush(flush_event.cb());
    ASSERT_OK(flush_event.result());
  }

  void ConfirmOrDie(int64_t sequencing_id, bool force = false) {
    ASSERT_TRUE(last_upload_generation_id_.has_value());
    LOG(ERROR) << "Confirm force=" << force << " seq=" << sequencing_id
               << " gen=" << last_upload_generation_id_.value();
    SequenceInformation seq_info;
    seq_info.set_sequencing_id(sequencing_id);
    seq_info.set_generation_id(last_upload_generation_id_.value());
    // Do not set priority!
    test::TestEvent<Status> c;
    storage_queue_->Confirm(std::move(seq_info), force, NewRecorder(), c.cb());
    const Status c_result = c.result();
    ASSERT_OK(c_result) << c_result;
  }

  void DeleteGenerationIdFromRecordFilePaths(const QueueOptions options) {
    // Remove the generation id from the path of all data files in the storage
    // queue directory
    std::string file_prefix_regex =
        base::StrCat({"*", options.file_prefix(), "*"});
    base::FileEnumerator dir_enum(
        options.directory(),
        /*recursive=*/false, base::FileEnumerator::FILES, file_prefix_regex);
    for (auto file_path = dir_enum.Next(); !file_path.empty();
         file_path = dir_enum.Next()) {
      const auto file_path_without_generation_id =
          base::FilePath(base::StrCat({file_path.RemoveFinalExtension()
                                           .RemoveFinalExtension()
                                           .MaybeAsASCII(),
                                       file_path.FinalExtension()}));
      ASSERT_TRUE(Move(file_path, file_path_without_generation_id));
    }
  }

  std::string dm_token_;
  scoped_refptr<HealthModule> health_module_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  // Sequenced task runner where all EXPECTs will happen.
  const scoped_refptr<base::SequencedTaskRunner> main_task_runner_{
      base::SequencedTaskRunner::GetCurrentDefault()};

  analytics::Metrics::TestEnvironment metrics_test_environment_;

  base::ScopedTempDir location_;
  StorageOptions options_;
  scoped_refptr<test::TestEncryptionModule> test_encryption_module_;
  scoped_refptr<StorageQueue> storage_queue_;
  std::optional<int64_t> last_upload_generation_id_;

  // Test-wide global mapping of <generation id, sequencing id> to record
  // digest. Serves all TestUploaders created by test fixture.
  TestUploader::LastRecordDigest::Map last_record_digest_map_;

  // Mock to be called for setting up the uploader.
  ::testing::MockFunction<StatusOr<std::unique_ptr<TestUploader>>(
      UploaderInterface::UploadReason /*reason*/)>
      set_mock_uploader_expectations_;
};

constexpr std::array<const char*, 3> kData = {"Rec1111", "Rec222", "Rec33"};
constexpr std::array<const char*, 3> kMoreData = {"More1111", "More222",
                                                  "More33"};

TEST_P(StorageQueueTest, WriteIntoStorageQueueAndReopen) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  ResetTestStorageQueue();

  // Init resume upload upon non-empty queue restart.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kData[0])
            .Required(1, kData[1])
            .Required(2, kData[2])
            .Complete();
      }))
      .RetiresOnSaturation();

  // Reopening will cause INIT_RESUME
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
}

TEST_P(StorageQueueTest, WriteIntoStorageQueueReopenAndWriteMore) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  ResetTestStorageQueue();

  // Init resume upload upon non-empty queue restart.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Reopening will cause INIT_RESUME
    CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  }

  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);
}

TEST_P(StorageQueueTest, WriteIntoStorageQueueAndUpload) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  // Set uploader expectations.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kData[0])
            .Required(1, kData[1])
            .Required(2, kData[2])
            .Complete();
      }))
      .RetiresOnSaturation();

  // Trigger upload.
  task_environment_.FastForwardBy(base::Seconds(1));
}

TEST_P(StorageQueueTest, WriteIntoStorageQueueAndUploadWithCache) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  // Inform the queue about cached events.
  InformAboutCachedUploads({});

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Trigger upload.
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Inform the queue about cached events.
  InformAboutCachedUploads({1, 2});

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Trigger upload.
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Inform the queue about cached events.
  InformAboutCachedUploads({1});

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Trigger upload.
    task_environment_.FastForwardBy(base::Seconds(1));
  }
}

TEST_P(StorageQueueTest, WriteIntoStorageQueueAndUploadWithFailures) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  // Inject simulated failures.
  auto inject = InjectFailures();
  EXPECT_CALL(*inject,
              Call(Eq(test::StorageQueueOperationKind::kReadBlock), Eq(1)))
      .WillRepeatedly(WithArg<1>(Invoke([](int64_t seq_id) {
        return Status(error::INTERNAL,
                      base::StrCat({"Simulated read failure, seq=",
                                    base::NumberToString(seq_id)}));
      })));

  // Set uploader expectations.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kData[0])
            .RequiredGap(1, 1)
            .Possible(2, kData[2])  // Depending on records binpacking
            .Complete();
      }))
      .RetiresOnSaturation();

  // Trigger upload.
  task_environment_.FastForwardBy(base::Seconds(1));
}

TEST_P(StorageQueueTest, WriteIntoStorageQueueReopenWriteMoreAndUpload) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  ResetTestStorageQueue();

  // Init resume upload upon non-empty queue restart.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Reopening will cause INIT_RESUME
    CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  }

  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  // Set uploader expectations.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kData[0])
            .Required(1, kData[1])
            .Required(2, kData[2])
            .Required(3, kMoreData[0])
            .Required(4, kMoreData[1])
            .Required(5, kMoreData[2])
            .Complete();
      }))
      .RetiresOnSaturation();

  // Trigger upload.
  task_environment_.FastForwardBy(base::Seconds(1));
}

TEST_P(StorageQueueTest,
       WriteIntoStorageQueueReopenWithMissingMetadataWriteMoreAndUpload) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  // Save copy of options.
  const QueueOptions options = storage_queue_->options();

  ResetTestStorageQueue();

  // Delete all metadata files.
  EnsureDeletingFiles(
      options.directory(),
      /*recursive=*/false, base::FileEnumerator::FILES,
      base::StrCat({StorageDirectory::kMetadataFileNamePrefix, ".*"}));

  // Avoid init resume upload upon non-empty queue restart.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
        .WillOnce(Invoke([&waiter](UploaderInterface::UploadReason reason) {
          waiter.Signal();
          return base::unexpected(
              Status(error::UNAVAILABLE, "Skipped upload in test"));
        }))
        .RetiresOnSaturation();

    // Reopen, starting a new generation.
    CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  }

  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  // Set uploader expectations. Previous data is all lost.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kData[0])
            .Required(1, kData[1])
            .Required(2, kData[2])
            .Required(3, kMoreData[0])
            .Required(4, kMoreData[1])
            .Required(5, kMoreData[2])
            .Complete();
      }))
      .RetiresOnSaturation();

  // Trigger upload.
  task_environment_.FastForwardBy(base::Seconds(1));
}

TEST_P(StorageQueueTest,
       WriteIntoStorageQueueReopenWithMissingLastMetadataWriteMoreAndUpload) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  // Save copy of options.
  const QueueOptions options = storage_queue_->options();

  ResetTestStorageQueue();

  // Delete the last metadata file.
  {  // scoping this block so that dir_enum is not used later.
    const auto last_metadata_file_pattern =
        base::StrCat({StorageDirectory::kMetadataFileNamePrefix, ".2"});
    base::FileEnumerator dir_enum(options.directory(),
                                  /*recursive=*/false,
                                  base::FileEnumerator::FILES,
                                  last_metadata_file_pattern);
    base::FilePath full_name = dir_enum.Next();
    ASSERT_FALSE(full_name.empty())
        << "No file matches " << last_metadata_file_pattern;
    ASSERT_TRUE(dir_enum.Next().empty())
        << full_name << " is not the last metadata file in "
        << options.directory();
    ASSERT_TRUE(brillo::DeleteFile(full_name))
        << "Failed to delete " << full_name;
  }

  // Avoid init resume upload upon non-empty queue restart.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
        .WillOnce(Invoke([&waiter](UploaderInterface::UploadReason reason) {
          waiter.Signal();
          return base::unexpected(
              Status(error::UNAVAILABLE, "Skipped upload in test"));
        }))
        .RetiresOnSaturation();

    // Reopen, starting a new generation.
    CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  }

  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  // Set uploader expectations. Previous data is all lost.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kData[0])
            .Required(1, kData[1])
            .Required(2, kData[2])
            .Required(3, kMoreData[0])
            .Required(4, kMoreData[1])
            .Required(5, kMoreData[2])
            .Complete();
      }))
      .RetiresOnSaturation();

  // Trigger upload.
  task_environment_.FastForwardBy(base::Seconds(1));
}

TEST_P(StorageQueueTest,
       WriteIntoStorageQueueReopenWithMissingDataWriteMoreAndUpload) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  // Save copy of options.
  const QueueOptions options = storage_queue_->options();

  ResetTestStorageQueue();

  // Avoid init resume upload upon non-empty queue restart.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
        .WillOnce(Invoke([&waiter](UploaderInterface::UploadReason reason) {
          waiter.Signal();
          return base::unexpected(
              Status(error::UNAVAILABLE, "Skipped upload in test"));
        }))
        .RetiresOnSaturation();

    // Reopen with the same generation and sequencing information.
    CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  }

  // Delete the data files *.generation.0
  EnsureDeletingFiles(options.directory(),
                      /*recursive=*/false, base::FileEnumerator::FILES,
                      base::StrCat({options.file_prefix(), ".*.0"}));

  // Write more data.
  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  // Set uploader expectations. Previous data is all lost.
  // The expected results depend on the test configuration.
  test::TestCallbackAutoWaiter waiter;
  switch (options.max_single_file_size()) {
    case 1:  // single record in file - deletion killed the first record
      EXPECT_CALL(set_mock_uploader_expectations_,
                  Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
          .WillOnce(
              Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
                return TestUploader::SetUp(&waiter, this)
                    .PossibleGap(0, 1)
                    .Required(1, kData[1])
                    .Required(2, kData[2])
                    .Required(3, kMoreData[0])
                    .Required(4, kMoreData[1])
                    .Required(5, kMoreData[2])
                    .Complete();
              }))
          .RetiresOnSaturation();
      break;
    case 256:  // two records in file - deletion killed the first two records.
               // Can bring gap of 2 records or 2 gaps 1 record each.
      EXPECT_CALL(set_mock_uploader_expectations_,
                  Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
          .WillOnce(
              Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
                return TestUploader::SetUp(&waiter, this)
                    .PossibleGap(0, 1)
                    .PossibleGap(1, 1)
                    .PossibleGap(0, 2)
                    .Failure(2, Status(error::DATA_LOSS,
                                       "Last record digest mismatch"))
                    .Required(3, kMoreData[0])
                    .Required(4, kMoreData[1])
                    .Required(5, kMoreData[2])
                    .Complete();
              }))
          .RetiresOnSaturation();
      break;
    default:  // Unlimited file size - deletion above killed all the data. Can
              // bring gap of 1-6 records.
      EXPECT_CALL(set_mock_uploader_expectations_,
                  Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
          .WillOnce(
              Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
                return TestUploader::SetUp(&waiter, this)
                    .PossibleGap(0, 1)
                    .PossibleGap(0, 2)
                    .PossibleGap(0, 3)
                    .PossibleGap(0, 4)
                    .PossibleGap(0, 5)
                    .PossibleGap(0, 6)
                    .Complete();
              }))
          .RetiresOnSaturation();
  }

  // Trigger upload.
  task_environment_.FastForwardBy(base::Seconds(1));
}

TEST_P(StorageQueueTest, WriteIntoStorageQueueAndFlush) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  // Set uploader expectations.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::MANUAL)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kData[0])
            .Required(1, kData[1])
            .Required(2, kData[2])
            .Complete();
      }))
      .RetiresOnSaturation();

  // Flush manually.
  FlushOrDie();
}

TEST_P(StorageQueueTest, WriteIntoStorageQueueReopenWriteMoreAndFlush) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  ResetTestStorageQueue();

  // Avoid init resume upload upon non-empty queue restart.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
        .WillOnce(Invoke([&waiter](UploaderInterface::UploadReason reason) {
          waiter.Signal();
          return base::unexpected(
              Status(error::UNAVAILABLE, "Skipped upload in test"));
        }))
        .RetiresOnSaturation();

    CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());
  }

  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  // Set uploader expectations.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::MANUAL)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kData[0])
            .Required(1, kData[1])
            .Required(2, kData[2])
            .Required(3, kMoreData[0])
            .Required(4, kMoreData[1])
            .Required(5, kMoreData[2])
            .Complete();
      }))
      .RetiresOnSaturation();

  // Flush manually.
  FlushOrDie();
}

TEST_P(StorageQueueTest, ValidateVariousRecordSizes) {
  std::vector<std::string> data;
  for (size_t i = 16; i < 16 + 16; ++i) {
    data.emplace_back(i, 'R');
  }
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());
  for (const auto& record : data) {
    WriteStringOrDie(record);
  }

  // Set uploader expectations.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::MANUAL)))
      .WillOnce(Invoke(
          [&waiter, &data, this](UploaderInterface::UploadReason reason) {
            TestUploader::SetUp uploader_setup(&waiter, this);
            for (size_t i = 0; i < data.size(); ++i) {
              uploader_setup.Required(i, data[i]);
            }
            return uploader_setup.Complete();
          }))
      .RetiresOnSaturation();

  // Flush manually.
  FlushOrDie();
}

TEST_P(StorageQueueTest, WriteAndRepeatedlyUploadWithConfirmations) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());

  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }
  // Confirm #0 and forward time again, removing record #0
  ConfirmOrDie(/*sequencing_id=*/0);
  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #1 and forward time again, removing record #1
  ConfirmOrDie(/*sequencing_id=*/1);
  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Add more data and verify that #2 and new data are returned.
  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(2, kData[2])
                  .Required(3, kMoreData[0])
                  .Required(4, kMoreData[1])
                  .Required(5, kMoreData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #2 and forward time again, removing record #2
  ConfirmOrDie(/*sequencing_id=*/2);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(3, kMoreData[0])
                  .Required(4, kMoreData[1])
                  .Required(5, kMoreData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    task_environment_.FastForwardBy(base::Seconds(1));
  }
}

TEST_P(StorageQueueTest, WriteAndUploadWithBadConfirmation) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());

  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #0 with bad generation.
  test::TestEvent<Status> c;
  SequenceInformation seq_info;
  seq_info.set_sequencing_id(/*sequencing_id=*/0);
  // Do not set priority and generation!
  LOG(ERROR) << "Bad confirm seq=" << seq_info.sequencing_id();
  storage_queue_->Confirm(std::move(seq_info), /*force=*/false, NewRecorder(),
                          c.cb());
  const Status c_result = c.result();
  ASSERT_FALSE(c_result.ok()) << c_result;
}

TEST_P(StorageQueueTest, WriteAndRepeatedlyUploadWithConfirmationsAndReopen) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());

  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #0 and forward time again, removing record #0
  ConfirmOrDie(/*sequencing_id=*/0);
  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #1 and forward time again, removing record #1
  ConfirmOrDie(/*sequencing_id=*/1);
  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  ResetTestStorageQueue();

  // Avoid init resume upload upon non-empty queue restart.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
        .WillOnce(Invoke([&waiter](UploaderInterface::UploadReason reason) {
          waiter.Signal();
          return base::unexpected(
              Status(error::UNAVAILABLE, "Skipped upload in test"));
        }))
        .RetiresOnSaturation();

    CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  }

  // Add more data and verify that #2 and new data are returned.
  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Possible(0, kData[0])
                  .Possible(1, kData[1])
                  .Required(2, kData[2])
                  .Required(3, kMoreData[0])
                  .Required(4, kMoreData[1])
                  .Required(5, kMoreData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #2 and forward time again, removing record #2
  ConfirmOrDie(/*sequencing_id=*/2);

  {
    test::TestCallbackAutoWaiter waiter;
    // Set uploader expectations.
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(3, kMoreData[0])
                  .Required(4, kMoreData[1])
                  .Required(5, kMoreData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    task_environment_.FastForwardBy(base::Seconds(1));
  }
}

TEST_P(StorageQueueTest,
       WriteAndRepeatedlyUploadWithConfirmationsAndReopenWithFailures) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());

  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #0 and forward time again, removing record #0
  ConfirmOrDie(/*sequencing_id=*/0);
  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #1 and forward time again, removing record #1
  ConfirmOrDie(/*sequencing_id=*/1);
  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  ResetTestStorageQueue();

  // Avoid init resume upload upon non-empty queue restart.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INIT_RESUME)))
        .WillOnce(Invoke([&waiter](UploaderInterface::UploadReason reason) {
          waiter.Signal();
          return base::unexpected(
              Status(error::UNAVAILABLE, "Skipped upload in test"));
        }))
        .RetiresOnSaturation();

    CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  }

  // Add more data and verify that #2 and new data are returned.
  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  // Inject simulated failures.
  auto inject = InjectFailures();
  EXPECT_CALL(*inject, Call(Eq(test::StorageQueueOperationKind::kReadBlock),
                            AnyOf(4, 5)))
      .WillRepeatedly(WithArg<1>(Invoke([](int64_t seq_id) {
        return Status(error::INTERNAL,
                      base::StrCat({"Simulated read failure, seq=",
                                    base::NumberToString(seq_id)}));
      })));

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Possible(0, kData[0])
                  .Possible(1, kData[1])
                  .Required(2, kData[2])
                  .Required(3, kMoreData[0])
                  // Gap may be 2 records at once or 2 gaps 1 record each.
                  .PossibleGap(4, 2)
                  .PossibleGap(4, 1)
                  .PossibleGap(5, 1)
                  .Complete();
            }))
        .RetiresOnSaturation();
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #2 and forward time again, removing record #2
  ConfirmOrDie(/*sequencing_id=*/2);

  // Reset error injection.
  {
    test::TestCallbackAutoWaiter waiter;
    storage_queue_->TestInjectErrorsForOperation(base::BindOnce(
        &test::TestCallbackAutoWaiter::Signal, base::Unretained(&waiter)));
  }

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(3, kMoreData[0])
                  .Required(4, kMoreData[1])
                  .Required(5, kMoreData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    task_environment_.FastForwardBy(base::Seconds(1));
  }
}

TEST_P(StorageQueueTest, WriteAndRepeatedlyImmediateUpload) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsImmediate());

  // Upload is initiated asynchronously, so it may happen after the next
  // record is also written. Because of that we set expectations for the
  // data after the current one as |Possible|.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Possible(1, kData[1])
                  .Possible(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kData[0]);
  }

  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Possible(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kData[1]);
  }

  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kData[2]);
  }
}

TEST_P(StorageQueueTest, WriteAndRepeatedlyImmediateUploadWithConfirmations) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsImmediate());

  // Upload is initiated asynchronously, so it may happen after the next
  // record is also written. Because of the Confirmation below, we set
  // expectations for the data that may be eliminated by Confirmation as
  // |Possible|.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kData[0]);
  }

  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kData[1]);
  }

  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kData[2]);
  }

  // Confirm #1, removing data #0 and #1
  ConfirmOrDie(/*sequencing_id=*/1);

  // Add more data to verify that #2 and new data are returned.
  // Upload is initiated asynchronously, so it may happen after the next
  // record is also written. Because of that we set expectations for the
  // data after the current one as |Possible|.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(2, kData[2])
                  .Required(3, kMoreData[0])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kMoreData[0]);
  }

  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(2, kData[2])
                  .Required(3, kMoreData[0])
                  .Required(4, kMoreData[1])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kMoreData[1]);
  }

  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(2, kData[2])
                  .Required(3, kMoreData[0])
                  .Required(4, kMoreData[1])
                  .Required(5, kMoreData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kMoreData[2]);
  }
}

TEST_P(StorageQueueTest, WriteAndImmediateUploadWithFailure) {
  CreateTestStorageQueueOrDie(
      BuildStorageQueueOptionsImmediate().set_upload_retry_delay(
          base::Seconds(1)));

  // Write a record as Immediate, initiating an upload which fails
  // and then restarts.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(Invoke([](UploaderInterface::UploadReason reason) {
          return base::unexpected(
              Status(error::UNAVAILABLE, "Intended failure in test"));
        }))
        .RetiresOnSaturation();
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::FAILURE_RETRY)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kData[0]);  // Immediately uploads and fails.
    // Let it retry upload and verify.
    task_environment_.FastForwardBy(base::Seconds(1));
  }
}

TEST_P(StorageQueueTest, WriteAndImmediateUploadWithoutConfirmation) {
  CreateTestStorageQueueOrDie(
      BuildStorageQueueOptionsImmediate().set_upload_retry_delay(
          base::Seconds(5)));

  // Write a record as Immediate, initiating an upload which fails
  // and then restarts.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::IMMEDIATE_FLUSH)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Complete();
            }))
        .RetiresOnSaturation();
    WriteStringOrDie(kData[0]);  // Immediately uploads and does not confirm.
  }

  // Let it retry upload and verify.
  {
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::INCOMPLETE_RETRY)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Complete();
            }))
        .RetiresOnSaturation();
    task_environment_.FastForwardBy(base::Seconds(5));
  }

  // Confirm 0 and make sure no retry happens (since everything is confirmed).
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::INCOMPLETE_RETRY)))
      .Times(0);

  ConfirmOrDie(/*sequencing_id=*/0);
  task_environment_.FastForwardBy(base::Seconds(10));
}

TEST_P(StorageQueueTest, WriteEncryptFailure) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  CHECK(test_encryption_module_);
  EXPECT_CALL(*test_encryption_module_, EncryptRecordImpl(_, _))
      .WillOnce(WithArg<1>(
          Invoke([](base::OnceCallback<void(StatusOr<EncryptedRecord>)> cb) {
            std::move(cb).Run(
                base::unexpected(Status(error::UNKNOWN, "Failing for tests")));
          })));
  const Status result = WriteString("TEST_MESSAGE");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error_code(), error::UNKNOWN);
}

TEST_P(StorageQueueTest, ForceConfirm) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());

  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(0, kData[0])
                  .Required(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Confirm #1 and forward time again, possibly removing records #0 and #1
  ConfirmOrDie(/*sequencing_id=*/1);

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Now force confirm the very beginning and forward time again.
  ConfirmOrDie(/*sequencing_id=*/-1, /*force=*/true);

  {
    // Set uploader expectations.
    // #0 and #1 could be returned as Gaps
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .RequiredSeqId(0)
                  .RequiredSeqId(1)
                  .RequiredSeqId(2)
                  // 0-2 must have been encountered, but actual contents
                  // can be different:
                  .Possible(0, kData[0])
                  .PossibleGap(0, 1)
                  .PossibleGap(0, 2)
                  .Possible(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }

  // Force confirm #0 and forward time again.
  ConfirmOrDie(/*sequencing_id=*/0, /*force=*/true);

  {
    // Set uploader expectations.
    // #0 and #1 could be returned as Gaps
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              return TestUploader::SetUp(&waiter, this)
                  .RequiredSeqId(1)
                  .RequiredSeqId(2)
                  // 0-2 must have been encountered, but actual contents
                  // can be different:
                  .PossibleGap(1, 1)
                  .Possible(1, kData[1])
                  .Required(2, kData[2])
                  .Complete();
            }))
        .RetiresOnSaturation();

    // Forward time to trigger upload
    task_environment_.FastForwardBy(base::Seconds(1));
  }
}

TEST_P(StorageQueueTest, WriteInvalidRecord) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  const Record invalid_record;
  Status write_result = WriteRecord(std::move(invalid_record));
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::FAILED_PRECONDITION);
}

TEST_P(StorageQueueTest, WriteRecordWithNoData) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  Record record;
  record.set_destination(UPLOAD_EVENTS);
  Status write_result = WriteRecord(std::move(record));
  EXPECT_OK(write_result);
}

TEST_P(StorageQueueTest, WriteRecordWithWriteMetadataFailures) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());

  auto inject = InjectFailures();
  EXPECT_CALL(*inject,
              Call(Eq(test::StorageQueueOperationKind::kWriteMetadata), Eq(0)))
      .WillOnce(WithArg<1>(Invoke([](int64_t seq_id) {
        return Status(error::INTERNAL,
                      base::StrCat({"Simulated metadata write failure, seq=",
                                    base::NumberToString(seq_id)}));
      })));

  Status write_result = WriteString(kData[0]);
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::INTERNAL);
}

TEST_P(StorageQueueTest, WriteRecordWithWriteBlockFailures) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());

  auto inject = InjectFailures();
  EXPECT_CALL(*inject,
              Call(Eq(test::StorageQueueOperationKind::kWriteBlock), Eq(0)))
      .WillOnce(WithArg<1>(Invoke([](int64_t seq_id) {
        return Status(error::INTERNAL,
                      base::StrCat({"Simulated write failure, seq=",
                                    base::NumberToString(seq_id)}));
      })));

  Status write_result = WriteString(kData[0]);
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::INTERNAL);
}

TEST_P(StorageQueueTest, WriteRecordWithInvalidFilePrefix) {
  CreateTestStorageQueueOrDie(
      BuildStorageQueueOptionsPeriodic().set_file_prefix(kInvalidFilePrefix));
  Status write_result = WriteString(kData[0]);
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::ALREADY_EXISTS);
}

TEST_P(StorageQueueTest, CreateStorageQueueInvalidOptionsPath) {
  StatusOr<scoped_refptr<StorageQueue>> queue_result = CreateTestStorageQueue(
      BuildStorageQueueOptionsPeriodic(),
      Status(error::UNAVAILABLE, "Wrong directory path"));
  EXPECT_FALSE(queue_result.has_value());
  EXPECT_EQ(queue_result.error().error_code(), error::UNAVAILABLE);
}

TEST_P(StorageQueueTest, CreateStorageQueueAllRetriesFail) {
  auto init_retry_cb = base::BindRepeating(
      [](base::RepeatingCallback<void(base::TimeDelta)> forward_cb,
         Status init_status, size_t retry_count) -> StatusOr<base::TimeDelta> {
        forward_cb.Run(base::Seconds(1));
        return base::Seconds(1);  // Retry allowed
      },
      base::BindPostTaskToCurrentDefault(
          base::BindRepeating(&base::test::TaskEnvironment::FastForwardBy,
                              base::Unretained(&task_environment_))));
  StatusOr<scoped_refptr<StorageQueue>> queue_result = CreateTestStorageQueue(
      BuildStorageQueueOptionsPeriodic(),
      Status(error::UNAVAILABLE, "Wrong directory path"), init_retry_cb);
  EXPECT_FALSE(queue_result.has_value());
  EXPECT_EQ(queue_result.error().error_code(), error::UNAVAILABLE);
}

TEST_P(StorageQueueTest, CreateStorageQueueMultipleTimesRace) {
  static constexpr size_t kThreads = 128;
  // Populate multiple instances of `StorageQueue` (synchronously) without
  // initialization.
  std::array<scoped_refptr<StorageQueue>, kThreads> queues;
  CreateTestEncryptionModuleOrDie();
  health_module_ =
      HealthModule::Create(std::make_unique<HealthModuleDelegateMock>());
  // Just to check everything works identically with debugging active.
  health_module_->set_debugging(testing::get<2>(GetParam()));
  const StorageQueue::Settings queue_settings{
      .generation_guid = "GENERATION_GUID",
      .options = BuildStorageQueueOptionsOnlyManual(),
      .async_start_upload_cb = base::BindRepeating(
          &StorageQueueTest_CreateStorageQueueMultipleTimesRace_Test::
              AsyncStartMockUploader,
          base::Unretained(this)),
      .degradation_candidates_cb = base::BindRepeating(
          [](scoped_refptr<StorageQueue> queue,
             base::OnceCallback<void(std::queue<scoped_refptr<StorageQueue>>)>
                 result_cb) {
            // Returns empty candidates queue - no degradation allowed.
            std::move(result_cb).Run({});
          }),
      .disconnect_queue_cb = base::BindRepeating(
          [](GenerationGuid generation_guid, base::OnceClosure done_cb) {
            // Finished disconnect.
            std::move(done_cb).Run();
          }),
      .encryption_module = test_encryption_module_,
      .compression_module = CompressionModule::Create(
          /*is_enabled=*/true, kCompressionThreshold, kCompressionType),
      .uma_id = kUmaId,
  };
  for (size_t i = 0; i < kThreads; ++i) {
    queues[i] = StorageQueue::Create(queue_settings);
  }
  // Initialize all instances in parallel with the same settings (options).
  std::array<test::TestEvent<Status>, kThreads> init_events;
  const StorageQueue::InitRetryCb init_retry_cb = base::BindRepeating(
      [](Status init_status, size_t retry_count) -> StatusOr<base::TimeDelta> {
        // Do not allow initialization retries.
        return base::unexpected(std::move(init_status));
      });
  for (size_t i = 0; i < kThreads; ++i) {
    base::ThreadPool::PostTask(
        FROM_HERE, base::BindOnce(&StorageQueue::Init, queues[i], init_retry_cb,
                                  init_events[i].cb()));
  }
  // Check that all queues have been initialized with success (to increase
  // chances of a race, in reverse order to the initialization calls).
  for (size_t i = kThreads; i > 0; --i) {
    const auto status = init_events[i - 1].result();
    ASSERT_OK(status) << "Failed to create TestStorageQueue[" << i - 1
                      << "], error=" << status;
  }
}

TEST_P(StorageQueueTest, CreateStorageQueueRetry) {
  // Create a file instead of directory, to make StorageQueue initialization
  // fail.
  base::FilePath bad_file;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(options_.directory(), &bad_file));
  const QueueOptions queue_options =
      BuildStorageQueueOptionsPeriodic().set_subdirectory(
          bad_file.BaseName().value());
  // Allow the retries with backoff several times, and the last time delete
  // the file.
  auto init_retry_cb = base::BindRepeating(
      [](base::RepeatingCallback<void(base::TimeDelta)> forward_cb,
         const base::FilePath& bad_file, Status init_status,
         size_t retry_count) -> StatusOr<base::TimeDelta> {
        if (retry_count == 1) {  // Last attempt.
          EXPECT_TRUE(brillo::DeleteFile(bad_file));
        }
        forward_cb.Run(base::Seconds(1));
        return base::Seconds(1);
      },
      base::BindPostTaskToCurrentDefault(
          base::BindRepeating(&base::test::TaskEnvironment::FastForwardBy,
                              base::Unretained(&task_environment_))),
      bad_file);
  StatusOr<scoped_refptr<StorageQueue>> queue_result =
      CreateTestStorageQueue(queue_options, Status::StatusOK(), init_retry_cb);
  EXPECT_OK(queue_result) << queue_result.error();
}

TEST_P(StorageQueueTest, WriteRecordDataWithInsufficientDiskSpaceFailure) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());

  // Inject simulated failures.
  auto inject = InjectFailures();
  EXPECT_CALL(
      *inject,
      Call(Eq(test::StorageQueueOperationKind::kWriteLowDiskSpace), Eq(0)))
      .WillRepeatedly(WithArg<1>(Invoke([](int64_t seq_id) {
        return Status(error::INTERNAL,
                      base::StrCat({"Simulated data write low disk space, seq=",
                                    base::NumberToString(seq_id)}));
      })));
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(StrEq(StorageQueue::kResourceExhaustedCaseUmaName),
                    Eq(StorageQueue::ResourceExhaustedCase::NO_DISK_SPACE),
                    Eq(StorageQueue::ResourceExhaustedCase::kMaxValue)))
      .WillOnce(Return(true));
  Status write_result = WriteString(kData[0]);
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::RESOURCE_EXHAUSTED);
  task_environment_.RunUntilIdle();  // For asynchronous UMA upload.
}

TEST_P(StorageQueueTest, WriteRecordMetadataWithInsufficientDiskSpaceFailure) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());

  // Inject simulated failures.
  auto inject = InjectFailures();
  EXPECT_CALL(
      *inject,
      Call(Eq(test::StorageQueueOperationKind::kWriteLowDiskSpace), Eq(0)))
      .WillRepeatedly(WithArg<1>(Invoke([](int64_t seq_id) {
        return Status(
            error::INTERNAL,
            base::StrCat({"Simulated metadata write low disk space, seq=",
                          base::NumberToString(seq_id)}));
      })));
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(StrEq(StorageQueue::kResourceExhaustedCaseUmaName),
                    Eq(StorageQueue::ResourceExhaustedCase::NO_DISK_SPACE),
                    Eq(StorageQueue::ResourceExhaustedCase::kMaxValue)))
      .WillOnce(Return(true));
  Status write_result = WriteString(kData[0]);
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::RESOURCE_EXHAUSTED);
  task_environment_.RunUntilIdle();  // For asynchronous UMA upload.
}

TEST_P(StorageQueueTest, WrappedRecordWithInsufficientMemoryWithRetry) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());

  // Inject "low memory" error multiple times, then retire and return success.
  auto inject = InjectFailures();
  static constexpr size_t kAttempts = 3;
  size_t attempts = 0;
  EXPECT_CALL(
      *inject,
      Call(Eq(test::StorageQueueOperationKind::kWrappedRecordLowMemory), Eq(0)))
      .Times(kAttempts)
      .WillRepeatedly(WithArg<1>(Invoke([&attempts](int64_t seq_id) {
        return Status(error::RESOURCE_EXHAUSTED,
                      base::StrCat({"Not enough memory for WrappedRecord, seq=",
                                    base::NumberToString(seq_id), " attempt=",
                                    base::NumberToString(attempts++)}));
      })))
      .RetiresOnSaturation();
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(
          StrEq(StorageQueue::kResourceExhaustedCaseUmaName),
          Eq(StorageQueue::ResourceExhaustedCase::NO_MEMORY_FOR_WRITE_BUFFER),
          Eq(StorageQueue::ResourceExhaustedCase::kMaxValue)))
      .Times(0);  // No UMA call!
  Record record;
  record.set_data(std::string(kData[0]));
  record.set_destination(UPLOAD_EVENTS);
  if (!dm_token_.empty()) {
    record.set_dm_token(dm_token_);
  }
  test::TestEvent<Status> write_event;
  LOG(ERROR) << "Write data='" << record.data() << "'";
  storage_queue_->Write(std::move(record), NewRecorder(), write_event.cb());
  Status write_result = write_event.result();
  EXPECT_OK(write_result) << write_result;
  EXPECT_THAT(attempts, Eq(kAttempts));
  task_environment_.RunUntilIdle();  // For asynchronous UMA upload.
}

TEST_P(StorageQueueTest, WrappedRecordWithInsufficientMemoryWithFailure) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());

  // Inject "low memory" error multiple times, then retire and return success.
  auto inject = InjectFailures();
  EXPECT_CALL(
      *inject,
      Call(Eq(test::StorageQueueOperationKind::kWrappedRecordLowMemory), Eq(0)))
      .WillRepeatedly(WithArg<1>(Invoke([](int64_t seq_id) {
        return Status(error::RESOURCE_EXHAUSTED,
                      base::StrCat({"Not enough memory for WrappedRecord, seq=",
                                    base::NumberToString(seq_id)}));
      })))
      .RetiresOnSaturation();
  EXPECT_CALL(
      analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
      SendEnumToUMA(
          StrEq(StorageQueue::kResourceExhaustedCaseUmaName),
          Eq(StorageQueue::ResourceExhaustedCase::NO_MEMORY_FOR_WRITE_BUFFER),
          Eq(StorageQueue::ResourceExhaustedCase::kMaxValue)))
      .WillOnce(Return(true));
  Record record;
  record.set_data(std::string(kData[0]));
  record.set_destination(UPLOAD_EVENTS);
  if (!dm_token_.empty()) {
    record.set_dm_token(dm_token_);
  }
  test::TestEvent<Status> write_event;
  LOG(ERROR) << "Write data='" << record.data() << "'";
  storage_queue_->Write(std::move(record), NewRecorder(), write_event.cb());
  Status write_result = write_event.result();
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::RESOURCE_EXHAUSTED);
  task_environment_.RunUntilIdle();  // For asynchronous UMA upload.
}

TEST_P(StorageQueueTest, EncryptedRecordWithInsufficientMemoryWithRetry) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());

  // Inject "low memory" error multiple times, then retire and return success.
  auto inject = InjectFailures();
  static constexpr size_t kAttempts = 3;
  size_t attempts = 0;
  EXPECT_CALL(
      *inject,
      Call(Eq(test::StorageQueueOperationKind::kEncryptedRecordLowMemory),
           Eq(0)))
      .Times(kAttempts)
      .WillRepeatedly(WithArg<1>(Invoke([&attempts](int64_t seq_id) {
        return Status(
            error::RESOURCE_EXHAUSTED,
            base::StrCat({"Not enough memory for EncryptedRecord, seq=",
                          base::NumberToString(seq_id),
                          " attempt=", base::NumberToString(attempts++)}));
      })))
      .RetiresOnSaturation();
  EXPECT_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
              SendEnumToUMA(StrEq(StorageQueue::kResourceExhaustedCaseUmaName),
                            Eq(StorageQueue::ResourceExhaustedCase::
                                   NO_MEMORY_FOR_ENCRYPTED_RECORD),
                            Eq(StorageQueue::ResourceExhaustedCase::kMaxValue)))
      .Times(0);  // No UMA call!
  Record record;
  record.set_data(std::string(kData[0]));
  record.set_destination(UPLOAD_EVENTS);
  if (!dm_token_.empty()) {
    record.set_dm_token(dm_token_);
  }
  test::TestEvent<Status> write_event;
  LOG(ERROR) << "Write data='" << record.data() << "'";
  storage_queue_->Write(std::move(record), NewRecorder(), write_event.cb());
  Status write_result = write_event.result();
  EXPECT_OK(write_result) << write_result;
  EXPECT_THAT(attempts, Eq(kAttempts));
  task_environment_.RunUntilIdle();  // For asynchronous UMA upload.
}

TEST_P(StorageQueueTest, EncryptedRecordWithInsufficientMemoryWithFailure) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());

  // Inject "low memory" error multiple times, then retire and return success.
  auto inject = InjectFailures();
  EXPECT_CALL(
      *inject,
      Call(Eq(test::StorageQueueOperationKind::kEncryptedRecordLowMemory),
           Eq(0)))
      .WillRepeatedly(WithArg<1>(Invoke([](int64_t seq_id) {
        return Status(
            error::RESOURCE_EXHAUSTED,
            base::StrCat({"Not enough memory for EncryptedRecord, seq=",
                          base::NumberToString(seq_id)}));
      })))
      .RetiresOnSaturation();
  EXPECT_CALL(analytics::Metrics::TestEnvironment::GetMockMetricsLibrary(),
              SendEnumToUMA(StrEq(StorageQueue::kResourceExhaustedCaseUmaName),
                            Eq(StorageQueue::ResourceExhaustedCase::
                                   NO_MEMORY_FOR_ENCRYPTED_RECORD),
                            Eq(StorageQueue::ResourceExhaustedCase::kMaxValue)))
      .WillOnce(Return(true));
  Record record;
  record.set_data(std::string(kData[0]));
  record.set_destination(UPLOAD_EVENTS);
  if (!dm_token_.empty()) {
    record.set_dm_token(dm_token_);
  }
  test::TestEvent<Status> write_event;
  LOG(ERROR) << "Write data='" << record.data() << "'";
  storage_queue_->Write(std::move(record), NewRecorder(), write_event.cb());
  Status write_result = write_event.result();
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::RESOURCE_EXHAUSTED);
  task_environment_.RunUntilIdle();  // For asynchronous UMA upload.
}

TEST_P(StorageQueueTest, WriteRecordWithReservedSpace) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());

  const auto tatal_disk_space = options_.disk_space_resource()->GetTotal();
  Record record;
  record.set_data(kData[0]);
  record.set_destination(UPLOAD_EVENTS);
  if (!dm_token_.empty()) {
    record.set_dm_token(dm_token_);
  }
  // Large reservation, but still available.
  record.set_reserved_space(tatal_disk_space / 2);
  Status write_result = WriteRecord(record);
  EXPECT_OK(write_result) << write_result;
  // Even larger reservation, not available.
  record.set_reserved_space(tatal_disk_space);
  write_result = WriteRecord(record);
  EXPECT_FALSE(write_result.ok());
  EXPECT_EQ(write_result.error_code(), error::RESOURCE_EXHAUSTED);
}

TEST_P(StorageQueueTest, UploadWithInsufficientMemory) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic(base::Seconds(5))
                                  .set_upload_retry_delay(base::Seconds(1)));
  WriteStringOrDie(kData[0]);

  const auto original_total_memory = options_.memory_resource()->GetTotal();

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
        .WillOnce(
            Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
              // First attempt - update total memory to a low amount.
              options_.memory_resource()->Test_SetTotal(100u);
              return TestUploader::SetUp(&waiter, this)
                  .Complete(Status(error::RESOURCE_EXHAUSTED,
                                   "Insufficient memory for upload"));
            }))
        .RetiresOnSaturation();
    // Trigger upload which will experience insufficient memory.
    task_environment_.FastForwardBy(base::Seconds(5));
  }

  {
    // Set uploader expectations.
    test::TestCallbackAutoWaiter waiter;
    EXPECT_CALL(set_mock_uploader_expectations_,
                Call(Eq(UploaderInterface::UploadReason::FAILURE_RETRY)))
        .WillOnce(Invoke([&waiter, &original_total_memory,
                          this](UploaderInterface::UploadReason reason) {
          // Reset after running upload so it does not affect other tests.
          options_.memory_resource()->Test_SetTotal(original_total_memory);
          return TestUploader::SetUp(&waiter, this)
              .Required(0, kData[0])
              .Complete();
        }))
        .RetiresOnSaturation();

    // Trigger another (failure retry) upload resetting the memory resource.
    task_environment_.FastForwardBy(base::Seconds(1));
  }
}

TEST_P(StorageQueueTest, WriteIntoStorageQueueReopenWithCorruptData) {
  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsPeriodic());
  WriteStringOrDie(kData[0]);
  WriteStringOrDie(kData[1]);
  WriteStringOrDie(kData[2]);

  // Save copy of options.
  const QueueOptions options = storage_queue_->options();

  ResetTestStorageQueue();

  DeleteGenerationIdFromRecordFilePaths(options);

  // All data files should be irreparably corrupt, but we still consider it a
  // success: the queue regenerates.
  CreateTestStorageQueueOrDie(options);

  // Make sure the queue is OK, but old writes are lost.
  WriteStringOrDie(kMoreData[0]);
  WriteStringOrDie(kMoreData[1]);
  WriteStringOrDie(kMoreData[2]);

  // Set uploader expectations.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::PERIODIC)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kMoreData[0])
            .Required(1, kMoreData[1])
            .Required(2, kMoreData[2])
            .Complete();
      }))
      .RetiresOnSaturation();

  // Trigger upload.
  task_environment_.FastForwardBy(base::Seconds(1));
}

TEST_P(StorageQueueTest, WriteWithUnencryptedCopy) {
  static constexpr char kTestData[] = "test_data";

  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());
  Record record;
  record.set_data(kTestData);
  record.set_destination(UPLOAD_EVENTS);
  record.set_needs_local_unencrypted_copy(true);
  if (!dm_token_.empty()) {
    record.set_dm_token(dm_token_);
  }
  const Status write_result = WriteRecord(std::move(record));
  ASSERT_OK(write_result) << write_result;

  // Set uploader expectations.
  test::TestCallbackAutoWaiter waiter;
  EXPECT_CALL(set_mock_uploader_expectations_,
              Call(Eq(UploaderInterface::UploadReason::MANUAL)))
      .WillOnce(Invoke([&waiter, this](UploaderInterface::UploadReason reason) {
        return TestUploader::SetUp(&waiter, this)
            .Required(0, kTestData)
            .HasUnencryptedCopy(0, UPLOAD_EVENTS, kTestData)
            .Complete();
      }))
      .RetiresOnSaturation();

  // Flush manually.
  FlushOrDie();
}

TEST_P(StorageQueueTest, WriteWithNoDestination) {
  static constexpr char kTestData[] = "test_data";

  CreateTestStorageQueueOrDie(BuildStorageQueueOptionsOnlyManual());

  Record record;
  record.set_data(kTestData);
  if (!dm_token_.empty()) {
    record.set_dm_token(dm_token_);
  }

  // Attempt Write with no destination.
  Status write_result = WriteRecord(std::move(record));
  ASSERT_THAT(write_result,
              AllOf(Property(&Status::code, Eq(error::FAILED_PRECONDITION)),
                    Property(&Status::message,
                             StrEq("Malformed record: missing destination"))))
      << write_result;

  // Attempt Write with undefined destination.
  record.set_destination(UNDEFINED_DESTINATION);
  write_result = WriteRecord(std::move(record));
  ASSERT_THAT(write_result,
              AllOf(Property(&Status::code, Eq(error::FAILED_PRECONDITION)),
                    Property(&Status::message,
                             StrEq("Malformed record: missing destination"))))
      << write_result;
}

INSTANTIATE_TEST_SUITE_P(
    VaryingFileSize,
    StorageQueueTest,
    testing::Combine(testing::Values(128 * 1024LL * 1024LL,
                                     256 /* two records in file */,
                                     1 /* single record in file */),
                     testing::Values("DM TOKEN", ""),
                     testing::Bool()));

}  // namespace
}  // namespace reporting
