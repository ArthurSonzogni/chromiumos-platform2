// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/check.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/common.h"

namespace secagentd {
NetworkBpfSkeleton::NetworkBpfSkeleton(
    uint32_t batch_interval_s,
    std::unique_ptr<shill::Client> shill,
    std::optional<SkeletonCallbacks<network_bpf>> cbs)
    : batch_interval_s_(batch_interval_s), weak_ptr_factory_(this) {
  CHECK(shill != nullptr);
  platform_ = GetPlatform();
  SkeletonCallbacks<network_bpf> skel_cbs;
  if (cbs) {
    skel_cbs = std::move(*cbs);
  } else {
    skel_cbs.destroy = base::BindRepeating(network_bpf__destroy);
    skel_cbs.open = base::BindRepeating(network_bpf__open);
    skel_cbs.open_opts = base::BindRepeating(network_bpf__open_opts);
  }
  shill_ = std::move(shill);
  default_bpf_skeleton_ =
      std::make_unique<BpfSkeleton<network_bpf>>("network", skel_cbs);
}

int NetworkBpfSkeleton::ConsumeEvent() {
  return default_bpf_skeleton_->ConsumeEvent();
}

std::pair<absl::Status, metrics::BpfAttachResult>
NetworkBpfSkeleton::LoadAndAttach() {
  shill_->RegisterOnAvailableCallback(base::BindOnce(
      &NetworkBpfSkeleton::OnShillAvailable, weak_ptr_factory_.GetWeakPtr()));
  auto rv = default_bpf_skeleton_->LoadAndAttach();
  if (!rv.first.ok()) {
    return rv;
  }
  scan_bpf_maps_timer_.Start(
      FROM_HERE, base::Seconds(batch_interval_s_),
      base::BindRepeating(&NetworkBpfSkeleton::ScanFlowMap,
                          weak_ptr_factory_.GetWeakPtr()));
  return rv;
}

void NetworkBpfSkeleton::OnShillProcessChanged(bool is_reset) {
  if (is_reset) {
    LOG(INFO) << "Shill was reset.";
    return;
  }
  LOG(INFO) << "Shill was shutdown.";
  shill_->RegisterOnAvailableCallback(base::BindOnce(
      &NetworkBpfSkeleton::OnShillAvailable, weak_ptr_factory_.GetWeakPtr()));
}

void NetworkBpfSkeleton::OnShillAvailable(bool success) {
  if (!success) {
    LOG(ERROR) << __func__ << "Shill not actually ready.";
    // TODO(b:277815178): Add a UMA metric to log errors related to external
    // interface fetching.
    return;
  }
  brillo::VariantDictionary properties;
  brillo::ErrorPtr error;
  LOG(INFO) << "Shill is now available.";
  shill_->RegisterProcessChangedHandler(
      base::BindRepeating(&NetworkBpfSkeleton::OnShillProcessChanged,
                          weak_ptr_factory_.GetWeakPtr()));
  shill_->RegisterDeviceAddedHandler(base::BindRepeating(
      &NetworkBpfSkeleton::OnShillDeviceAdded, weak_ptr_factory_.GetWeakPtr()));
  shill_->RegisterDeviceRemovedHandler(
      base::BindRepeating(&NetworkBpfSkeleton::OnShillDeviceRemoved,
                          weak_ptr_factory_.GetWeakPtr()));
}

void NetworkBpfSkeleton::AddExternalDevice(
    const shill::Client::Device* device) {
  auto map =
      default_bpf_skeleton_->skel_->maps.cros_network_external_interfaces;
  int64_t key = platform_->IfNameToIndex(device->ifname.c_str());
  int64_t value = key;
  int rv = platform_->BpfMapUpdateElem(map, &key, sizeof(key), &value,
                                       sizeof(value), BPF_NOEXIST);
  if (rv == EEXIST) {
    LOG(WARNING) << "Network: External device " << device->ifname
                 << " already in the BPF external device map.";
  } else if (rv < 0) {
    LOG(ERROR) << "Network: Unable to add " << device->ifname
               << " to the BPF external device map.";
    // TODO(b:277815178): Add a UMA metric to log errors related to external
    // interface fetching.
  } else {
    VLOG(1) << device->ifname << ":" << key << " added to external device map.";
  }
}

void NetworkBpfSkeleton::RemoveExternalDevice(
    const shill::Client::Device* device) {
  auto map =
      default_bpf_skeleton_->skel_->maps.cros_network_external_interfaces;
  int64_t key = platform_->IfNameToIndex(device->ifname.c_str());
  if (platform_->BpfMapDeleteElem(map, &key, sizeof(key), 0) < 0) {
    LOG(ERROR) << "Failed to remove " << device->ifname
               << " from BPF external device map.";
    // TODO(b:277815178): Add a UMA metric to log errors related to external
    // interface fetching.
  } else {
    VLOG(1) << device->ifname << ":" << key
            << " removed from external device map.";
  }
}

void NetworkBpfSkeleton::OnShillDeviceAdded(
    const shill::Client::Device* const device) {
  // Called when a new device is added (even if it's a VPN device)
  // Other virtual, non-external devices are not added.
  AddExternalDevice(device);
}

void NetworkBpfSkeleton::OnShillDeviceRemoved(
    const shill::Client::Device* const device) {
  RemoveExternalDevice(device);
}

std::unordered_set<uint64_t> NetworkBpfSkeleton::GetActiveSocketsSet() {
  uint64_t* cur_key = nullptr;
  uint64_t next_key;
  int bpf_rv;
  std::unordered_set<uint64_t> rv;
  do {
    bpf_rv = platform_->BpfMapGetNextKey(
        default_bpf_skeleton_->skel_->maps.active_socket_map, cur_key,
        &next_key, sizeof(next_key));
    cur_key = &next_key;
    if (bpf_rv == 0 || bpf_rv == -ENOENT) {  // ENOENT means last key
      rv.insert(next_key);
    }
  } while (bpf_rv == 0);
  return rv;
}

void NetworkBpfSkeleton::ScanFlowMap() {
  /* iterate through the entire map generating one synthetic event
   * per entry. This is relatively cheap as this is basically a function call.
   * no IPC message passing is actually being done.
   */
  int rv = 0;
  auto& skel_maps = default_bpf_skeleton_->skel_->maps;
  auto& skel_flow_map = skel_maps.cros_network_flow_map;
  auto& skel_process_map = skel_maps.process_map;
  std::unordered_set<uint64_t> active_sockets;

  // build a set of deceased socket identifiers.
  active_sockets = GetActiveSocketsSet();

  bpf::cros_flow_map_key* cur_key = nullptr;
  bpf::cros_flow_map_key* next_key = nullptr;
  std::vector<bpf::cros_flow_map_key> flow_map_entries_to_delete;
  bpf::cros_event cros_event;
  next_key = &cros_event.data.network_event.data.flow.flow_map_key;
  auto& network_event = cros_event.data.network_event;
  auto& event_flow = network_event.data.flow;
  auto& event_flow_map_value = event_flow.flow_map_value;
  network_event.type = bpf::kSyntheticNetworkFlow;
  cros_event.type = bpf::kNetworkEvent;
  do {
    rv = platform_->BpfMapGetNextKey(
        default_bpf_skeleton_->skel_->maps.cros_network_flow_map, cur_key,
        next_key, sizeof(*next_key));
    cur_key = next_key;
    if (rv == 0 || rv == -ENOENT) {  // ENOENT means last key.
      if (platform_->BpfMapLookupElem(skel_flow_map, cur_key, sizeof(*cur_key),
                                      &event_flow_map_value,
                                      sizeof(event_flow_map_value), 0) < 0) {
        LOG(ERROR) << "Flow metrics map retrieval failed for a given key.";
        // TODO(b:277815178): Add a UMA metric to log errors.
        continue;
      }
      if (active_sockets.find(next_key->sock) == active_sockets.end()) {
        event_flow_map_value.garbage_collect_me = true;
        // Delay the deletion of flow map events. It may not be safe to
        // delete elements while we're iterating through the map.
        flow_map_entries_to_delete.push_back(*next_key);
      }
      if (platform_->BpfMapLookupElem(
              skel_process_map, &cur_key->sock, sizeof(cur_key->sock),
              &event_flow.process_map_value,
              sizeof(event_flow.process_map_value), 0) < 0) {
        LOG(ERROR) << "Error fetching process related information for a "
                      "flow entry.";
        // TODO(b:277815178): Add a UMA metric to log errors.
        continue;
      }
      if (event_flow_map_value.garbage_collect_me) {
        platform_->BpfMapDeleteElem(skel_process_map, &(next_key->sock),
                                    sizeof(next_key->sock), 0);
      }
      default_bpf_skeleton_->callbacks_.ring_buffer_event_callback.Run(
          cros_event);
    }
  } while (rv == 0);
  // Garbage collect entries in the flow map.
  for (const auto& flow_key : flow_map_entries_to_delete) {
    platform_->BpfMapDeleteElem(skel_flow_map, &flow_key, sizeof(flow_key), 0);
  }
}

void NetworkBpfSkeleton::RegisterCallbacks(BpfCallbacks cbs) {
  default_bpf_skeleton_->RegisterCallbacks(std::move(cbs));
}

}  // namespace secagentd
