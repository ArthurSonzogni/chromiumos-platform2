// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage.h"

#include <tuple>
#include <utility>

#include <base/barrier_closure.h>
#include <base/check.h>
#include <base/containers/adapters.h>
#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/platform_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/threading/thread.h>
#include <base/location.h>
#include <base/memory/scoped_refptr.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>
#include <base/uuid.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "missive/encryption/encryption_module_interface.h"
#include "missive/health/health_module.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/storage_base.h"
#include "missive/storage/storage_configuration.h"
#include "missive/storage/storage_queue.h"
#include "missive/storage/storage_util.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"
#include "missive/util/task_runner_context.h"

// Temporary replacement for `Priority_Name` that does
// not work in certain CQ.
// TODO(b/294756107): Remove this function once fixed.
#include "missive/proto/priority_name.h"

namespace reporting {

// Context for creating a single queue. Upon success, calls the callback with
// the GenerationGuid passed into the context, otherwise error status.
class CreateQueueContext : public TaskRunnerContext<StatusOr<GenerationGuid>> {
 public:
  CreateQueueContext(
      Priority priority,
      QueueOptions queue_options,
      scoped_refptr<Storage> storage,
      GenerationGuid generation_guid,
      base::OnceCallback<void(StatusOr<GenerationGuid>)> callback)
      : TaskRunnerContext<StatusOr<GenerationGuid>>(
            std::move(callback),
            storage->sequenced_task_runner_),  // Same runner as the Storage!
        queue_options_(queue_options),
        storage_(storage),
        generation_guid_(generation_guid),
        priority_(priority) {}

  CreateQueueContext(const CreateQueueContext&) = delete;
  CreateQueueContext& operator=(const CreateQueueContext&) = delete;

 private:
  void OnStart() override {
    CheckOnValidSequence();
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
    // Verify this queue doesn't already exist
    CHECK(!storage_->queues_container_->GetQueue(priority_, generation_guid_)
               .ok());

    // Set the extension of the queue directory name
    queue_options_.set_subdirectory_extension(generation_guid_);

    // Construct the queue
    InitQueue(priority_, queue_options_);
  }

  void InitQueue(Priority priority, QueueOptions queue_options) {
    CheckOnValidSequence();
    StorageQueue::Create(
        {
            .generation_guid = generation_guid_,
            .options = queue_options,
            // Note: the callback below belongs to the Queue and does not
            // outlive Storage, so it cannot refer to `storage_` itself!
            .async_start_upload_cb = base::BindRepeating(
                &QueueUploaderInterface::AsyncProvideUploader, priority,
                storage_->health_module_, storage_->async_start_upload_cb_,
                storage_->encryption_module_),
            // `queues_container_` refers a weak pointer only, so that its
            // callback does not hold a reference to it.
            .degradation_candidates_cb = base::BindPostTask(
                storage_->sequenced_task_runner_,
                base::BindRepeating(&QueuesContainer::GetDegradationCandidates,
                                    storage_->queues_container_->GetWeakPtr(),
                                    priority)),
            .encryption_module = storage_->encryption_module_,
            .compression_module = storage_->compression_module_,
            .init_retry_cb =
                base::BindRepeating(&StorageQueue::MaybeBackoffAndReInit),
            .uma_id = Priority_Name_Substitute(priority),
        },
        base::BindPostTaskToCurrentDefault(base::BindOnce(
            &CreateQueueContext::AddQueue, base::Unretained(this),
            /*priority=*/priority)));
  }

  void AddQueue(Priority priority,
                StatusOr<scoped_refptr<StorageQueue>> storage_queue_result) {
    CheckOnValidSequence();
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
    if (!storage_queue_result.ok()) {
      LOG(ERROR) << "Could not create queue for generation_guid="
                 << generation_guid_ << " priority=" << priority
                 << ", error=" << storage_queue_result.status();
      Response(storage_queue_result.status());
      return;
    }
    // Add queue to storage
    auto added_status = storage_->queues_container_->AddQueue(
        priority, storage_queue_result.ValueOrDie());
    if (!added_status.ok()) {
      Response(added_status);
      return;
    }

    // Return the generation_guid this queue was initialized with
    Response(generation_guid_);
  }

