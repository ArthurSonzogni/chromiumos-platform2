// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_DHCP_CONTROLLER_H_
#define SHILL_NETWORK_DHCP_CONTROLLER_H_

#include <memory>
#include <string>

#include <base/cancelable_callback.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/process_manager.h>
#include <metrics/timer.h>

#include "shill/event_dispatcher.h"
#include "shill/metrics.h"
#include "shill/mockable.h"
#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/store/key_value_store.h"
#include "shill/technology.h"
#include "shill/time.h"

namespace shill {

// This class provides a DHCP client instance for the device |device_name|.
//
// The DHCPController instance asks the DHCP client to create a lease file
// containing the name |lease_file|. If this suffix is the same as
// |device_name|, the lease is considered to be ephemeral, and the lease file is
// removed whenever this DHCPController instance is no longer needed.
// Otherwise, the lease file persists and will be re-used in future attempts.
// If |hostname| is not empty, it will be used in the DHCP request as DHCP
// option 12. This asks the DHCP server to register this hostname on our
// behalf, for purposes of administration or creating a dynamic DNS entry.
class DHCPController : public DHCPClientProxy::EventHandler {
 public:
  // Time to wait for a DHCP lease.
  static constexpr base::TimeDelta kAcquisitionTimeout = base::Seconds(30);

  using Options = DHCPClientProxy::Options;

  // Called when the IPConfig got from DHCP is updated. |network_config|
  // contains the parameters we get from DHCP and will be used for network
  // configuration. |dhcp_data| contains the other parameters that needs to be
  // exposed to user.|new_lease_acquired| indicates whether or not a DHCP lease
  // was acquired from the server.
  using UpdateCallback = base::RepeatingCallback<void(
      const net_base::NetworkConfig& network_config,
      const DHCPv4Config::Data& dhcp_data,
      bool new_lease_acquired)>;
  // Called when DHCP process ended without getting a lease. |is_voluntary|
  // indicates whether that was a voluntary stop per option 108, or because of a
  // failure.
  using DropCallback = base::RepeatingCallback<void(bool is_voluntary)>;

  enum class ReleaseReason { kDisconnect, kStaticIP };

  DHCPController(EventDispatcher* dispatcher,
                 Metrics* metrics,
                 Time* time,
                 DHCPClientProxyFactory* dhcp_client_proxy_factory,
                 std::string_view device_name,
                 Technology technology,
                 const Options& options,
                 UpdateCallback update_callback,
                 DropCallback drop_callback);

  virtual ~DHCPController();

  // Renews and releases IP configuration. Returns true on success, false
  // otherwise. ReleaseIP is advisory: if we are no longer connected, it is not
  // possible to properly vacate the lease on the remote server.  Also,
  // depending on the configuration of the specific IPConfig subclass, we may
  // end up holding on to the lease so we can resume to the network lease
  // faster.
  mockable bool RenewIP();
  mockable bool ReleaseIP(ReleaseReason reason);

  std::string device_name() const { return device_name_; }

  // Returns the time left (in seconds) till the current DHCP lease is to be
  // renewed in |time_left|. Returns nullopt if an error occurs (i.e. current
  // lease has already expired or no current DHCP lease), true otherwise.
  std::optional<base::TimeDelta> TimeToLeaseExpiry();

  // Returns the duration from Start() until the first time that this class gets
  // the DHCP lease information from the DHCP client, and then resets the value
  // (i.e., consumes the value). The next call to this function will return
  // std::nullopt, unless the DHCPController is Start()-ed again. Note that the
  // timer will only be started in Start(), which means the duration include the
  // time for starting the DHCP client, and the renewal process triggered by
  // sending a D-Bus signal to an existing DHCP client won't be counted.
  std::optional<base::TimeDelta> GetAndResetLastProvisionDuration();

  // Implement DHCPClientProxy::EventHandler.
  void OnDHCPEvent(DHCPClientProxy::EventReason reason,
                   const KeyValueStore& configuration) override;
  void OnProcessExited(int pid, int exit_status) override;

 private:
  using CreateDHCPClientProxyCB =
      base::RepeatingCallback<std::unique_ptr<DHCPClientProxy>()>;

