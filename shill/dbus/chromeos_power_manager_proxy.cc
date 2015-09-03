// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/chromeos_power_manager_proxy.h"

#include <base/bind.h>
#include <google/protobuf/message_lite.h>

#include "power_manager/proto_bindings/suspend.pb.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"

using std::string;
using std::vector;

namespace shill {

namespace {

// Serializes |protobuf| to |out| and returns true on success.
bool SerializeProtocolBuffer(const google::protobuf::MessageLite& protobuf,
                             vector<uint8_t>* out) {
  CHECK(out);
  out->clear();
  string serialized_protobuf;
  if (!protobuf.SerializeToString(&serialized_protobuf))
    return false;
  out->assign(serialized_protobuf.begin(), serialized_protobuf.end());
  return true;
}

// Deserializes |serialized_protobuf| to |protobuf_out| and returns true on
// success.
bool DeserializeProtocolBuffer(const vector<uint8_t>& serialized_protobuf,
                               google::protobuf::MessageLite* protobuf_out) {
  CHECK(protobuf_out);
  if (serialized_protobuf.empty())
    return false;
  return protobuf_out->ParseFromArray(&serialized_protobuf.front(),
                                      serialized_protobuf.size());
}

}  // namespace

ChromeosPowerManagerProxy::ChromeosPowerManagerProxy(
      EventDispatcher* dispatcher,
      const scoped_refptr<dbus::Bus>& bus,
      PowerManagerProxyDelegate* delegate,
      const base::Closure& service_appeared_callback,
      const base::Closure& service_vanished_callback)
    : proxy_(new org::chromium::PowerManagerProxy(bus)),
      dispatcher_(dispatcher),
      delegate_(delegate),
      service_appeared_callback_(service_appeared_callback),
      service_vanished_callback_(service_vanished_callback) {
  // Register signal handlers.
  proxy_->RegisterSuspendImminentSignalHandler(
      base::Bind(&ChromeosPowerManagerProxy::SuspendImminent,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ChromeosPowerManagerProxy::OnSignalConnected,
                 weak_factory_.GetWeakPtr()));
  proxy_->RegisterSuspendDoneSignalHandler(
      base::Bind(&ChromeosPowerManagerProxy::SuspendDone,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ChromeosPowerManagerProxy::OnSignalConnected,
                 weak_factory_.GetWeakPtr()));
  proxy_->RegisterDarkSuspendImminentSignalHandler(
      base::Bind(&ChromeosPowerManagerProxy::DarkSuspendImminent,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ChromeosPowerManagerProxy::OnSignalConnected,
                 weak_factory_.GetWeakPtr()));

  // Monitor service owner changes. This callback lives for the lifetime of
  // the ObjectProxy.
  proxy_->GetObjectProxy()->SetNameOwnerChangedCallback(
      base::Bind(&ChromeosPowerManagerProxy::OnServiceOwnerChanged,
                 weak_factory_.GetWeakPtr()));

  // One time callback when service becomes available.
  proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::Bind(&ChromeosPowerManagerProxy::OnServiceAvailable,
                 weak_factory_.GetWeakPtr()));
}

ChromeosPowerManagerProxy::~ChromeosPowerManagerProxy() {}

bool ChromeosPowerManagerProxy::RegisterSuspendDelay(
    base::TimeDelta timeout,
    const string& description,
    int* delay_id_out) {
  if (!service_available_) {
    LOG(ERROR) << "PowerManager service not available";
    return false;
  }
  return RegisterSuspendDelayInternal(false,
                                      timeout,
                                      description,
                                      delay_id_out);
}

bool ChromeosPowerManagerProxy::UnregisterSuspendDelay(int delay_id) {
  if (!service_available_) {
    LOG(ERROR) << "PowerManager service not available";
    return false;
  }
  return UnregisterSuspendDelayInternal(false, delay_id);
}

bool ChromeosPowerManagerProxy::ReportSuspendReadiness(int delay_id,
                                                       int suspend_id) {
  if (!service_available_) {
    LOG(ERROR) << "PowerManager service not available";
    return false;
  }
  return ReportSuspendReadinessInternal(false, delay_id, suspend_id);
}