  QueueOptions queue_options_;
  const scoped_refptr<Storage> storage_;
  const GenerationGuid generation_guid_;
  const Priority priority_;
};

void Storage::Create(
    const Storage::Settings& settings,
    base::OnceCallback<void(StatusOr<scoped_refptr<Storage>>)> completion_cb) {
  // Initializes Storage object and populates all the queues by reading the
  // storage directory and parsing queue directory names. Deletes directories
  // that do not following the queue directory name format.
  class StorageInitContext
      : public TaskRunnerContext<StatusOr<scoped_refptr<Storage>>> {
   public:
    StorageInitContext(
        scoped_refptr<Storage> storage,
        base::OnceCallback<void(StatusOr<scoped_refptr<Storage>>)> callback)
        : TaskRunnerContext<StatusOr<scoped_refptr<Storage>>>(
              std::move(callback),
              storage->sequenced_task_runner_),  // Same runner as the Storage!
          storage_(std::move(storage)) {}

   private:
    // Context can only be deleted by calling Response method.
    ~StorageInitContext() override {
      DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
      CHECK_EQ(count_, 0u);
    }

    void OnStart() override {
      CheckOnValidSequence();
      // TODO(b/300553971): delete empty queues periodically, not just during
      // storage creation.
      StorageDirectory::DeleteEmptyMultigenerationQueueDirectories(
          storage_->options_.directory());

      // Get the information we need to create queues
      queue_parameters_ = StorageDirectory::FindQueueDirectories(
          storage_->options_.directory(),
          storage_->options_.ProduceQueuesOptionsList());

      // If encryption is not enabled, proceed with the queues.
      if (!storage_->encryption_module_->is_enabled()) {
        InitAllQueues();
        return;
      }

      // Encryption is enabled. Locate the latest signed_encryption_key
      // file with matching key signature after deserialization.
      const auto download_key_result =
          storage_->key_in_storage_->DownloadKeyFile();
      if (!download_key_result.ok()) {
        // Key not found or corrupt. Proceed with encryption setup.
        // Key will be downloaded during setup.
        EncryptionSetUp(download_key_result.status());
        return;
      }

      // Key found, verified and downloaded.
      storage_->encryption_module_->UpdateAsymmetricKey(
          download_key_result.ValueOrDie().first,
          download_key_result.ValueOrDie().second,
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              &StorageInitContext::EncryptionSetUp, base::Unretained(this))));
    }

    void EncryptionSetUp(Status status) {
      CheckOnValidSequence();

      if (status.ok()) {
        // Encryption key has been found and set up. Must be available now.
        CHECK(storage_->encryption_module_->has_encryption_key());
      } else {
        LOG(WARNING)
            << "Encryption is enabled, but the key is not available yet, "
               "status="
            << status;

        // Start a task in the background which periodically requests the
        // encryption key if we need it.
        storage_->key_delivery_->StartPeriodicKeyUpdate(
            storage_->options_.key_check_period());
      }

      InitAllQueues();
    }

    void InitAllQueues() {
      CheckOnValidSequence();

      count_ = queue_parameters_.size();
      if (count_ == 0) {
        Response(std::move(storage_));
        return;
      }

      // Create queues the queue directories we found in the storage directory
      for (const auto& [priority, generation_guid] : queue_parameters_) {
        Start<CreateQueueContext>(
            // Don't transfer ownership of  `storage_` via std::move() since
            // we need to return `storage_` in the response
            priority, storage_->options_.ProduceQueueOptions(priority),
            storage_, generation_guid,
            base::BindPostTaskToCurrentDefault(
                base::BindOnce(&StorageInitContext::RespondIfAllQueuesCreated,
                               base::Unretained(this))));
      }
    }

    void RespondIfAllQueuesCreated(
        StatusOr<GenerationGuid> create_queue_result) {
      CheckOnValidSequence();
      DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
      if (!create_queue_result.status().ok()) {
        LOG(ERROR) << "Failed to create queue during Storage creation, error="
                   << create_queue_result.status();
        final_status_ = create_queue_result.status();
      }
      CHECK_GT(count_, 0u);
      if (--count_ > 0u) {
        return;
      }
      if (!final_status_.ok()) {
        Response(final_status_);
        return;
      }
      Response(std::move(storage_));
    }

