// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webservd/permission_broker_firewall.h"

#include <unistd.h>

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/macros.h>

namespace webservd {

PermissionBrokerFirewall::PermissionBrokerFirewall() {
  int fds[2];
  PCHECK(pipe(fds) == 0) << "Failed to create firewall lifeline pipe";
  lifeline_read_fd_ = fds[0];
  lifeline_write_fd_ = fds[1];
}

PermissionBrokerFirewall::~PermissionBrokerFirewall() {
  close(lifeline_read_fd_);
  close(lifeline_write_fd_);
}

void PermissionBrokerFirewall::WaitForServiceAsync(scoped_refptr<dbus::Bus> bus,
                                                   base::OnceClosure callback) {
  service_started_cb_ = std::move(callback);
  proxy_ = std::make_unique<org::chromium::PermissionBrokerProxy>(bus);
  proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::Bind(&PermissionBrokerFirewall::OnPermissionBrokerAvailable,
                 weak_ptr_factory_.GetWeakPtr()));
  proxy_->GetObjectProxy()->SetNameOwnerChangedCallback(
      base::Bind(&PermissionBrokerFirewall::OnPermissionBrokerNameOwnerChanged,
                 weak_ptr_factory_.GetWeakPtr()));
}

void PermissionBrokerFirewall::PunchTcpHoleAsync(
    uint16_t port,
    const std::string& interface_name,
    base::OnceCallback<void(bool)> success_cb,
    base::OnceCallback<void(brillo::Error*)> failure_cb) {
  proxy_->RequestTcpPortAccessAsync(port, interface_name, lifeline_read_fd_,
                                    std::move(success_cb),
                                    std::move(failure_cb));
}

void PermissionBrokerFirewall::OnPermissionBrokerAvailable(bool available) {
  if (available && !service_started_cb_.is_null())
    std::move(service_started_cb_).Run();
}

void PermissionBrokerFirewall::OnPermissionBrokerNameOwnerChanged(
    const std::string& old_owner, const std::string& new_owner) {
  if (!new_owner.empty() && !service_started_cb_.is_null())
    std::move(service_started_cb_).Run();
}

}  // namespace webservd
