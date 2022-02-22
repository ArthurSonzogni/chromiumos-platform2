// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include "base/run_loop.h"
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <dbus/message.h>
#include <chromeos/dbus/service_constants.h>

#include "missive/dbus/upload_client.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/util/disconnectable_client.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

UploadClient::UploadClient(scoped_refptr<dbus::Bus> bus,
                           dbus::ObjectProxy* chrome_proxy)
    : bus_(bus),
      chrome_proxy_(chrome_proxy),
      client_(nullptr, base::OnTaskRunnerDeleter(bus_->GetOriginTaskRunner())) {
  chrome_proxy_->SetNameOwnerChangedCallback(base::BindRepeating(
      &UploadClient::OwnerChanged, weak_ptr_factory_.GetWeakPtr()));
  chrome_proxy_->WaitForServiceToBeAvailable(base::BindOnce(
      &UploadClient::ServerAvailable, weak_ptr_factory_.GetWeakPtr()));
}
UploadClient::~UploadClient() = default;

// static
scoped_refptr<UploadClient> UploadClient::Create() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;

  // Despite being a base::RefCountedThreadSafe object, dbus::Bus doesn't follow
  // the normal pattern of creation. The constructor is public and this is the
  // standard usage.
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  CHECK(bus->Connect());
  CHECK(bus->SetUpAsyncOperations());
  dbus::ObjectProxy* chrome_proxy = bus->GetObjectProxy(
      chromeos::kChromeReportingServiceName,
      dbus::ObjectPath(chromeos::kChromeReportingServicePath));
  CHECK(chrome_proxy);

  return Create(bus, chrome_proxy);
}

// static
scoped_refptr<UploadClient> UploadClient::Create(
    scoped_refptr<dbus::Bus> bus, dbus::ObjectProxy* chrome_proxy) {
  return base::WrapRefCounted(new UploadClient(bus, chrome_proxy));
}

// Class implements DisconnectableClient::Delegate specifically for dBus
// calls. Logic that handles dBus connect/disconnect cases remains with the
// base class.
class UploadEncryptedRecordDelegate : public DisconnectableClient::Delegate {
 public:
  UploadEncryptedRecordDelegate(
      std::unique_ptr<std::vector<EncryptedRecord>> records,
      const bool need_encryption_keys,
      scoped_refptr<dbus::Bus> bus,
      dbus::ObjectProxy* chrome_proxy,
      UploadClient::HandleUploadResponseCallback response_callback)
      : bus_(bus),
        chrome_proxy_(chrome_proxy),
        response_callback_(std::move(response_callback)) {
    // Build the request.
    for (const auto& record : *records) {
      request_.add_encrypted_record()->CheckTypeAndMergeFrom(record);
    }
    request_.set_need_encryption_keys(need_encryption_keys);
  }

  // Implementation of DisconnectableClient::Delegate
  void DoCall(base::OnceClosure cb) final {
    bus_->AssertOnOriginThread();
    base::ScopedClosureRunner autorun(std::move(cb));
    dbus::MethodCall method_call(
        chromeos::kChromeReportingServiceInterface,
        chromeos::kChromeReportingServiceUploadEncryptedRecordMethod);
    dbus::MessageWriter writer(&method_call);
    if (!writer.AppendProtoAsArrayOfBytes(request_)) {
      Status status(error::UNKNOWN,
                    "MessageWriter was unable to append the request.");
      LOG(ERROR) << status;
      std::move(response_callback_).Run(status);
      return;
    }

    // Make a dBus call.
    chrome_proxy_->CallMethod(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::BindOnce(
            [](base::ScopedClosureRunner autorun,
               base::WeakPtr<UploadEncryptedRecordDelegate> self,
               dbus::Response* response) {
              if (!self) {
                return;  // Delegate already deleted.
              }
              self->bus_->AssertOnOriginThread();
              if (!response) {
                self->Respond(
                    Status(error::UNAVAILABLE, "Returned no response"));
                return;
              }
              self->response_ = response;
            },
            std::move(autorun), weak_ptr_factory_.GetWeakPtr()));
  }

  // Process dBus response, if status is OK, or error otherwise.
  void Respond(Status status) final {
    bus_->AssertOnOriginThread();
    if (!response_callback_) {
      return;
    }

    if (!status.ok()) {
      std::move(response_callback_).Run(status);
      return;
    }

    if (!response_) {
      std::move(response_callback_)
          .Run(Status(error::UNAVAILABLE,
                      "Chrome is not responding, upload skipped."));
      return;
    }

    dbus::MessageReader reader(response_);
    UploadEncryptedRecordResponse response_body;
    if (!reader.PopArrayOfBytesAsProto(&response_body)) {
      std::move(response_callback_)
          .Run(Status(error::INTERNAL, "Response was not parsable."));
      return;
    }

    std::move(response_callback_).Run(std::move(response_body));
  }

 private:
  dbus::Response* response_;
  scoped_refptr<dbus::Bus> const bus_;
  dbus::ObjectProxy* const chrome_proxy_;

  UploadEncryptedRecordRequest request_;
  UploadClient::HandleUploadResponseCallback response_callback_;

  // Weak pointer factory - must be last member of the class.
  base::WeakPtrFactory<UploadEncryptedRecordDelegate> weak_ptr_factory_{this};
};

void UploadClient::MaybeMakeCall(
    std::unique_ptr<std::vector<EncryptedRecord>> records,
    const bool need_encryption_keys,
    HandleUploadResponseCallback response_callback) {
  bus_->AssertOnOriginThread();
  auto delegate = std::make_unique<UploadEncryptedRecordDelegate>(
      std::move(records), need_encryption_keys, bus_, chrome_proxy_,
      std::move(response_callback));
  GetDisconnectableClient()->MaybeMakeCall(std::move(delegate));
}

DisconnectableClient* UploadClient::GetDisconnectableClient() {
  bus_->AssertOnOriginThread();
  if (!client_) {
    client_ = std::unique_ptr<DisconnectableClient, base::OnTaskRunnerDeleter>(
        new DisconnectableClient(bus_->GetOriginTaskRunner()),
        base::OnTaskRunnerDeleter(bus_->GetOriginTaskRunner()));
  }
  return client_.get();
}

void UploadClient::SendEncryptedRecords(
    std::unique_ptr<std::vector<EncryptedRecord>> records,
    const bool need_encryption_keys,
    HandleUploadResponseCallback response_callback) {
  bus_->GetOriginTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(&UploadClient::MaybeMakeCall,
                     weak_ptr_factory_.GetWeakPtr(), std::move(records),
                     need_encryption_keys, std::move(response_callback)));
}

void UploadClient::OwnerChanged(const std::string& old_owner,
                                const std::string& new_owner) {
  bus_->AssertOnOriginThread();
  GetDisconnectableClient()->SetAvailability(
      /*is_available=*/!new_owner.empty());
}

void UploadClient::ServerAvailable(bool service_is_available) {
  bus_->AssertOnOriginThread();
  GetDisconnectableClient()->SetAvailability(
      /*is_available=*/service_is_available);
}

void UploadClient::SetAvailabilityForTest(bool is_available) {
  base::RunLoop run_loop;
  bus_->GetOriginTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(&UploadClient::ServerAvailable,
                                base::Unretained(this), is_available));
  bus_->GetOriginTaskRunner()->PostTask(FROM_HERE, run_loop.QuitClosure());
  run_loop.Run();
}

}  // namespace reporting
