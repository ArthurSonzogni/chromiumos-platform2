// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/storage/storage.h"

#include <string_view>
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
#include <base/memory/scoped_refptr.h>
#include <base/notreached.h>
#include <base/sequence_checker.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "missive/analytics/metrics.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/health/health_module.h"
#include "missive/proto/health.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/storage/key_delivery.h"
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

constexpr std::string_view kUmaDeleteEmptyMultigenerationQueueDirectories =
    "Platform.Missive.DeleteEmptyMultigenerationQueueDirectories";

// Context for creating a single queue. Upon success, calls the callback with
// the GenerationGuid passed into the context, otherwise error status.
class CreateQueueContext : public TaskRunnerContext<Status> {
 public:
  CreateQueueContext(
      Priority priority,
      QueueOptions queue_options,
      scoped_refptr<Storage> storage,
      GenerationGuid generation_guid,
      base::OnceCallback<void(scoped_refptr<reporting::StorageQueue>,
                              base::OnceCallback<void(reporting::Status)>)>
          queue_created_cb,
      base::OnceCallback<void(Status)> completion_cb)
      : TaskRunnerContext<Status>(
            std::move(completion_cb),
            storage->sequenced_task_runner_),  // Same runner as the Storage!
        queue_options_(queue_options),
        storage_(storage),
        generation_guid_(generation_guid),
        priority_(priority),
        queue_created_cb_(std::move(queue_created_cb)) {}

  CreateQueueContext(const CreateQueueContext&) = delete;
  CreateQueueContext& operator=(const CreateQueueContext&) = delete;

 private:
  void OnStart() override {
    CheckOnValidSequence();
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);

    // Set the extension of the queue directory name
    queue_options_.set_subdirectory_extension(generation_guid_);

