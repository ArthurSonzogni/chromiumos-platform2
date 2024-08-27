// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vhost_user_starter_client.h"

#include <string>
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

namespace internal {
std::vector<vm_tools::vhost_user_starter::IdMapItem> IdMapStringToIdMapItem(
    const std::string& id_map_string) {
  std::vector<vm_tools::vhost_user_starter::IdMapItem> id_map;

  std::vector<std::string> id_map_items = base::SplitString(
      id_map_string, ",", base::WhitespaceHandling::KEEP_WHITESPACE,
      base::SplitResult::SPLIT_WANT_NONEMPTY);

  for (std::string& id_map_item : id_map_items) {
    vm_tools::vhost_user_starter::IdMapItem item;
    std::stringstream item_ss(id_map_item);
    std::vector<std::string> prase_result = base::SplitString(
        id_map_item, " ", base::WhitespaceHandling::KEEP_WHITESPACE,
        base::SplitResult::SPLIT_WANT_NONEMPTY);

    if (prase_result.size() != 3) {
      LOG(ERROR) << "IdMapStringToIdMapItem parses wrong input: "
                 << id_map_string;
      return std::vector<vm_tools::vhost_user_starter::IdMapItem>();
    }

    item.set_in_id(std::stoi(prase_result[0]));
    item.set_out_id(std::stoi(prase_result[1]));
    item.set_range(std::stoi(prase_result[2]));

    id_map.push_back(item);
  }

  return id_map;
}
}  // namespace internal

VhostUserStarterClient::VhostUserStarterClient(scoped_refptr<dbus::Bus> bus)
    : bus_(bus),
      vhost_user_starter_proxy_(org::chromium::VhostUserStarterProxy(bus)),
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

void VhostUserStarterClient::StartVhostUserFs(
    const std::vector<base::ScopedFD>& in_socket,
    const SharedDataParam& param) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  vm_tools::vhost_user_starter::StartVhostUserFsRequest request;
  request.set_tag(param.tag);
  request.set_shared_dir(param.data_dir.value());
  for (auto item : internal::IdMapStringToIdMapItem(param.uid_map)) {
    auto uid_item = request.add_uid_map();
    uid_item->set_in_id(item.in_id());
    uid_item->set_out_id(item.out_id());
    uid_item->set_range(item.range());
  }
  for (auto item : internal::IdMapStringToIdMapItem(param.gid_map)) {
    auto gid_item = request.add_gid_map();
    gid_item->set_in_id(item.in_id());
    gid_item->set_out_id(item.out_id());
    gid_item->set_range(item.range());
  }

  auto cfg = request.mutable_cfg();
  param.set_vhost_user_virtio_fs_cfg(cfg);

  vm_tools::vhost_user_starter::StartVhostUserFsResponse response;

  CHECK(vhost_user_starter_proxy_.GetObjectProxy() != nullptr);

  vhost_user_starter_proxy_.StartVhostUserFsAsync(
      request, in_socket,
      base::BindOnce(&VhostUserStarterClient::StartVhostUserFsSuccessCallback,
                     weak_factory_.GetWeakPtr()),
      base::BindOnce(&VhostUserStarterClient::StartVhostUserFsErrorCallback,
                     weak_factory_.GetWeakPtr()));
}
}  // namespace vm_tools::concierge
