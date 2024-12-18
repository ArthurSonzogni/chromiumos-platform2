// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vhost_user_starter_client.h"

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <vhost_user_starter/proto_bindings/vhost_user_starter.pb.h>

namespace vm_tools::concierge {

namespace {

// Creates a std::vector containing the given base::ScopedFD.
std::vector<base::ScopedFD> ScopedFDToVector(base::ScopedFD fd) {
  std::vector<base::ScopedFD> fds;
  fds.emplace_back(std::move(fd));
  return fds;
}

}  // namespace

VhostUserStarterClient::VhostUserStarterClient(scoped_refptr<dbus::Bus> bus)
    : vhost_user_starter_proxy_(org::chromium::VhostUserStarterProxy(bus)),
      weak_factory_(this) {}

void VhostUserStarterClient::StartVhostUserFsSuccessCallback(
    const vm_tools::vhost_user_starter::StartVhostUserFsResponse& response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++started_device_count;
  LOG(INFO) << "StartVhostUserFsSuccess";
}

void VhostUserStarterClient::StartVhostUserFsErrorCallback(
    brillo::Error* error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(ERROR) << "StartVhostUserFsError: " << error;
}

void VhostUserStarterClient::StartVhostUserFs(base::ScopedFD in_socket,
                                              const SharedDataParam& param,
                                              std::string_view syslog_tag) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  vm_tools::vhost_user_starter::StartVhostUserFsRequest request =
      param.get_start_vhost_user_virtio_fs_request(syslog_tag);

  vm_tools::vhost_user_starter::StartVhostUserFsResponse response;

  CHECK(vhost_user_starter_proxy_.GetObjectProxy() != nullptr);

  vhost_user_starter_proxy_.StartVhostUserFsAsync(
      request, ScopedFDToVector(std::move(in_socket)),
      base::BindOnce(&VhostUserStarterClient::StartVhostUserFsSuccessCallback,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&VhostUserStarterClient::StartVhostUserFsErrorCallback,
                     weak_factory_.GetWeakPtr()));
}
}  // namespace vm_tools::concierge