    StorageOptions::QueuesOptionsList queues_options_
        GUARDED_BY_CONTEXT(storage_->sequence_checker_);
    const scoped_refptr<Storage> storage_;
    size_t count_ GUARDED_BY_CONTEXT(storage_->sequence_checker_) = 0;
    Status final_status_ GUARDED_BY_CONTEXT(storage_->sequence_checker_) =
        Status::StatusOK();
    // Stores necessary fields for creating queues. Populated by parsing queue
    // directory names.
    StorageDirectory::Set queue_parameters_
        GUARDED_BY_CONTEXT(storage_->sequence_checker_);
  };

  // Create Storage object.
  // Cannot use base::MakeRefCounted<Storage>, because constructor is
  // private.
  auto storage = base::WrapRefCounted(new Storage(settings));

  // Asynchronously run initialization.
  Start<StorageInitContext>(std::move(storage), std::move(completion_cb));
}

Storage::Storage(const Storage::Settings& settings)
    : options_(settings.options),
      sequenced_task_runner_(
          settings.queues_container->sequenced_task_runner()),
      health_module_(settings.health_module),
      encryption_module_(settings.encryption_module),
      key_delivery_(KeyDelivery::Create(settings.encryption_module,
                                        health_module_,
                                        settings.async_start_upload_cb)),
      compression_module_(settings.compression_module),
      key_in_storage_(std::make_unique<KeyInStorage>(
          settings.options.signature_verification_public_key(),
          settings.signature_verification_dev_flag,
          settings.options.directory())),
      async_start_upload_cb_(settings.async_start_upload_cb),
      queues_container_(settings.queues_container) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

Storage::~Storage() = default;

StatusOr<GenerationGuid> Storage::GetOrCreateGenerationGuid(
    const DMtoken& dm_token, Priority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  StatusOr<GenerationGuid> generation_guid_result;
  if (generation_guid_result = GetGenerationGuid(dm_token, priority);
      !generation_guid_result.ok()) {
    // Create a generation guid for this dm token and priority
    generation_guid_result = CreateGenerationGuidForDMToken(dm_token, priority);
  }
  return generation_guid_result;
}

StatusOr<GenerationGuid> Storage::GetGenerationGuid(const DMtoken& dm_token,
                                                    Priority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (dmtoken_to_generation_guid_map_.find(std::make_tuple(
          dm_token, priority)) == dmtoken_to_generation_guid_map_.end()) {
    return Status(
        error::NOT_FOUND,
        base::StrCat({"No generation guid exists for DM token: ", dm_token}));
  }
  return dmtoken_to_generation_guid_map_[std::make_tuple(dm_token, priority)];
}

StatusOr<GenerationGuid> Storage::CreateGenerationGuidForDMToken(
    const DMtoken& dm_token, Priority priority) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (auto generation_guid = GetGenerationGuid(dm_token, priority);
      generation_guid.ok()) {
    return Status(
        error::FAILED_PRECONDITION,
        base::StrCat({"Generation guid for dm_token ", dm_token,
                      " already exists! guid=", generation_guid.ValueOrDie()}));
  }

  GenerationGuid generation_guid =
      base::Uuid::GenerateRandomV4().AsLowercaseString();

  dmtoken_to_generation_guid_map_[std::make_tuple(dm_token, priority)] =
      generation_guid;
  return generation_guid;
}