    // Construct the queue
    InitQueue(priority_, queue_options_);
  }

  void InitQueue(Priority priority, QueueOptions queue_options) {
    CheckOnValidSequence();
    // Instantiate queue.
    storage_queue_ = StorageQueue::Create({
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
        .disable_queue_cb = base::BindPostTask(
            storage_->sequenced_task_runner_,
            base::BindRepeating(&QueuesContainer::DisableQueue,
                                storage_->queues_container_->GetWeakPtr(),
                                priority)),
        .disconnect_queue_cb = base::BindPostTask(
            storage_->sequenced_task_runner_,
            base::BindRepeating(&QueuesContainer::DisconnectQueue,
                                storage_->queues_container_->GetWeakPtr(),
                                priority)),
        .encryption_module = storage_->encryption_module_,
        .compression_module = storage_->compression_module_,
        .uma_id = Priority_Name_Substitute(priority),
    });
    // Add queue to the container.
    const auto added_status =
        storage_->queues_container_->AddQueue(priority, storage_queue_);
    if (added_status.ok()) {
      // The queue has been added. Once it is initialized, we will resume at
      // `Initialized` and invoke the `queue_created_cb_` (if successful).
      // Asynchronously run initialization.
      storage_queue_->Init(
          /*init_retry_cb=*/base::BindRepeating(
              &StorageQueue::MaybeBackoffAndReInit),
          /*initialized_cb=*/base::BindPostTaskToCurrentDefault(
              base::BindOnce(&CreateQueueContext::Initialized,
                             base::Unretained(this), priority)));
      return;
    }

    // The queue failed to add. It could happen because the same priority and
    // guid were being added in parallel (could only happen when new
    // multi-generation queues are needed for `Write` operation).
    // We will check whether this is the case, and return that queue instead.
    const auto query_result =
        storage_->queues_container_->GetQueue(priority, generation_guid_);
    if (!query_result.has_value()) {
      // No pre-recorded queue either.
      Response(added_status);
      return;
    }
    // Substitute and use prior queue from now on.
    storage_queue_ = query_result.value();
    // Schedule `Initialized` to be invoked when initialization is done (or
    // immediately, if the queue is already initialized).
    storage_queue_->OnInit(base::BindPostTaskToCurrentDefault(base::BindOnce(
        &CreateQueueContext::Initialized, base::Unretained(this), priority)));
  }

  void Initialized(Priority priority, Status initialization_result) {
    CheckOnValidSequence();
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
    if (!initialization_result.ok()) {
      LOG(ERROR) << "Could not initialize queue for generation_guid="
                 << generation_guid_ << " priority=" << priority
                 << ", error=" << initialization_result;
      Response(initialization_result);
      return;
    }

    // Report success.
    std::move(queue_created_cb_)
        .Run(storage_queue_,
             base::BindPostTaskToCurrentDefault(base::BindOnce(
                 &CreateQueueContext::Response, base::Unretained(this))));
  }

  QueueOptions queue_options_;
  scoped_refptr<StorageQueue> storage_queue_;

  const scoped_refptr<Storage> storage_;
  const GenerationGuid generation_guid_;
  const Priority priority_;
  base::OnceCallback<void(scoped_refptr<reporting::StorageQueue>,
                          base::OnceCallback<void(reporting::Status)>)>
      queue_created_cb_;
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

      const bool executed_without_error =
          StorageDirectory::DeleteEmptyMultigenerationQueueDirectories(
              storage_->options_.directory());

      const auto res = analytics::Metrics::SendBoolToUMA(
          /*name=*/kUmaDeleteEmptyMultigenerationQueueDirectories.data(),
          executed_without_error);
      LOG_IF(ERROR, !res) << "SendEnumToUMA failure, "
                          << kUmaDeleteEmptyMultigenerationQueueDirectories
                          << " " << executed_without_error;

      // Get the information we need to create queues
      DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
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
      if (!download_key_result.has_value()) {
        // Key not found or corrupt. Proceed with encryption setup.
        // Key will be downloaded during setup.
        EncryptionSetUp(download_key_result.error());
        return;
      }

      // Key found, verified and downloaded.
      storage_->encryption_module_->UpdateAsymmetricKey(
          download_key_result.value().first, download_key_result.value().second,
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              &StorageInitContext::EncryptionSetUp, base::Unretained(this))));
    }

    void EncryptionSetUp(Status status) {
      CheckOnValidSequence();

      if (status.ok()) {
        // Encryption key has been found and set up. Must be available now.
        CHECK(storage_->encryption_module_->has_encryption_key());
        // Enable periodic updates of the key.
        storage_->key_delivery_->ScheduleNextKeyUpdate();
      } else {
        LOG(WARNING)
            << "Encryption is enabled, but the key is not available yet, "
               "status="
            << status;
      }

      InitAllQueues();
    }

    void InitAllQueues() {
      CheckOnValidSequence();

      DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
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
            base::BindOnce(&StorageInitContext::QueueCreated,
                           base::Unretained(this)),
            base::BindPostTaskToCurrentDefault(
                base::BindOnce(&StorageInitContext::RespondIfAllQueuesCreated,
                               base::Unretained(this))));
      }
    }

    void QueueCreated(scoped_refptr<StorageQueue> created_queue,
                      base::OnceCallback<void(Status)> completion_cb) {
      CheckOnValidSequence();
      std::move(completion_cb).Run(Status::StatusOK());
    }

    void RespondIfAllQueuesCreated(Status status) {
      CheckOnValidSequence();
      DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
      if (!status.ok()) {
        LOG(ERROR) << "Failed to create queue during Storage creation, error="
                   << status;
        final_status_ = status;
      }
      CHECK_GT(count_, 0u);
      if (--count_ > 0u) {
        return;
      }
      if (!final_status_.ok()) {
        Response(base::unexpected(final_status_));
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
      server_configuration_controller_(
          settings.server_configuration_controller),
      health_module_(settings.health_module),
      encryption_module_(settings.encryption_module),
      key_delivery_(KeyDelivery::Create(options_.key_check_period(),
                                        options_.lazy_key_check_period(),
                                        settings.encryption_module,
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

class WriteContext : public TaskRunnerContext<Status> {
 public:
  WriteContext(Priority priority,
               Record record,
               base::OnceCallback<void(Status)> write_callback,
               scoped_refptr<Storage> storage)
      : TaskRunnerContext<Status>(std::move(write_callback),
                                  storage->sequenced_task_runner_),
        storage_(storage),
        priority_(priority),
        record_(std::move(record)) {
    CHECK(storage.get());
  }

 private:
  // Context can only be deleted by calling Response method.
  ~WriteContext() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
  }

  void OnStart() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);

    // Provide health module recorded, if debugging is enabled.
    recorder_ = storage_->health_module_->NewRecorder();
    if (recorder_) {
      auto* const enqueue_record = recorder_->mutable_enqueue_record_call();
      enqueue_record->set_priority(priority_);
      enqueue_record->set_destination(record_.destination());
    }

    // Check if the destination is blocked by the current configuration
    // file provided by the server, this file has already been
    // fetched and validated on the browser code.
    if (storage_->server_configuration_controller_->IsDestinationBlocked(
            record_.destination())) {
      // If the health module is enabled then we generate a record and
      // move `recorder` into local variable, so that it gets
      // destructed.
      if (auto blocked_recorder = storage_->health_module_->NewRecorder()) {
        auto* const blocked_record =
            blocked_recorder->mutable_blocked_record_call();
        blocked_record->set_priority(priority_);
        blocked_record->set_destination(record_.destination());
        // Move `blocked_recorder` into local variable, so that it
        // destructs. After that it is no longer necessary anyway,
        // but being destructed here, it will be included in
        // health history and attached to write response request
        // and thus immediately visible on Chrome.
        const auto finished_recording = std::move(blocked_recorder);
      }
      // Since we are blocking this record we are not adding it to the
      // storage, we are just returning.
      Response(Status(error::CANCELLED, "Record blocked by destination."));
      return;
    }

    if (storage_->encryption_module_->is_enabled() &&
        !storage_->encryption_module_->has_encryption_key()) {
      // Key was not found at startup time. Note that if the key
      // is outdated, we still can use it, and won't load it now.
      // So this processing can only happen after Storage is
      // initialized (until the first successful delivery of a
      // key). After that we will resume the write into the queue.
      storage_->key_delivery_->Request(
          base::BindPostTaskToCurrentDefault(base::BindOnce(
              &WriteContext::ProceedToQueue, base::Unretained(this))));
      return;
    }

    ProceedToQueue(Status::StatusOK());
  }

  void ProceedToQueue(Status status) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);
    if (!status.ok()) {
      Response(status);
      return;
    }

    GenerationGuid generation_guid;
    if (storage_->options_.is_multi_generational(priority_)) {
      // Get or create the generation guid associated with the dm token
      // and priority in this record.
      StatusOr<GenerationGuid> generation_guid_result =
          storage_->queues_container_->GetOrCreateGenerationGuid(
              record_.dm_token(), priority_);

      // This should never happen. We should always be able to create a
      // generation guid if one doesn't exist.
      CHECK(generation_guid_result.has_value());

      generation_guid = generation_guid_result.value();
    }

    // Find the queue for this generation guid + priority and write to
    // it.
    auto queue_result = storage_->TryGetQueue(priority_, generation_guid);
    if (queue_result.has_value()) {
      // The queue we need already exists, so we can write to it.
      PerformWriteQueue(queue_result.value(),
                        base::BindPostTaskToCurrentDefault(base::BindOnce(
                            &WriteContext::Response, base::Unretained(this))));
      return;
    }

    // We don't have a queue for this priority + generation guid, so
    // create one, and then let the context execute the write
    // via `write_queue_action`. Note that we can end up in a race
    // with another `Write` of the same `priority` and
    // `generation_guid`, and in that case only one queue will survive
    // and be used.
    Start<CreateQueueContext>(
        priority_, storage_->options_.ProduceQueueOptions(priority_), storage_,
        generation_guid,
        base::BindPostTaskToCurrentDefault(base::BindOnce(
            &WriteContext::PerformWriteQueue, base::Unretained(this))),
        base::BindPostTaskToCurrentDefault(
            base::BindOnce(&WriteContext::Response, base::Unretained(this))));
  }

  void PerformWriteQueue(scoped_refptr<StorageQueue> queue,
                         base::OnceCallback<void(Status)> cb) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);

    // Provide health module recorded, if debugging is enabled.
    auto recorder = storage_->health_module_->NewRecorder();
    if (recorder) {
      auto* const queue_action_record =
          recorder->mutable_storage_queue_action();
      queue_action_record->set_priority(priority_);
      queue_action_record
          ->mutable_storage_enqueue();  // Expected enqueue action.
    }

    queue->Write(std::move(record_), std::move(recorder),
                 base::BindPostTaskToCurrentDefault(std::move(cb)));
  }

  void OnCompletion(const Status& status) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(storage_->sequence_checker_);

    // Complete health module recording, if debugging is enabled.
    if (recorder_) {
      if (!status.ok()) {
        status.SaveTo(
            recorder_->mutable_enqueue_record_call()->mutable_status());
      }
      // Move `recorder` into local variable, so that it
      // destructs. After that it is no longer necessary anyway,
      // but being destructed here, it will be included in
      // health history and attached to write response request
      // and thus immediately visible on Chrome.
      const auto finished_recording = std::move(recorder_);
    }
  }

  const scoped_refptr<Storage> storage_;

  const Priority priority_;
  Record record_ GUARDED_BY_CONTEXT(storage_->sequence_checker_);

  HealthModule::Recorder recorder_
      GUARDED_BY_CONTEXT(storage_->sequence_checker_);
};

