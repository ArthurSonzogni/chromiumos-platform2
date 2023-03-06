// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_MESSAGE_SENDER_H_
#define SECAGENTD_MESSAGE_SENDER_H_

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "base/files/file_path.h"
#include "base/files/file_path_watcher.h"
#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "google/protobuf/message_lite.h"
#include "missive/client/report_queue.h"
#include "missive/proto/record_constants.pb.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd {

class MessageSenderInterface
    : public base::RefCountedThreadSafe<MessageSenderInterface> {
 public:
  virtual absl::Status Initialize() = 0;
  virtual void SendMessage(
      reporting::Destination destination,
      cros_xdr::reporting::CommonEventDataFields* mutable_common,
      std::unique_ptr<google::protobuf::MessageLite> message,
      std::optional<reporting::ReportQueue::EnqueueCallback> cb) = 0;
  virtual ~MessageSenderInterface() = default;
};

namespace testing {
class MessageSenderTestFixture;
}

class MessageSender : public MessageSenderInterface {
 public:
  MessageSender();

  // Initializes:
  //   A queue for each destination and stores result into queue_map.
  //   Values for some common event proto fields.
  absl::Status Initialize() override;

  // Creates and enqueues a given proto message to the given destination.
  // Populates mutable_common with common fields if not nullptr. mutable_common
  // must be owned within message.
  // Allows for an optional callback that will be called with the message
  // status.
  void SendMessage(
      reporting::Destination destination,
      cros_xdr::reporting::CommonEventDataFields* mutable_common,
      std::unique_ptr<google::protobuf::MessageLite> message,
      std::optional<reporting::ReportQueue::EnqueueCallback> cb) override;

  // Allow calling the private test-only constructor without befriending
  // scoped_refptr.
  template <typename... Args>
  static scoped_refptr<MessageSender> CreateForTesting(Args&&... args) {
    return base::WrapRefCounted(new MessageSender(std::forward<Args>(args)...));
  }

 private:
  friend class testing::MessageSenderTestFixture;
  // Internal constructor used for testing.
  explicit MessageSender(const base::FilePath& root_path);

  void InitializeDeviceBtime();
  void InitializeAndWatchDeviceTz();
  absl::Status InitializeQueues();
  // Called by common_file_watcher_.
  void UpdateDeviceTz(const base::FilePath& timezone_symlink, bool error);

  // Map linking each destination to its corresponding Report_Queue.
  std::unordered_map<
      reporting::Destination,
      std::unique_ptr<reporting::ReportQueue, base::OnTaskRunnerDeleter>>
      queue_map_;
  // Current set of common fields. Kept up to date with a file watch.
  base::FilePathWatcher common_file_watcher_;
  base::Lock common_lock_;
  cros_xdr::reporting::CommonEventDataFields common_;
  const base::FilePath root_path_;
};

// KeyType: Return type of the "KeyDerivation" method that's used to uniquely
// identify and query queued messages. E.g the UUID of a process or the
// Community ID of a network event.
//
// XdrMessage: The larger composed or batched message type.
//
// AtomicVariantMessage: Type of the individual variant that XdrMessage is
// composed of.
template <typename KeyType, typename XdrMessage, typename AtomicVariantMessage>
class BatchSenderInterface {
 public:
  using VisitCallback = base::OnceCallback<void(AtomicVariantMessage*)>;

  virtual ~BatchSenderInterface() = default;

  // Starts internal timers.
  virtual void Start() = 0;
  // Enqueues a single atomic event. Will fill out the common fields.
  virtual void Enqueue(std::unique_ptr<AtomicVariantMessage> batched_event) = 0;
  // Applies the callback to an arbitrary message matching given variant type
  // and key. Important: The callback must not change any fields that are used
  // by KeyDerive because that isn't handled properly yet.
  virtual bool Visit(
      typename AtomicVariantMessage::VariantTypeCase variant_type,
      const KeyType& key,
      VisitCallback cb) = 0;
};