void Storage::Write(Priority priority,
                    Record record,
                    base::OnceCallback<void(Status)> completion_cb) {
  // Ensure everything is executed on Storage's sequenced task runner
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<Storage> self, Priority priority, Record record,
             base::OnceCallback<void(Status)> completion_cb) {
            // Provide health module recorded, if debugging is enabled.
            if (auto recorder = self->health_module_->NewRecorder()) {
              auto* const enqueue_record =
                  recorder->mutable_enqueue_record_call();
              enqueue_record->set_priority(priority);
              enqueue_record->set_destination(record.destination());
              completion_cb = base::BindOnce(
                  [](HealthModule::Recorder recorder,
                     base::OnceCallback<void(Status)> completion_cb,
                     Status status) {
                    if (recorder) {
                      if (!status.ok()) {
                        status.SaveTo(recorder->mutable_enqueue_record_call()
                                          ->mutable_status());
                      }
                      // Move `recorder` into local variable, so that it
                      // destructs. After that it is no longer necessary anyway,
                      // but being destructed here, it will be included in
                      // health history and attached to write response request
                      // and thus immediately visible on Chrome.
                      const auto finished_recording = std::move(recorder);
                    }
                    std::move(completion_cb).Run(status);
                  },
                  std::move(recorder), std::move(completion_cb));
            }

            const DMtoken& dm_token = record.dm_token();

            // Provide health module recorded, if debugging is enabled.
            auto recorder = self->health_module_->NewRecorder();
            if (recorder) {
              auto* const queue_action_record =
                  recorder->mutable_storage_queue_action();
              queue_action_record->set_priority(priority);
              queue_action_record
                  ->mutable_storage_enqueue();  // Expected enqueue action.
            }

            // Callback that writes to the queue.
            auto queue_action =
                base::BindOnce(&Storage::WriteToQueue, self, std::move(record),
                               std::move(recorder));

            // Callback for `AsyncGetQueue` so that we can either run it here or
            // have it run after we create any necessary queues. We attach
            // `queue_action` which will execute the write when `AsyncGetQueue`
            // calls it.
            auto call_async_get_queue = base::BindOnce(
                &Storage::AsyncGetQueueAndProceed, self, priority,
                std::move(queue_action), std::move(completion_cb));

            GenerationGuid generation_guid;
            if (self->options_.is_multi_generational(priority)) {
              // Get or create the generation guid associated with the dm token
              // and priority in this record.
              StatusOr<GenerationGuid> generation_guid_result =
                  self->GetOrCreateGenerationGuid(dm_token, priority);

              if (!generation_guid_result.ok()) {
                // This should never happen. We should always be able to create
                // a generation guid if one doesn't exist.
                NOTREACHED_NORETURN();
              }
              generation_guid = generation_guid_result.ValueOrDie();
            }

            // Find the queue for this generation guid + priority and write to
            // it.
            if (!self->queues_container_->GetQueue(priority, generation_guid)
                     .ok()) {
              // We don't have a queue for this priority + generation guid, so
              // create one, and then let the context execute the write
              // via `call_async_get_queue`.
              Start<CreateQueueContext>(
                  priority, self->options_.ProduceQueueOptions(priority), self,
                  generation_guid, std::move(call_async_get_queue));
              return;
            }

            // The queue we need already exists, so we can write to it.
            std::move(call_async_get_queue).Run(generation_guid);
          },
          base::WrapRefCounted(this), priority, std::move(record),
          std::move(completion_cb)));
}

void Storage::WriteToQueue(Record record,
                           HealthModule::Recorder recorder,
                           scoped_refptr<StorageQueue> queue,
                           base::OnceCallback<void(Status)> completion_cb) {
  if (encryption_module_->is_enabled() &&
      !encryption_module_->has_encryption_key()) {
    // Key was not found at startup time. Note that if the key
    // is outdated, we still can use it, and won't load it now.
    // So this processing can only happen after Storage is
    // initialized (until the first successful delivery of a
    // key). After that we will resume the write into the queue.
    KeyDelivery::RequestCallback action = base::BindOnce(
        [](scoped_refptr<StorageQueue> queue, Record record,
           HealthModule::Recorder recorder,
           base::OnceCallback<void(Status)> completion_cb, Status status) {
          if (!status.ok()) {
            if (recorder) {
              status.SaveTo(
                  recorder->mutable_storage_queue_action()->mutable_status());
              // Move `recorder` into local variable, so that it destructs.
              // After that it is no longer necessary anyway, but being
              // destructed here, it will be included in health history and
              // attached to write response request and thus immediately visible
              // on Chrome.
              const auto finished_recording = std::move(recorder);
            }
            std::move(completion_cb).Run(status);
            return;
          }
          queue->Write(std::move(record), std::move(recorder),
                       std::move(completion_cb));
        },
        queue, std::move(record), std::move(recorder),
        std::move(completion_cb));
    key_delivery_->Request(std::move(action));
    return;
  }
  // Otherwise we can write into the queue right away.
  queue->Write(std::move(record), std::move(recorder),
               std::move(completion_cb));
}

