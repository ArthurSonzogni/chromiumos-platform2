// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/missive_client.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "missive/analytics/metrics.h"
#include "missive/client/missive_dbus_constants.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/disconnectable_client.h"
#include "missive/util/reporting_errors.h"
#include "missive/util/status.h"

namespace reporting {
namespace {

MissiveClient* g_instance = nullptr;

class MissiveClientImpl : public MissiveClient {
 public:
  MissiveClientImpl() : client_(origin_task_runner()) {}
  MissiveClientImpl(const MissiveClientImpl& other) = delete;
  MissiveClientImpl& operator=(const MissiveClientImpl& other) = delete;
  ~MissiveClientImpl() override = default;

  void Init(dbus::Bus* const bus) {
    CHECK(bus);
    origin_task_runner_ = bus->GetOriginTaskRunner();

    analytics::Metrics::Initialize();

    CHECK(!missive_service_proxy_);
    missive_service_proxy_ =
        bus->GetObjectProxy(missive::kMissiveServiceName,
                            dbus::ObjectPath(missive::kMissiveServicePath));
    missive_service_proxy_->SetNameOwnerChangedCallback(base::BindRepeating(
        &MissiveClientImpl::OwnerChanged, weak_ptr_factory_.GetWeakPtr()));
    missive_service_proxy_->WaitForServiceToBeAvailable(base::BindOnce(
        &MissiveClientImpl::ServerAvailable, weak_ptr_factory_.GetWeakPtr()));
  }

  void EnqueueRecord(
      const Priority priority,
      Record record,
      base::OnceCallback<void(Status)> completion_callback) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(origin_checker_);
    auto delegate = std::make_unique<EnqueueRecordDelegate>(
        priority, std::move(record), this, std::move(completion_callback));
    client_.MaybeMakeCall(std::move(delegate));
  }

  void Flush(const Priority priority,
             base::OnceCallback<void(Status)> completion_callback) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(origin_checker_);
    auto delegate = std::make_unique<FlushDelegate>(
        priority, this, std::move(completion_callback));
    client_.MaybeMakeCall(std::move(delegate));
  }