void Storage::Write(Priority priority,
                    Record record,
                    base::OnceCallback<void(Status)> completion_cb) {
  Start<WriteContext>(priority, std::move(record), std::move(completion_cb),
                      this);
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

  // Prepare an async confirmation action to be directed to the queue.
  auto queue_confirm_action = base::BindOnce(
      [](SequenceInformation sequence_information, bool force,
         HealthModule::Recorder recorder, scoped_refptr<StorageQueue> queue,
         base::OnceCallback<void(Status)> completion_cb) {
        queue->Confirm(std::move(sequence_information), force,
                       std::move(recorder), std::move(completion_cb));
      },
      std::move(sequence_information), force, std::move(recorder));
  // Locate or create a queue, pass it to the action callback.
  sequenced_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<Storage> self, Priority priority,
             StatusOr<GenerationGuid> generation_guid,
             base::OnceCallback<void(scoped_refptr<StorageQueue>,
                                     base::OnceCallback<void(Status)>)>
                 queue_action,
             base::OnceCallback<void(Status)> completion_cb) {
            auto queue_result = self->TryGetQueue(priority, generation_guid);
            if (!queue_result.has_value()) {
              std::move(completion_cb).Run(queue_result.error());
              return;
            }
            // Queue found, execute the action (it should relocate on
            // queue thread soon, to not block Storage task runner).
            std::move(queue_action)
                .Run(queue_result.value(), std::move(completion_cb));
          },
          base::WrapRefCounted(this), priority, std::move(generation_guid),
          std::move(queue_confirm_action), std::move(completion_cb)));
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
    if (count_ == 0u) {
      // No action has been initiated, respond right away.
      Response(final_status_);
    }
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

  // If key is not available, there is nothing to flush, but we need to
  // request the key instead.
  if (encryption_module_->is_enabled() &&
      !encryption_module_->has_encryption_key()) {
    key_delivery_->Request(std::move(completion_cb));
    return;
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
    key_delivery_->OnKeyUpdateResult(signature_verification_status);
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
              storage->key_delivery_->OnKeyUpdateResult(status);
              return;
            }
            // Encryption key updated successfully.
            storage->key_delivery_->OnKeyUpdateResult(Status::StatusOK());
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

StatusOr<scoped_refptr<StorageQueue>> Storage::TryGetQueue(
    Priority priority, StatusOr<GenerationGuid> generation_guid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!generation_guid.has_value()) {
    return base::unexpected(std::move(generation_guid).error());
  }
  // Attempt to get queue by priority and generation_guid on
  // the Storage task runner.
  auto queue_result =
      queues_container_->GetQueue(priority, generation_guid.value());
  if (!queue_result.has_value()) {
    // Queue not found, abort.
    return base::unexpected(std::move(queue_result).error());
  }
  // Queue found, return it.
  return std::move(queue_result).value();
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