void Storage::Confirm(SequenceInformation sequence_information,
                      bool force,
                      base::OnceCallback<void(Status)> completion_cb) {
  // Subtle bug: sequence_information is moved instead of copied, so we need
  // to extract fields from it, or else those fields  will be empty when
  // sequence_information is consumed by std::move
  const GenerationGuid generation_guid = sequence_information.generation_guid();
  const Priority priority = sequence_information.priority();

  if (auto recorder = health_module_->NewRecorder()) {
    auto* const record = recorder->mutable_confirm_record_upload_call();
    record->set_priority(sequence_information.priority());
    record->set_sequencing_id(sequence_information.sequencing_id());
    record->set_force_confirm(force);
    completion_cb = base::BindOnce(
        [](HealthModule::Recorder recorder,
           base::OnceCallback<void(Status)> completion_cb, Status status) {
          if (recorder) {
            if (!status.ok()) {
              status.SaveTo(recorder->mutable_confirm_record_upload_call()
                                ->mutable_status());
            }
            // Move `recorder` into local variable, so that it destructs.
            // After that it is no longer necessary anyway, but being
            // destructed here, it will be included in health history and
            // attached to write response request and thus immediately visible
            // on Chrome.
            const auto finished_recording = std::move(recorder);
          }
          std::move(completion_cb).Run(status);
        },
        std::move(recorder), std::move(completion_cb));
  }

  auto recorder = health_module_->NewRecorder();
  if (recorder) {
    auto* const queue_action_record = recorder->mutable_storage_queue_action();
    queue_action_record->set_priority(sequence_information.priority());
    queue_action_record->mutable_storage_dequeue();  // expected dequeue action
  }

  AsyncGetQueueAndProceed(
      priority,
      base::BindOnce(
          [](SequenceInformation sequence_information, bool force,
             HealthModule::Recorder recorder, scoped_refptr<StorageQueue> queue,
             base::OnceCallback<void(Status)> completion_cb) {
            queue->Confirm(std::move(sequence_information), force,
                           std::move(recorder), std::move(completion_cb));
          },
          std::move(sequence_information), force, std::move(recorder)),
      std::move(completion_cb), generation_guid);
}

class FlushContext : public TaskRunnerContext<Status> {
 public:
  FlushContext(scoped_refptr<Storage> storage,
               Priority priority,
               base::OnceCallback<void(Status)> callback)
      : TaskRunnerContext<Status>(
            std::move(callback),
            storage->sequenced_task_runner_),  // Same runner as the Storage!
        storage_(storage),
        priority_(priority) {}

  FlushContext(const FlushContext&) = delete;
  FlushContext& operator=(const FlushContext&) = delete;

 private:
  // Context can only be deleted by calling Response method.
  ~FlushContext() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
    CHECK_EQ(count_, 0u);
  }

  void OnStart() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);

    // Flush each queue
    count_ = storage_->queues_container_->RunActionOnAllQueues(
        priority_,
        base::BindRepeating(
            [](FlushContext* context, scoped_refptr<StorageQueue> queue) {
              queue->Flush(base::BindPostTaskToCurrentDefault(base::BindOnce(
                  &FlushContext::RespondIfAllQueuesAreFlush,
                  base::Unretained(context), queue->generation_guid())));
            },
            base::Unretained(this)));
  }

  void RespondIfAllQueuesAreFlush(GenerationGuid generation_guid,
                                  Status status) {
    CheckOnValidSequence();
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);

    if (!status.ok()) {
      if (final_status_.ok()) {
        final_status_ = status;
      }
      LOG(ERROR) << "Failed to flush queue with priority = " << priority_
                 << " generation_guid=" << generation_guid
                 << ", error=" << status.error_message();
    }
    CHECK_GT(count_, 0u);
    if (--count_ > 0u) {
      return;
    }
    Response(final_status_);
  }

  Status final_status_ GUARDED_BY_CONTEXT(storage_->sequence_checker_) =
      Status::StatusOK();
  const scoped_refptr<Storage> storage_;
  size_t count_ GUARDED_BY_CONTEXT(storage_->sequence_checker_) = 0;
  const Priority priority_;
};

