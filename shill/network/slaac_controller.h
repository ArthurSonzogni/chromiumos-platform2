// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_SLAAC_CONTROLLER_H_
#define SHILL_NETWORK_SLAAC_CONTROLLER_H_

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/proc_fs_stub.h>
#include <chromeos/net-base/rtnl_handler.h>
#include <chromeos/net-base/rtnl_listener.h>
#include <chromeos/net-base/rtnl_message.h>
#include <metrics/timer.h>

#include "shill/event_dispatcher.h"
#include "shill/mockable.h"

namespace shill {

class SLAACController {
 public:
  // Event type for Network callback.
  enum class UpdateType {
    kAddress = 1,
    kRDNSS = 2,
    kDefaultRoute = 3,
    kDNSSL = 4,
    kCaptivePortal = 5,
    kPFlag = 6,     // Notify Network upon receiving a PIO with P-flag.
    kNoPrefix = 7,  // Notify Network upon receiving RA without PIO.
    kPref64 = 8,
  };
  using UpdateCallback = base::RepeatingCallback<void(UpdateType)>;

  SLAACController(int interface_index,
                  net_base::ProcFsStub* proc_fs,
                  net_base::RTNLHandler* rtnl_handler,
                  EventDispatcher* dispatcher,
                  std::string_view logging_tag);
  virtual ~SLAACController();

  mockable void RegisterCallback(UpdateCallback update_callback);

  // Start monitoring SLAAC RTNL from kernel. Note that we force flap
  // disable-IPv6 state on this call so that netdevice IPv6 state are refreshed.
  // If |link_local_address| is present, it is configured before SLAAC starts.
  mockable void Start(
      std::optional<net_base::IPv6Address> link_local_address = std::nullopt);
  // Stop monitoring SLAAC address on the netdevice and stop the DNS timer. The
  // SLAAC process itself in the kernel is not stopped.
  mockable void Stop();

  // Return a net_base::NetworkConfig containing all information received from
  // SLAAC. The order of its |ipv6_addresses| is guaranteed to match kernel
  // preference so that the first element is always the preferred address.
  mockable net_base::NetworkConfig GetNetworkConfig() const;

  // Returns the duration from Start() and the first time that this class gets
  // the SLAAC address information from the kernel, and then resets the value
  // (i.e., consumes the value). The next call to this function will return
  // std::nullopt, unless the SLAACController is Start()-ed again.
  std::optional<base::TimeDelta> GetAndResetLastProvisionDuration();

 private:
  // TODO(b/227563210): Refactor to remove friend declaration after moving all
  // SLAAC functionality from DeviceInfo and Network to SLAACController.
  friend class SLAACControllerTest;

  // The data struct to store IP address received from RTNL together with its
  // flags and scope information.
  struct AddressData {
    AddressData(const net_base::IPv6CIDR& cidr_in,
                unsigned char flags_in,
                unsigned char scope_in)
        : cidr(cidr_in), flags(flags_in), scope(scope_in) {}
    net_base::IPv6CIDR cidr;
    unsigned char flags;
    unsigned char scope;
  };

  void AddressMsgHandler(const net_base::RTNLMessage& msg);
  void RouteMsgHandler(const net_base::RTNLMessage& msg);
  void NDOptionMsgHandler(const net_base::RTNLMessage& msg);
  void RDNSSMsgHandler(const net_base::RTNLMessage& msg);
  void DNSSLMsgHandler(const net_base::RTNLMessage& msg);
  void CaptivePortalMsgHandler(const net_base::RTNLMessage& msg);
  void Pref64MsgHandler(const net_base::RTNLMessage& msg);

  // Timer function for monitoring RDNSS's lifetime.
  void StartRDNSSTimer(base::TimeDelta lifetime);
  void StopRDNSSTimer();
  // Called when the lifetime for RDNSS expires.
  void RDNSSExpired();

  void StartDNSSLTimer(base::TimeDelta lifetime);
  void StopDNSSLTimer();
  void DNSSLExpired();

  // A short timer to process other RTNL messages from the same RA upon
  // receiving a default route. If by end of the timer we still don't see any IP
  // address configured, notify Network so it can start DHCP-PD instead. This
  // can be deprecated if all routers are to implement P-flag properly.
  void StartNoPrefixTimer(base::TimeDelta address_timeout);
  void StopNoPrefixTimer();
  void NoPrefixTimerExpired();

  void ConfigureLinkLocalAddress();
  void SendRouterSolicitation();

  const int interface_index_;
  std::optional<net_base::IPv6Address> link_local_address_;

  // Cache of kernel SLAAC data collected through RTNL.
  std::vector<AddressData> slaac_addresses_;
  net_base::NetworkConfig network_config_;

  // Internal timer for RDNSS and DNSSL expiration.
  base::CancelableOnceClosure rdnss_expired_callback_;
  base::CancelableOnceClosure dnssl_expired_callback_;

  base::CancelableOnceClosure no_prefix_callback_;

  // Callbacks registered by RegisterCallbacks().
  UpdateCallback update_callback_;

  // The timer to measure the duration from the last Start() until we get the
  // SLAAC address from the kernel for the first time.
  std::unique_ptr<chromeos_metrics::Timer> last_provision_timer_;

  // Owned by Network
  net_base::ProcFsStub* proc_fs_;

  net_base::RTNLHandler* rtnl_handler_;
  std::unique_ptr<net_base::RTNLListener> address_listener_;
  std::unique_ptr<net_base::RTNLListener> route_listener_;
  std::unique_ptr<net_base::RTNLListener> nd_option_listener_;

  EventDispatcher* dispatcher_;

  std::string logging_tag_;

  base::WeakPtrFactory<SLAACController> weak_factory_{this};
};

BRILLO_EXPORT std::ostream& operator<<(std::ostream& stream,
                                       SLAACController::UpdateType update_type);

}  // namespace shill

#endif  // SHILL_NETWORK_SLAAC_CONTROLLER_H_