bool ChromeosPowerManagerProxy::RegisterDarkSuspendDelay(
    base::TimeDelta timeout,
    const string& description,
    int* delay_id_out) {
  if (!service_available_) {
    LOG(ERROR) << "PowerManager service not available";
    return false;
  }
  return RegisterSuspendDelayInternal(true,
                                      timeout,
                                      description,
                                      delay_id_out);
}

bool ChromeosPowerManagerProxy::UnregisterDarkSuspendDelay(int delay_id) {
  if (!service_available_) {
    LOG(ERROR) << "PowerManager service not available";
    return false;
  }
  return UnregisterSuspendDelayInternal(true, delay_id);
}

bool ChromeosPowerManagerProxy::ReportDarkSuspendReadiness(int delay_id,
                                                           int suspend_id ) {
  if (!service_available_) {
    LOG(ERROR) << "PowerManager service not available";
    return false;
  }
  return ReportSuspendReadinessInternal(true, delay_id, suspend_id);
}

bool ChromeosPowerManagerProxy::RecordDarkResumeWakeReason(
    const string& wake_reason) {
  LOG(INFO) << __func__;

  if (!service_available_) {
    LOG(ERROR) << "PowerManager service not available";
    return false;
  }

  power_manager::DarkResumeWakeReason proto;
  proto.set_wake_reason(wake_reason);
  vector<uint8_t> serialized_proto;
  CHECK(SerializeProtocolBuffer(proto, &serialized_proto));

  chromeos::ErrorPtr error;
  if (!proxy_->RecordDarkResumeWakeReason(serialized_proto, &error)) {
    LOG(ERROR) << "Failed tp record dark resume wake reason: "
               << error->GetCode() << " " << error->GetMessage();
    return false;
  }
  return true;
}

bool ChromeosPowerManagerProxy::RegisterSuspendDelayInternal(
    bool is_dark,
    base::TimeDelta timeout,
    const string& description,
    int* delay_id_out) {
  const string is_dark_arg = (is_dark ? "dark=true" : "dark=false");
  LOG(INFO) << __func__ << "(" << timeout.InMilliseconds()
            << ", " << is_dark_arg <<")";

  power_manager::RegisterSuspendDelayRequest request_proto;
  request_proto.set_timeout(timeout.ToInternalValue());
  request_proto.set_description(description);
  vector<uint8_t> serialized_request;
  CHECK(SerializeProtocolBuffer(request_proto, &serialized_request));

  vector<uint8_t> serialized_reply;
  chromeos::ErrorPtr error;
  if (is_dark) {
    proxy_->RegisterDarkSuspendDelay(serialized_request,
                                     &serialized_reply,
                                     &error);
  } else {
    proxy_->RegisterSuspendDelay(serialized_request, &serialized_reply, &error);
  }
  if (error) {
    LOG(ERROR) << "Failed to register suspend delay: "
               << error->GetCode() << " " << error->GetMessage();
    return false;
  }

  power_manager::RegisterSuspendDelayReply reply_proto;
  if (!DeserializeProtocolBuffer(serialized_reply, &reply_proto)) {
    LOG(ERROR) << "Failed to register "
               << (is_dark ? "dark " : "")
               << "suspend delay.  Couldn't parse response.";
    return false;
  }
  *delay_id_out = reply_proto.delay_id();
  return true;
}

bool ChromeosPowerManagerProxy::UnregisterSuspendDelayInternal(bool is_dark,
                                                               int delay_id) {
  const string is_dark_arg = (is_dark ? "dark=true" : "dark=false");
  LOG(INFO) << __func__ << "(" << delay_id << ", " << is_dark_arg << ")";

  power_manager::UnregisterSuspendDelayRequest request_proto;
  request_proto.set_delay_id(delay_id);
  vector<uint8_t> serialized_request;
  CHECK(SerializeProtocolBuffer(request_proto, &serialized_request));

  chromeos::ErrorPtr error;
  if (is_dark) {
    proxy_->UnregisterDarkSuspendDelay(serialized_request, &error);
  } else {
    proxy_->UnregisterSuspendDelay(serialized_request, &error);
  }
  if (error) {
    LOG(ERROR) << "Failed to unregister suspend delay: "
               << error->GetCode() << " " << error->GetMessage();
    return false;
  }
  return true;
}