void Storage::Flush(Priority priority,
                    base::OnceCallback<void(Status)> completion_cb) {
  if (auto recorder = health_module_->NewRecorder()) {
    recorder->mutable_flush_priority_call()->set_priority(priority);
    completion_cb = base::BindOnce(
        [](HealthModule::Recorder recorder,
           base::OnceCallback<void(Status)> completion_cb, Status status) {
          if (recorder) {
            if (!status.ok()) {
              status.SaveTo(
                  recorder->mutable_flush_priority_call()->mutable_status());
            }
            // Move `recorder` into local variable, so that it destructs.
            // After that it is no longer necessary anyway, but being
            // destructed here, it will be included in health history and
            // attached to write response request and thus immediately visible
            // on Chrome.
            const auto finished_recording = std::move(recorder);
          }
          std::move(completion_cb).Run(status);
        },
        std::move(recorder), std::move(completion_cb));
  }

  Start<FlushContext>(base::WrapRefCounted(this), priority,
                      std::move(completion_cb));
}

void Storage::UpdateEncryptionKey(SignedEncryptionInfo signed_encryption_key) {
  // Verify received key signature. Bail out if failed.
  const auto signature_verification_status =
      key_in_storage_->VerifySignature(signed_encryption_key);
  if (!signature_verification_status.ok()) {
    LOG(WARNING) << "Key failed verification, status="
                 << signature_verification_status;
    key_delivery_->OnCompletion(signature_verification_status);
    return;
  }

  // Assign the received key to encryption module.
  encryption_module_->UpdateAsymmetricKey(
      signed_encryption_key.public_asymmetric_key(),
      signed_encryption_key.public_key_id(),
      base::BindOnce(
          [](scoped_refptr<Storage> storage, Status status) {
            if (!status.ok()) {
              LOG(WARNING) << "Encryption key update failed, status=" << status;
              storage->key_delivery_->OnCompletion(status);
              return;
            }
            // Encryption key updated successfully.
            storage->key_delivery_->OnCompletion(Status::StatusOK());
          },
          base::WrapRefCounted(this)));

  // Serialize whole signed_encryption_key to a new file, discard the old
  // one(s). Do it on a thread which may block doing file operations.
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT, base::MayBlock()},
      base::BindOnce(
          [](SignedEncryptionInfo signed_encryption_key,
             scoped_refptr<Storage> storage) {
            const Status status =
                storage->key_in_storage_->UploadKeyFile(signed_encryption_key);
            LOG_IF(ERROR, !status.ok())
                << "Failed to upload the new encription key.";
          },
          std::move(signed_encryption_key), base::WrapRefCounted(this)));
}

void Storage::AsyncGetQueueAndProceed(
    Priority priority,
    base::OnceCallback<void(scoped_refptr<StorageQueue>,
                            base::OnceCallback<void(Status)>)> queue_action,
    base::OnceCallback<void(Status)> completion_cb,
    StatusOr<GenerationGuid> generation_guid) {
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<Storage> self,
             StatusOr<GenerationGuid> generation_guid, Priority priority,
             base::OnceCallback<void(scoped_refptr<StorageQueue>,
                                     base::OnceCallback<void(Status)>)>
                 queue_action,
             base::OnceCallback<void(Status)> completion_cb) {
            if (!generation_guid.ok()) {
              std::move(completion_cb).Run(generation_guid.status());
              return;
            }
            // Attempt to get queue by priority and generation_guid on
            // the Storage task runner.
            auto queue_result = self->queues_container_->GetQueue(
                priority, generation_guid.ValueOrDie());
            if (!queue_result.ok()) {
              // Queue not found, abort.
              std::move(completion_cb).Run(queue_result.status());
              return;
            }
            // Queue found, execute the action (it should relocate on
            // queue thread soon, to not block Storage task runner).
            std::move(queue_action)
                .Run(queue_result.ValueOrDie(), std::move(completion_cb));
          },
          base::WrapRefCounted(this), std::move(generation_guid), priority,
          std::move(queue_action), std::move(completion_cb)));
}

void Storage::RegisterCompletionCallback(base::OnceClosure callback) {
  // Although this is an asynchronous action, note that Storage cannot be
  // destructed until the callback is registered - StorageQueue is held by
  // added reference here. Thus, the callback being registered is guaranteed
  // to be called when the Storage is being destructed.
  CHECK(callback);
  sequenced_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&QueuesContainer::RegisterCompletionCallback,
                                queues_container_, std::move(callback)));
}
}  // namespace reporting