  void UpdateConfigInMissive(
      const ListOfBlockedDestinations& destinations) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(origin_checker_);
    auto delegate =
        std::make_unique<UpdateConfigInMissiveDelegate>(destinations, this);
    client_.MaybeMakeCall(std::move(delegate));
  }

  void UpdateEncryptionKey(
      const SignedEncryptionInfo& encryption_info) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(origin_checker_);
    auto delegate =
        std::make_unique<UpdateEncryptionKeyDelegate>(encryption_info, this);
    client_.MaybeMakeCall(std::move(delegate));
  }

  void ReportSuccess(const SequenceInformation& sequence_information,
                     bool force_confirm) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(origin_checker_);
    auto delegate = std::make_unique<ReportSuccessDelegate>(
        sequence_information, force_confirm, this);
    client_.MaybeMakeCall(std::move(delegate));
  }

  MissiveClient::TestInterface* GetTestInterface() override { return nullptr; }

  base::WeakPtr<MissiveClient> GetWeakPtr() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  // Class implements DisconnectableClient::Delegate specifically for dBus
  // calls. Logic that handles dBus connect/disconnect cases remains with the
  // base class.
  class DBusDelegate : public DisconnectableClient::Delegate {
   public:
    DBusDelegate(const DBusDelegate& other) = delete;
    DBusDelegate& operator=(const DBusDelegate& other) = delete;
    ~DBusDelegate() override = default;

    // Writes request into dBus message writer.
    virtual bool WriteRequest(dbus::MessageWriter* writer) = 0;

    // Parses response, retrieves status information from it and returns it.
    // Optional - returns OK if absent.
    virtual Status ParseResponse(dbus::MessageReader* reader) {
      return Status::StatusOK();
    }

   protected:
    DBusDelegate(const char* dbus_method,
                 MissiveClientImpl* owner,
                 base::OnceCallback<void(Status)> completion_callback)
        : dbus_method_(dbus_method),
          owner_(owner),
          completion_callback_(std::move(completion_callback)) {}

   private:
    // Implementation of DisconnectableClient::Delegate
    void DoCall(base::OnceClosure cb) final {
      DCHECK_CALLED_ON_VALID_SEQUENCE(owner_->origin_checker_);
      base::ScopedClosureRunner autorun(std::move(cb));
      dbus::MethodCall method_call(missive::kMissiveServiceInterface,
                                   dbus_method_);
      dbus::MessageWriter writer(&method_call);
      if (!WriteRequest(&writer)) {
        Status status(error::UNKNOWN,
                      "MessageWriter was unable to append the request.");
        LOG(ERROR) << status;
        std::move(completion_callback_).Run(status);
        return;
      }

      // Make a dBus call.
      owner_->missive_service_proxy_->CallMethodWithErrorResponse(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
          base::BindOnce(
              [](base::ScopedClosureRunner autorun,
                 base::WeakPtr<DBusDelegate> self, dbus::Response* response,
                 dbus::ErrorResponse* error_response) {
                if (!self) {
                  return;  // Delegate already deleted.
                }
                DCHECK_CALLED_ON_VALID_SEQUENCE(self->owner_->origin_checker_);
                if (!response) {
                  if (error_response) {
                    std::string error_name = error_response->GetErrorName();
                    std::string error_message;
                    dbus::MessageReader reader(error_response);
                    reader.PopString(&error_message);
                    self->Respond(Status(
                        error::UNAVAILABLE,
                        base::StrCat({"Returned error response: ", error_name,
                                      ": ", error_message})));
                  } else {
                    self->Respond(
                        Status(error::UNAVAILABLE, "Returned no response"));
                  }

                  analytics::Metrics::SendEnumToUMA(
                      kUmaUnavailableErrorReason,
                      UnavailableErrorReason::MISSIVE_CLIENT_NO_DBUS_RESPONSE,
                      UnavailableErrorReason::MAX_VALUE);
                  return;
                }
                self->response_ = response;
              },
              std::move(autorun), weak_ptr_factory_.GetWeakPtr()));
    }

    // Process dBus response, if status is OK, or error otherwise.
    void Respond(Status status) final {
      DCHECK_CALLED_ON_VALID_SEQUENCE(owner_->origin_checker_);
      if (!completion_callback_) {
        return;
      }
      if (status.ok()) {
        dbus::MessageReader reader(response_);
        status = ParseResponse(&reader);
      }
      std::move(completion_callback_).Run(status);
    }

    const char* const dbus_method_;
    dbus::Response* response_;
    MissiveClientImpl* const owner_;
    base::OnceCallback<void(Status)> completion_callback_;

    // Weak pointer factory - must be last member of the class.
    base::WeakPtrFactory<DBusDelegate> weak_ptr_factory_{this};
  };

  class EnqueueRecordDelegate : public DBusDelegate {
   public:
    EnqueueRecordDelegate(Priority priority,
                          Record record,
                          MissiveClientImpl* owner,
                          base::OnceCallback<void(Status)> completion_callback)
        : DBusDelegate(
              missive::kEnqueueRecord, owner, std::move(completion_callback)) {
      *request_.mutable_record() = std::move(record);
      request_.set_priority(priority);
    }

    bool WriteRequest(dbus::MessageWriter* writer) override {
      return writer->AppendProtoAsArrayOfBytes(request_);
    }

    Status ParseResponse(dbus::MessageReader* reader) override {
      EnqueueRecordResponse response_body;
      if (!reader->PopArrayOfBytesAsProto(&response_body)) {
        return Status(error::INTERNAL, "Response was not parsable.");
      }
      Status status;
      status.RestoreFrom(response_body.status());
      return status;
    }

   private:
    EnqueueRecordRequest request_;
  };

  class FlushDelegate : public DBusDelegate {
   public:
    FlushDelegate(Priority priority,
                  MissiveClientImpl* owner,
                  base::OnceCallback<void(Status)> completion_callback)
        : DBusDelegate(
              missive::kFlushPriority, owner, std::move(completion_callback)) {
      request_.set_priority(priority);
    }

    bool WriteRequest(dbus::MessageWriter* writer) override {
      return writer->AppendProtoAsArrayOfBytes(request_);
    }

    Status ParseResponse(dbus::MessageReader* reader) override {
      FlushPriorityResponse response_body;
      if (!reader->PopArrayOfBytesAsProto(&response_body)) {
        return Status(error::INTERNAL, "Response was not parsable.");
      }
      Status status;
      status.RestoreFrom(response_body.status());
      return status;
    }

   private:
    FlushPriorityRequest request_;
  };

  class UpdateConfigInMissiveDelegate : public DBusDelegate {
   public:
    UpdateConfigInMissiveDelegate(const ListOfBlockedDestinations& destinations,
                                  MissiveClientImpl* owner)
        : DBusDelegate(
              missive::kUpdateConfigInMissive, owner, base::DoNothing()) {
      *request_.mutable_list_of_blocked_destinations() = destinations;
    }

    bool WriteRequest(dbus::MessageWriter* writer) override {
      return writer->AppendProtoAsArrayOfBytes(request_);
    }

   private:
    UpdateConfigInMissiveRequest request_;
  };

  class UpdateEncryptionKeyDelegate : public DBusDelegate {
   public:
    UpdateEncryptionKeyDelegate(const SignedEncryptionInfo& encryption_info,
                                MissiveClientImpl* owner)
        : DBusDelegate(
              missive::kUpdateEncryptionKey, owner, base::DoNothing()) {
      *request_.mutable_signed_encryption_info() = encryption_info;
    }

    bool WriteRequest(dbus::MessageWriter* writer) override {
      return writer->AppendProtoAsArrayOfBytes(request_);
    }

   private:
    UpdateEncryptionKeyRequest request_;
  };

  class ReportSuccessDelegate : public DBusDelegate {
   public:
    ReportSuccessDelegate(const SequenceInformation& sequence_information,
                          bool force_confirm,
                          MissiveClientImpl* owner)
        : DBusDelegate(
              missive::kConfirmRecordUpload, owner, base::DoNothing()) {
      *request_.mutable_sequence_information() = sequence_information;
      request_.set_force_confirm(force_confirm);
    }

    bool WriteRequest(dbus::MessageWriter* writer) override {
      return writer->AppendProtoAsArrayOfBytes(request_);
    }

   private:
    ConfirmRecordUploadRequest request_;
  };

  void OwnerChanged(const std::string& old_owner,
                    const std::string& new_owner) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(origin_checker_);
    client_.SetAvailability(/*is_available=*/!new_owner.empty());
  }

  void ServerAvailable(bool service_is_available) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(origin_checker_);
    client_.SetAvailability(/*is_available=*/service_is_available);
  }

  scoped_refptr<dbus::ObjectProxy> missive_service_proxy_;

  DisconnectableClient client_;

  // Weak pointer factory - must be last member of the class.
  base::WeakPtrFactory<MissiveClientImpl> weak_ptr_factory_{this};
};

}  // namespace

MissiveClient::MissiveClient() {
  CHECK(!g_instance);
  g_instance = this;
}

MissiveClient::~MissiveClient() {
  CHECK_EQ(this, g_instance);
  g_instance = nullptr;
}

scoped_refptr<base::SequencedTaskRunner> MissiveClient::origin_task_runner()
    const {
  return origin_task_runner_;
}

// static
void MissiveClient::Initialize(dbus::Bus* bus) {
  CHECK(bus);
  (new MissiveClientImpl())->Init(bus);
}

// static
void MissiveClient::Shutdown() {
  CHECK(g_instance);
  delete g_instance;
}

// static
MissiveClient* MissiveClient::Get() {
  return g_instance;
}

}  // namespace reporting