bool ChromeosPowerManagerProxy::ReportSuspendReadinessInternal(
    bool is_dark, int delay_id, int suspend_id) {
  const string is_dark_arg = (is_dark ? "dark=true" : "dark=false");
  LOG(INFO) << __func__
            << "(" << delay_id
            << ", " << suspend_id
            << ", " << is_dark_arg << ")";

  power_manager::SuspendReadinessInfo proto;
  proto.set_delay_id(delay_id);
  proto.set_suspend_id(suspend_id);
  vector<uint8_t> serialized_proto;
  CHECK(SerializeProtocolBuffer(proto, &serialized_proto));

  chromeos::ErrorPtr error;
  if (is_dark) {
    proxy_->HandleDarkSuspendReadiness(serialized_proto, &error);
  } else {
    proxy_->HandleSuspendReadiness(serialized_proto, &error);
  }
  if (error) {
    LOG(ERROR) << "Failed to report suspend readiness: "
               << error->GetCode() << " " << error->GetMessage();
    return false;
  }
  return true;
}

void ChromeosPowerManagerProxy::SuspendImminent(
    const vector<uint8_t>& serialized_proto) {
  LOG(INFO) << __func__;
  power_manager::SuspendImminent proto;
  if (!DeserializeProtocolBuffer(serialized_proto, &proto)) {
    LOG(ERROR) << "Failed to parse SuspendImminent signal.";
    return;
  }
  delegate_->OnSuspendImminent(proto.suspend_id());
}

void ChromeosPowerManagerProxy::SuspendDone(
    const vector<uint8_t>& serialized_proto) {
  LOG(INFO) << __func__;
  power_manager::SuspendDone proto;
  if (!DeserializeProtocolBuffer(serialized_proto, &proto)) {
    LOG(ERROR) << "Failed to parse SuspendDone signal.";
    return;
  }
  delegate_->OnSuspendDone(proto.suspend_id());
}

void ChromeosPowerManagerProxy::DarkSuspendImminent(
    const vector<uint8_t>& serialized_proto) {
  LOG(INFO) << __func__;
  power_manager::SuspendImminent proto;
  if (!DeserializeProtocolBuffer(serialized_proto, &proto)) {
    LOG(ERROR) << "Failed to parse DarkSuspendImminent signal.";
    return;
  }
  delegate_->OnDarkSuspendImminent(proto.suspend_id());
}

void ChromeosPowerManagerProxy::OnServiceAvailable(bool available) {
  LOG(INFO) << __func__ << ": " << available;

  // The callback might invoke calls to the ObjectProxy, so defer the callback
  // to event loop.
  if (available && !service_appeared_callback_.is_null()) {
    dispatcher_->PostTask(service_appeared_callback_);
  } else if (!available && !service_vanished_callback_.is_null()) {
    dispatcher_->PostTask(service_vanished_callback_);
  }
  service_available_ = available;
}

void ChromeosPowerManagerProxy::OnServiceOwnerChanged(
    const string& old_owner, const string& new_owner) {
  LOG(INFO) << __func__ << "old: " << old_owner << " new: " << new_owner;
  if (new_owner.empty()) {
    OnServiceAvailable(false);
  } else {
    OnServiceAvailable(true);
  }
}

void ChromeosPowerManagerProxy::OnSignalConnected(
    const string& interface_name, const string& signal_name, bool success) {
  LOG(INFO) << __func__ << " interface: " << interface_name
            << " signal: " << signal_name << "success: " << success;
  if (!success) {
    LOG(ERROR) << "Failed to connect signal " << signal_name
        << " to interface " << interface_name;
  }
}

}  // namespace shill