template <typename KeyType, typename XdrMessage, typename AtomicVariantMessage>
class BatchSender
    : public BatchSenderInterface<KeyType, XdrMessage, AtomicVariantMessage> {
 public:
  using KeyDerive =
      base::RepeatingCallback<KeyType(const AtomicVariantMessage&)>;
  using VisitCallback = base::OnceCallback<void(AtomicVariantMessage*)>;

  static constexpr size_t kMaxMessageSizeBytes = 8 * 1024 * 1024;

  BatchSender(KeyDerive kd,
              scoped_refptr<secagentd::MessageSenderInterface> message_sender,
              reporting::Destination destination,
              uint32_t batch_interval_s)
      : weak_ptr_factory_(this),
        kd_(std::move(kd)),
        message_sender_(message_sender),
        destination_(destination),
        batch_interval_s_(batch_interval_s) {}

  void Start() override {
    batch_timer_.Start(FROM_HERE,
                       base::Seconds(std::max(batch_interval_s_, 1u)),
                       base::BindRepeating(&BatchSender::Flush,
                                           weak_ptr_factory_.GetWeakPtr()));
  }
  bool Visit(typename AtomicVariantMessage::VariantTypeCase variant_type,
             const KeyType& key,
             VisitCallback cb) override {
    base::AutoLock lock(events_lock_);
    auto it = lookup_map_.find(std::make_pair(variant_type, key));
    if (it != lookup_map_.end()) {
      events_byte_size_ -= it->second->ByteSizeLong();
      std::move(cb).Run(it->second);
      events_byte_size_ += it->second->ByteSizeLong();
      return true;
    }
    cb.Reset();
    return false;
  }

  void Enqueue(std::unique_ptr<AtomicVariantMessage> atomic_event) override {
    atomic_event->mutable_common()->set_create_timestamp_us(
        base::Time::Now().ToJavaTime() *
        base::Time::kMicrosecondsPerMillisecond);
    base::AutoLock lock(events_lock_);
    size_t event_byte_size = atomic_event->ByteSizeLong();
    // Reserve ~10% for overhead of packing these events into the larger
    // message.
    if (events_byte_size_ + event_byte_size >= kMaxMessageSizeBytes * 0.9) {
      base::AutoUnlock unlock(events_lock_);
      Flush();
    }
    lookup_map_.insert(
        std::make_pair(std::make_pair(atomic_event->variant_type_case(),
                                      kd_.Run(*atomic_event)),
                       atomic_event.get()));
    events_byte_size_ += event_byte_size;
    events_.emplace_back(std::move(atomic_event));
  }

 protected:
  void Flush() {
    if (events_byte_size_) {
      base::AutoLock lock(events_lock_);
      VLOG(1) << "Flushing Batch for Destination " << destination_
              << ". Batch size = " << events_.size() << " (~"
              << events_byte_size_ << " bytes)";
      lookup_map_.clear();
      auto xdr_proto = std::make_unique<XdrMessage>();
      for (auto& event : events_) {
        xdr_proto->add_batched_events()->Swap(event.get());
      }
      message_sender_->SendMessage(destination_, xdr_proto->mutable_common(),
                                   std::move(xdr_proto), std::nullopt);
      events_.clear();
      events_byte_size_ = 0;
    }
    // Automatically re-fires timer after the same delay.
    batch_timer_.Reset();
  }

  base::WeakPtrFactory<BatchSender> weak_ptr_factory_;
  KeyDerive kd_;
  scoped_refptr<secagentd::MessageSenderInterface> message_sender_;
  const reporting::Destination destination_;
  uint32_t batch_interval_s_;
  base::RetainingOneShotTimer batch_timer_;
  base::Lock events_lock_;
  // Lookup Key -> &event for visitation.
  absl::flat_hash_map<
      std::pair<typename AtomicVariantMessage::VariantTypeCase, KeyType>,
      AtomicVariantMessage*>
      lookup_map_;
  // Vector of currently enqueued (atomic) events.
  std::vector<std::unique_ptr<AtomicVariantMessage>> events_;
  // Running total serialized size of currently enqueued events.
  size_t events_byte_size_ = 0;
};

}  // namespace secagentd

#endif  // SECAGENTD_MESSAGE_SENDER_H_
