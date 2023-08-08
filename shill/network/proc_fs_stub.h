// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_PROC_FS_STUB_H_
#define SHILL_NETWORK_PROC_FS_STUB_H_

#include <string>

#include <net-base/ip_address.h>

#include "shill/mockable.h"

namespace shill {

// Helper class to handle all /proc/sys/ interactions for a certain network
// interface.
class ProcFsStub {
 public:
  static constexpr char kIPFlagAcceptDuplicateAddressDetection[] = "accept_dad";
  static constexpr char kIPFlagAcceptDuplicateAddressDetectionEnabled[] = "1";
  static constexpr char kIPFlagAcceptRouterAdvertisements[] = "accept_ra";
  static constexpr char kIPFlagAcceptRouterAdvertisementsNever[] = "0";
  static constexpr char kIPFlagAcceptRouterAdvertisementsAlways[] = "2";
  static constexpr char kIPFlagAddressGenerationMode[] = "addr_gen_mode";
  static constexpr char kIPFlagAddressGenerationModeDefault[] = "0";
  static constexpr char kIPFlagAddressGenerationModeNoLinkLocal[] = "1";
  static constexpr char kIPFlagDisableIPv6[] = "disable_ipv6";
  static constexpr char kIPFlagUseTempAddr[] = "use_tempaddr";
  static constexpr char kIPFlagUseTempAddrUsedAndDefault[] = "2";
  static constexpr char kIPFlagArpAnnounce[] = "arp_announce";
  static constexpr char kIPFlagArpAnnounceBestLocal[] = "2";
  static constexpr char kIPFlagArpIgnore[] = "arp_ignore";
  static constexpr char kIPFlagArpIgnoreLocalOnly[] = "1";

  explicit ProcFsStub(const std::string& interface_name);
  ProcFsStub(const ProcFsStub&) = delete;
  ProcFsStub& operator=(const ProcFsStub&) = delete;
  virtual ~ProcFsStub() = default;

  // Set an IP configuration flag on the device. |flag| should be the name of
  // the flag to be set and |value| is what this flag should be set to.
  // Overridden by unit tests to pretend writing to procfs.
  mockable bool SetIPFlag(net_base::IPFamily family,
                          const std::string& flag,
                          const std::string& value);

  // Flush the routing cache for all interfaces. Does not use member variables
  // but declared non-static for mocking.
  mockable bool FlushRoutingCache();

 private:
  const std::string interface_name_;
};
}  // namespace shill

#endif  // SHILL_NETWORK_PROC_FS_STUB_H_
