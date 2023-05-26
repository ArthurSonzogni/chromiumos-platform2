// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/subnet.h"

#include <arpa/inet.h>

#include <string>
#include <utility>

#include <base/check_op.h>
#include <base/functional/bind.h>
#include <base/logging.h>

#include "patchpanel/net_util.h"

namespace {
// Adds a positive offset given in host order to the address given in
// network byte order. Returns the address in network-byte order.
uint32_t AddOffset(uint32_t addr_no, uint32_t offset_ho) {
  return htonl(ntohl(addr_no) + offset_ho);
}
}  // namespace

namespace patchpanel {

SubnetAddress::SubnetAddress(const net_base::IPv4CIDR& cidr,
                             base::OnceClosure release_cb)
    : cidr_(cidr), release_cb_(std::move(release_cb)) {}

SubnetAddress::~SubnetAddress() = default;

Subnet::Subnet(uint32_t base_addr,
               int prefix_length,
               base::OnceClosure release_cb)
    : base_addr_(base_addr),
      prefix_length_(prefix_length),
      release_cb_(std::move(release_cb)),
      weak_factory_(this) {
  CHECK_LT(prefix_length, 32);

  addrs_.resize(1ul << (32 - prefix_length), false);

  // Mark the base address and broadcast address as allocated.
  addrs_.front() = true;
  addrs_.back() = true;
}

Subnet::~Subnet() {
  std::move(release_cb_).Run();
}

std::unique_ptr<SubnetAddress> Subnet::AllocateAtOffset(uint32_t offset) {
  if (!IsValidOffset(offset)) {
    return nullptr;
  }

  if (addrs_[offset]) {
    // Address is already allocated.
    return nullptr;
  }
  addrs_[offset] = true;

  const uint32_t addr = AddressAtOffset(offset);
  return std::make_unique<SubnetAddress>(
      *net_base::IPv4CIDR::CreateFromAddressAndPrefix(
          ConvertUint32ToIPv4Address(addr), prefix_length_),
      base::BindOnce(&Subnet::Free, weak_factory_.GetWeakPtr(), offset));
}

uint32_t Subnet::AddressAtOffset(uint32_t offset) const {
  if (!IsValidOffset(offset)) {
    return INADDR_ANY;
  }

  // The first usable IP is after the base address.
  return AddOffset(base_addr_, offset);
}

uint32_t Subnet::AvailableCount() const {
  // The available IP count is all IPs in a subnet, minus the network ID
  // and the broadcast address.
  return static_cast<uint32_t>(addrs_.size()) - 2;
}

uint32_t Subnet::BaseAddress() const {
  return base_addr_;
}

uint32_t Subnet::Netmask() const {
  return Ipv4Netmask(prefix_length_);
}

uint32_t Subnet::Prefix() const {
  return base_addr_ & Netmask();
}

int Subnet::PrefixLength() const {
  return prefix_length_;
}

std::string Subnet::ToCidrString() const {
  return IPv4AddressToCidrString(base_addr_, prefix_length_);
}

void Subnet::Free(uint32_t offset) {
  DCHECK(IsValidOffset(offset));

  addrs_[offset] = false;
}

bool Subnet::IsValidOffset(uint32_t offset) const {
  // The base address and broadcast address are considered invalid, so the range
  // of the valid offset is (0, addrs_.size() - 1).
  return 0 < offset && offset < addrs_.size() - 1;
}

}  // namespace patchpanel
