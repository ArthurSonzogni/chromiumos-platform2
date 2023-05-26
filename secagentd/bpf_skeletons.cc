// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "secagentd/bpf_skeleton_wrappers.h"

namespace secagentd {
NetworkBpfSkeleton::NetworkBpfSkeleton(uint32_t batch_interval_s)
    : batch_interval_s_(batch_interval_s), weak_ptr_factory_(this) {
  SkeletonCallbacks<network_bpf> skel_cbs;
  skel_cbs.destroy = base::BindRepeating(network_bpf__destroy);
  skel_cbs.open = base::BindRepeating(network_bpf__open);
  skel_cbs.open_opts = base::BindRepeating(network_bpf__open_opts);
  default_bpf_skeleton_ =
      std::make_unique<BpfSkeleton<network_bpf>>("network", skel_cbs);
}

int NetworkBpfSkeleton::ConsumeEvent() {
  return default_bpf_skeleton_->ConsumeEvent();
}

std::pair<absl::Status, metrics::BpfAttachResult>
NetworkBpfSkeleton::LoadAndAttach() {
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

void NetworkBpfSkeleton::ScanFlowMap() {
  /* iterate through the entire map generating one synthetic event
   * per entry. This is relatively cheap as this is basically a function call.
   * no IPC message passing is actually being done.
   */
  bpf::cros_flow_map_key* cur_key = nullptr;
  bpf::cros_flow_map_key* next_key = nullptr;
  int rv = 0;
  bpf::cros_event cros_event;
  next_key = &cros_event.data.network_event.data.flow.flow_map_key;
  auto& network_event = cros_event.data.network_event;
  auto& event_flow = network_event.data.flow;
  auto& event_flow_map_value = event_flow.flow_map_value;
  auto& skel_maps = default_bpf_skeleton_->skel_->maps;
  auto& skel_flow_map = skel_maps.cros_network_flow_map;
  auto& skel_process_map = skel_maps.process_map;

  do {
    network_event.type = bpf::kSyntheticNetworkFlow;
    cros_event.type = bpf::kNetworkEvent;
    rv = bpf_map__get_next_key(
        default_bpf_skeleton_->skel_->maps.cros_network_flow_map, cur_key,
        next_key, sizeof(*next_key));
    cur_key = next_key;
    if (rv == 0 || rv == -ENOENT) {  // ENOENT means last key.
      if (bpf_map__lookup_elem(skel_flow_map, cur_key, sizeof(*cur_key),
                               &event_flow_map_value,
                               sizeof(event_flow_map_value), 0) < 0) {
        LOG(ERROR) << "Flow metrics map retrieval failed for a given key.";
        continue;
      }
      if (bpf_map__lookup_elem(skel_process_map, &cur_key->sock,
                               sizeof(cur_key->sock),
                               &event_flow.process_map_value,
                               sizeof(event_flow.process_map_value), 0) < 0) {
        LOG(ERROR)
            << "Error fetching process related information for a flow entry.";
        continue;
      }
      default_bpf_skeleton_->callbacks_.ring_buffer_event_callback.Run(
          cros_event);
    }
  } while (rv == 0);
}

void NetworkBpfSkeleton::RegisterCallbacks(BpfCallbacks cbs) {
  default_bpf_skeleton_->RegisterCallbacks(std::move(cbs));
}

}  // namespace secagentd
