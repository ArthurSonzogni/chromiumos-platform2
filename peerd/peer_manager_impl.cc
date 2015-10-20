// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peerd/peer_manager_impl.h"

#include <string>

#include <dbus/object_path.h>

#include "peerd/dbus_constants.h"

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::ExportedObjectManager;
using peerd::technologies::Technology;
using std::string;
using std::to_string;
using std::unique_ptr;

namespace peerd {

PeerManagerImpl::PeerManagerImpl(const scoped_refptr<dbus::Bus> bus,
                                 ExportedObjectManager* object_manager)
    : bus_(bus),
      object_manager_(object_manager) {
}

void PeerManagerImpl::OnPeerDiscovered(const string& peer_id,
                                       const base::Time& last_seen,
                                       Technology technology) {
  VLOG(1) << "Discovered peer=" << peer_id;
  auto it = peers_.find(peer_id);
  if (it != peers_.end()) {
    it->second->UpdateFromAdvertisement(last_seen, technology);
    return;
  }
  dbus::ObjectPath path{
      string(dbus_constants::kPeerPrefix) + to_string(++peers_discovered_)};
  unique_ptr<DiscoveredPeer> peer{new DiscoveredPeer{
      bus_, object_manager_, path, technology}};
  scoped_refptr<AsyncEventSequencer> sequencer(new AsyncEventSequencer());
  const bool registered = peer->RegisterAsync(
      nullptr, peer_id, last_seen,
      sequencer->GetHandler("Failed to expose DiscoveredPeer.", true));
  sequencer->OnAllTasksCompletedCall({});
  if (registered) {
    peers_[peer_id].swap(peer);
  } else {
    LOG(INFO) << "Discovered corrupted peer advertisement; discarding it.";
  }
}

void PeerManagerImpl::OnServiceDiscovered(const string& peer_id,
                                          const string& service_id,
                                          const Service::ServiceInfo& info,
                                          const Service::IpAddresses& addresses,
                                          const base::Time& last_seen,
                                          Technology technology) {
  auto it = peers_.find(peer_id);
  if (it == peers_.end()) {
    // A service was found that corresponds to no particular peer.
    // We could just silently add a peer entry here, or we can discard the
    // service.  Lets discard the service until it is known that we need to
    // support this case.
    LOG(WARNING) << "Found service=" << service_id << " but had no peer="
                 << peer_id;
    return;
  }
  VLOG(1) << "Updating service=" << service_id
          << " from technology=" << technology;
  it->second->UpdateService(service_id, addresses, info,
                            last_seen, technology);
}

void PeerManagerImpl::OnPeerRemoved(const string& peer_id,
                                    Technology technology) {
  auto it = peers_.find(peer_id);
  if (it == peers_.end()) {
    LOG(WARNING) << "Tried to remove technology=" << technology
                 << " from peer=" << peer_id
                 << " that was never discovered.";
    return;
  }
  it->second->RemoveTechnology(technology);
  if (it->second->GetTechnologyCount() == 0) {
    peers_.erase(it);
  }
}

void PeerManagerImpl::OnServiceRemoved(const string& peer_id,
                                       const string& service_id,
                                       Technology technology) {
  auto it = peers_.find(peer_id);
  if (it == peers_.end()) {
    LOG(WARNING) << "Tried to remove service from peer that was "
                 << "never discovered: " << peer_id;
    return;
  }
  it->second->RemoveTechnologyFromService(service_id, technology);
}

void PeerManagerImpl::OnTechnologyShutdown(Technology technology) {
  auto it = peers_.begin();
  while (it != peers_.end()) {
    it->second->RemoveTechnology(technology);
    if (it->second->GetTechnologyCount() == 0) {
      it = peers_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace peerd
