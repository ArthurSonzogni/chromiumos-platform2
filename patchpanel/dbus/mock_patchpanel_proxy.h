// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DBUS_MOCK_PATCHPANEL_PROXY_H_
#define PATCHPANEL_DBUS_MOCK_PATCHPANEL_PROXY_H_

#include <gmock/gmock.h>

#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/dbus-proxies.h"

namespace patchpanel {

// This file contains the stub and mock implementation of
// PatchPanelProxyInterface. The mock version only contains the methods required
// in the unit tests.

class StubPatchPanelProxy : public org::chromium::PatchPanelProxyInterface {
 public:
  bool ArcShutdown(const patchpanel::ArcShutdownRequest& in_request,
                   patchpanel::ArcShutdownResponse* out_response,
                   brillo::ErrorPtr* error,
                   int timeout_ms) override {
    return false;
  }

  void ArcShutdownAsync(
      const patchpanel::ArcShutdownRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::ArcShutdownResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool ArcStartup(const patchpanel::ArcStartupRequest& in_request,
                  patchpanel::ArcStartupResponse* out_response,
                  brillo::ErrorPtr* error,
                  int timeout_ms) override {
    return false;
  }

  void ArcStartupAsync(
      const patchpanel::ArcStartupRequest& in_request,
      base::OnceCallback<void(
          const patchpanel::ArcStartupResponse& /*response*/)> success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool ArcVmShutdown(const patchpanel::ArcVmShutdownRequest& in_request,
                     patchpanel::ArcVmShutdownResponse* out_response,
                     brillo::ErrorPtr* error,
                     int timeout_ms) override {
    return false;
  }

  void ArcVmShutdownAsync(
      const patchpanel::ArcVmShutdownRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::ArcVmShutdownResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool ArcVmStartup(const patchpanel::ArcVmStartupRequest& in_request,
                    patchpanel::ArcVmStartupResponse* out_response,
                    brillo::ErrorPtr* error,
                    int timeout_ms) override {
    return false;
  }

  void ArcVmStartupAsync(
      const patchpanel::ArcVmStartupRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::ArcVmStartupResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool ConnectNamespace(const patchpanel::ConnectNamespaceRequest& in_request,
                        const base::ScopedFD& in_client_fd,
                        patchpanel::ConnectNamespaceResponse* out_response,
                        brillo::ErrorPtr* error,
                        int timeout_ms) override {
    return false;
  }

  void ConnectNamespaceAsync(
      const patchpanel::ConnectNamespaceRequest& in_request,
      const base::ScopedFD& in_client_fd,
      base::OnceCallback<
          void(const patchpanel::ConnectNamespaceResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool CreateLocalOnlyNetwork(
      const patchpanel::LocalOnlyNetworkRequest& in_request,
      const base::ScopedFD& in_client_fd,
      patchpanel::LocalOnlyNetworkResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void CreateLocalOnlyNetworkAsync(
      const patchpanel::LocalOnlyNetworkRequest& in_request,
      const base::ScopedFD& in_client_fd,
      base::OnceCallback<
          void(const patchpanel::LocalOnlyNetworkResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool CreateTetheredNetwork(
      const patchpanel::TetheredNetworkRequest& in_request,
      const base::ScopedFD& in_client_fd,
      patchpanel::TetheredNetworkResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void CreateTetheredNetworkAsync(
      const patchpanel::TetheredNetworkRequest& in_request,
      const base::ScopedFD& in_client_fd,
      base::OnceCallback<
          void(const patchpanel::TetheredNetworkResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool ConfigureNetwork(const patchpanel::ConfigureNetworkRequest& in_request,
                        patchpanel::ConfigureNetworkResponse* out_response,
                        brillo::ErrorPtr* error,
                        int timeout_ms) override {
    return false;
  }

  void ConfigureNetworkAsync(
      const patchpanel::ConfigureNetworkRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::ConfigureNetworkResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool GetDevices(const patchpanel::GetDevicesRequest& in_request,
                  patchpanel::GetDevicesResponse* out_response,
                  brillo::ErrorPtr* error,
                  int timeout_ms) override {
    return false;
  }

  void GetDevicesAsync(
      const patchpanel::GetDevicesRequest& in_request,
      base::OnceCallback<void(
          const patchpanel::GetDevicesResponse& /*response*/)> success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool GetDownstreamNetworkInfo(
      const patchpanel::GetDownstreamNetworkInfoRequest& in_request,
      patchpanel::GetDownstreamNetworkInfoResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void GetDownstreamNetworkInfoAsync(
      const patchpanel::GetDownstreamNetworkInfoRequest& in_request,
      base::OnceCallback<void(
          const patchpanel::GetDownstreamNetworkInfoResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool GetTrafficCounters(const patchpanel::TrafficCountersRequest& in_request,
                          patchpanel::TrafficCountersResponse* out_response,
                          brillo::ErrorPtr* error,
                          int timeout_ms) override {
    return false;
  }

  void GetTrafficCountersAsync(
      const patchpanel::TrafficCountersRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::TrafficCountersResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool ModifyPortRule(const patchpanel::ModifyPortRuleRequest& in_request,
                      patchpanel::ModifyPortRuleResponse* out_response,
                      brillo::ErrorPtr* error,
                      int timeout_ms) override {
    return false;
  }

  void ModifyPortRuleAsync(
      const patchpanel::ModifyPortRuleRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::ModifyPortRuleResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool ParallelsVmShutdown(
      const patchpanel::ParallelsVmShutdownRequest& in_request,
      patchpanel::ParallelsVmShutdownResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void ParallelsVmShutdownAsync(
      const patchpanel::ParallelsVmShutdownRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::ParallelsVmShutdownResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool ParallelsVmStartup(
      const patchpanel::ParallelsVmStartupRequest& in_request,
      patchpanel::ParallelsVmStartupResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void ParallelsVmStartupAsync(
      const patchpanel::ParallelsVmStartupRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::ParallelsVmStartupResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool BruschettaVmShutdown(
      const patchpanel::BruschettaVmShutdownRequest& in_request,
      patchpanel::BruschettaVmShutdownResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void BruschettaVmShutdownAsync(
      const patchpanel::BruschettaVmShutdownRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::BruschettaVmShutdownResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool BruschettaVmStartup(
      const patchpanel::BruschettaVmStartupRequest& in_request,
      patchpanel::BruschettaVmStartupResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void BruschettaVmStartupAsync(
      const patchpanel::BruschettaVmStartupRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::BruschettaVmStartupResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool BorealisVmShutdown(
      const patchpanel::BorealisVmShutdownRequest& in_request,
      patchpanel::BorealisVmShutdownResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void BorealisVmShutdownAsync(
      const patchpanel::BorealisVmShutdownRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::BorealisVmShutdownResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool BorealisVmStartup(const patchpanel::BorealisVmStartupRequest& in_request,
                         patchpanel::BorealisVmStartupResponse* out_response,
                         brillo::ErrorPtr* error,
                         int timeout_ms) override {
    return false;
  }

  void BorealisVmStartupAsync(
      const patchpanel::BorealisVmStartupRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::BorealisVmStartupResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool SetDnsRedirectionRule(
      const patchpanel::SetDnsRedirectionRuleRequest& in_request,
      const base::ScopedFD& in_client_fd,
      patchpanel::SetDnsRedirectionRuleResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void SetDnsRedirectionRuleAsync(
      const patchpanel::SetDnsRedirectionRuleRequest& in_request,
      const base::ScopedFD& in_client_fd,
      base::OnceCallback<
          void(const patchpanel::SetDnsRedirectionRuleResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool SetVpnLockdown(const patchpanel::SetVpnLockdownRequest& in_request,
                      patchpanel::SetVpnLockdownResponse* out_response,
                      brillo::ErrorPtr* error,
                      int timeout_ms) override {
    return false;
  }

  void SetVpnLockdownAsync(
      const patchpanel::SetVpnLockdownRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::SetVpnLockdownResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool TagSocket(const patchpanel::TagSocketRequest& in_request,
                 const base::ScopedFD& in_socket_fd,
                 patchpanel::TagSocketResponse* out_response,
                 brillo::ErrorPtr* error,
                 int timeout_ms) override {
    return false;
  }

  void TagSocketAsync(
      const patchpanel::TagSocketRequest& in_request,
      const base::ScopedFD& in_socket_fd,
      base::OnceCallback<void(
          const patchpanel::TagSocketResponse& /*response*/)> success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool TerminaVmShutdown(const patchpanel::TerminaVmShutdownRequest& in_request,
                         patchpanel::TerminaVmShutdownResponse* out_response,
                         brillo::ErrorPtr* error,
                         int timeout_ms) override {
    return false;
  }

  void TerminaVmShutdownAsync(
      const patchpanel::TerminaVmShutdownRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::TerminaVmShutdownResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool TerminaVmStartup(const patchpanel::TerminaVmStartupRequest& in_request,
                        patchpanel::TerminaVmStartupResponse* out_response,
                        brillo::ErrorPtr* error,
                        int timeout_ms) override {
    return false;
  }

  void TerminaVmStartupAsync(
      const patchpanel::TerminaVmStartupRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::TerminaVmStartupResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool NotifyAndroidWifiMulticastLockChange(
      const patchpanel::NotifyAndroidWifiMulticastLockChangeRequest& in_request,
      patchpanel::NotifyAndroidWifiMulticastLockChangeResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void NotifyAndroidWifiMulticastLockChangeAsync(
      const patchpanel::NotifyAndroidWifiMulticastLockChangeRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::
                   NotifyAndroidWifiMulticastLockChangeResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool NotifyAndroidInteractiveState(
      const patchpanel::NotifyAndroidInteractiveStateRequest& in_request,
      patchpanel::NotifyAndroidInteractiveStateResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void NotifyAndroidInteractiveStateAsync(
      const patchpanel::NotifyAndroidInteractiveStateRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::
                   NotifyAndroidInteractiveStateResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool NotifySocketConnectionEvent(
      const patchpanel::NotifySocketConnectionEventRequest& in_request,
      patchpanel::NotifySocketConnectionEventResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void NotifySocketConnectionEventAsync(
      const patchpanel::NotifySocketConnectionEventRequest& in_request,
      base::OnceCallback<void(
          const patchpanel::NotifySocketConnectionEventResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool NotifyARCVPNSocketConnectionEvent(
      const patchpanel::NotifyARCVPNSocketConnectionEventRequest& in_request,
      patchpanel::NotifyARCVPNSocketConnectionEventResponse* out_response,
      brillo::ErrorPtr* error,
      int timeout_ms) override {
    return false;
  }

  void NotifyARCVPNSocketConnectionEventAsync(
      const patchpanel::NotifyARCVPNSocketConnectionEventRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::
                   NotifyARCVPNSocketConnectionEventResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  bool SetFeatureFlag(const patchpanel::SetFeatureFlagRequest& in_request,
                      patchpanel::SetFeatureFlagResponse* out_response,
                      brillo::ErrorPtr* error,
                      int timeout_ms) override {
    return false;
  }

  void SetFeatureFlagAsync(
      const patchpanel::SetFeatureFlagRequest& in_request,
      base::OnceCallback<
          void(const patchpanel::SetFeatureFlagResponse& /*response*/)>
          success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms) override {}

  void RegisterNetworkDeviceChangedSignalHandler(
      const base::RepeatingCallback<
          void(const patchpanel::NetworkDeviceChangedSignal&)>& signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {}

  void RegisterNetworkConfigurationChangedSignalHandler(
      const base::RepeatingCallback<
          void(const patchpanel::NetworkConfigurationChangedSignal&)>&
          signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {}

  void RegisterNeighborReachabilityEventSignalHandler(
      const base::RepeatingCallback<void(
          const patchpanel::NeighborReachabilityEventSignal&)>& signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {}

  const dbus::ObjectPath& GetObjectPath() const override { return path_; }
  dbus::ObjectProxy* GetObjectProxy() const override { return nullptr; }

 private:
  dbus::ObjectPath path_;
};

class MockPatchPanelProxy : public StubPatchPanelProxy {
 public:
  MockPatchPanelProxy();
  ~MockPatchPanelProxy() override;

  MOCK_METHOD(
      bool,
      ArcShutdown,
      (const ArcShutdownRequest&, ArcShutdownResponse*, brillo::ErrorPtr*, int),
      (override));

  MOCK_METHOD(
      bool,
      ArcStartup,
      (const ArcStartupRequest&, ArcStartupResponse*, brillo::ErrorPtr*, int),
      (override));

  MOCK_METHOD(bool,
              ArcVmShutdown,
              (const ArcVmShutdownRequest&,
               ArcVmShutdownResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              ArcVmStartup,
              (const ArcVmStartupRequest&,
               ArcVmStartupResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              ConnectNamespace,
              (const ConnectNamespaceRequest&,
               const base::ScopedFD&,
               ConnectNamespaceResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              ParallelsVmShutdown,
              (const ParallelsVmShutdownRequest&,
               ParallelsVmShutdownResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              ParallelsVmStartup,
              (const ParallelsVmStartupRequest&,
               ParallelsVmStartupResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              BruschettaVmShutdown,
              (const BruschettaVmShutdownRequest&,
               BruschettaVmShutdownResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              BruschettaVmStartup,
              (const BruschettaVmStartupRequest&,
               BruschettaVmStartupResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              BorealisVmShutdown,
              (const BorealisVmShutdownRequest&,
               BorealisVmShutdownResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              BorealisVmStartup,
              (const BorealisVmStartupRequest&,
               BorealisVmStartupResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              TerminaVmShutdown,
              (const TerminaVmShutdownRequest&,
               TerminaVmShutdownResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              TerminaVmStartup,
              (const TerminaVmStartupRequest&,
               TerminaVmStartupResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(bool,
              SetFeatureFlag,
              (const patchpanel::SetFeatureFlagRequest&,
               patchpanel::SetFeatureFlagResponse*,
               brillo::ErrorPtr*,
               int),
              (override));

  MOCK_METHOD(void,
              RegisterNeighborReachabilityEventSignalHandler,
              (const base::RepeatingCallback<void(
                   const NeighborReachabilityEventSignal&)>& signal_callback,
               dbus::ObjectProxy::OnConnectedCallback on_connected_callback),
              (override));

  MOCK_METHOD(void,
              TagSocketAsync,
              (const patchpanel::TagSocketRequest&,
               const base::ScopedFD&,
               base::OnceCallback<
                   void(const patchpanel::TagSocketResponse& /*response*/)>,
               base::OnceCallback<void(brillo::Error*)>,
               int),
              (override));
};

}  // namespace patchpanel
#endif  // PATCHPANEL_DBUS_MOCK_PATCHPANEL_PROXY_H_