  // Starts the DHCP client if no DHCP client is running. Returns false if
  // any error occurs and the DHCP client is not running.
  bool Start();
  // Stops the DHCP client.
  void Stop();

  void UpdateConfiguration(const KeyValueStore& configuration,
                           bool is_gateway_arp);

  // On we get a new network config via DHCP. |new_lease_acquired| indicates
  // whether this is an authoritative confirmation.
  void OnIPConfigUpdated(const net_base::NetworkConfig& network_config,
                         const DHCPv4Config::Data& dhcp_data,
                         bool new_lease_acquired);

  void NotifyDropCallback(bool is_voluntary);

  // Initialize a callback that will invoke ProcessAcquisitionTimeout if we
  // do not get a lease in a reasonable amount of time.
  void StartAcquisitionTimeout();
  // Cancel callback created by StartAcquisitionTimeout. One-liner included
  // for symmetry.
  void StopAcquisitionTimeout();
  // Called if we do not get a DHCP lease in a reasonable amount of time.
  // Informs upper layers of the failure.
  void ProcessAcquisitionTimeout();

  // Initialize a callback that will invoke ProcessExpirationTimeout if we
  // do not renew a lease in a |lease_duration|.
  void StartExpirationTimeout(base::TimeDelta lease_duration);
  // Cancel callback created by StartExpirationTimeout. One-liner included
  // for symmetry.
  void StopExpirationTimeout();
  // Called if we do not renew a DHCP lease by the time the lease expires.
  // Informs upper layers of the expiration and restarts the DHCP client.
  void ProcessExpirationTimeout(base::TimeDelta lease_duration);

  // Updates |current_lease_expiration_time_| by adding |new_lease_duration| to
  // the current time.
  void UpdateLeaseExpirationTime(uint32_t new_lease_duration);
  // Resets |current_lease_expiration_time_| to its default value.
  void ResetLeaseExpirationTime();

  // The lifetime of these variables should outlive this instance.
  EventDispatcher* dispatcher_;
  Metrics* metrics_;
  Time* time_;

  const std::string device_name_;
  const Technology technology_;
  const DHCPClientProxy::Options options_;
  const UpdateCallback update_callback_;
  const DropCallback drop_callback_;
  const bool use_arp_gateway_;
  const CreateDHCPClientProxyCB create_dhcp_client_proxy_cb_;

  std::unique_ptr<DHCPClientProxy> dhcp_client_proxy_;

  // Indicates whether a lease has been acquired from the DHCP server or gateway
  // ARP.
  bool is_lease_active_{false};

  // Indicates whether it is valid to retain the lease acquired via gateway ARP.
  bool is_gateway_arp_active_{false};

  // Called if we fail to get a DHCP lease in a timely manner.
  base::CancelableOnceClosure lease_acquisition_timeout_callback_;
  std::optional<struct timeval> current_lease_expiration_time_;

  // Called if a DHCP lease expires.
  base::CancelableOnceClosure lease_expiration_callback_;

  // The timer to measure the duration from the last Start() until we get the
  // DHCP lease information from the DHCP client for the first time.
  std::unique_ptr<chromeos_metrics::Timer> last_provision_timer_;

  base::WeakPtrFactory<DHCPController> weak_ptr_factory_{this};
};

// The factory class of DHCPController. It's used to inject mock instances at
// testing.
class DHCPControllerFactory {
 public:
  DHCPControllerFactory(EventDispatcher* dispatcher,
                        Metrics* metrics,
                        Time* time,
                        DHCPClientProxyFactory* dhcp_client_proxy_factory);
  virtual ~DHCPControllerFactory();

  virtual std::unique_ptr<DHCPController> Create(
      std::string_view device_name,
      Technology technology,
      const DHCPController::Options& options,
      DHCPController::UpdateCallback update_callback,
      DHCPController::DropCallback drop_callback);

 private:
  EventDispatcher* dispatcher_;
  Metrics* metrics_;
  Time* time_;
  DHCPClientProxyFactory* dhcp_client_proxy_factory_;
};

}  // namespace shill
#endif  // SHILL_NETWORK_DHCP_CONTROLLER_H_
