// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_METRICS_ENUMS_H_
#define SHILL_METRICS_ENUMS_H_

namespace shill {

// A static class defined only as a temporary place to host Metrics's enums
// in a header file that can be included anywhere in shill without creating
// circular dependencies. This file allows to remove all dependencies of Metrics
// onto internal shill classes and migrate enum conversion functions to allow
// using Metrics more easily across shill.
// TODO(b/268579868): Fold back these enums into metrics.h once b/268579868 is
// resolved.
class MetricsEnums {
 public:
  virtual ~MetricsEnums();

  enum WirelessSecurity {
    kWirelessSecurityUnknown = 0,
    kWirelessSecurityNone = 1,
    kWirelessSecurityWep = 2,
    kWirelessSecurityWpa = 3,
    // Value "802.11i/RSN" (4) is not used anymore.
    kWirelessSecurity8021x = 5,
    kWirelessSecurityPsk = 6,
    kWirelessSecurityWpa3 = 7,
    kWirelessSecurityWpaWpa2 = 8,
    kWirelessSecurityWpa2 = 9,
    kWirelessSecurityWpa2Wpa3 = 10,
    kWirelessSecurityWpaEnterprise = 11,
    kWirelessSecurityWpaWpa2Enterprise = 12,
    kWirelessSecurityWpa2Enterprise = 13,
    kWirelessSecurityWpa2Wpa3Enterprise = 14,
    kWirelessSecurityWpa3Enterprise = 15,
    kWirelessSecurityWpaAll = 16,
    kWirelessSecurityWpaAllEnterprise = 17,
    kWirelessSecurityWepEnterprise = 18,

    kWirelessSecurityMax
  };

  // These correspond to entries in Chrome's tools/metrics/histograms/enums.xml.
  // Please do not remove entries (append 'Deprecated' instead), and update the
  // enums.xml file when entries are added.
  enum NetworkServiceError {
    kNetworkServiceErrorNone = 0,
    kNetworkServiceErrorAAA = 1,
    kNetworkServiceErrorActivation = 2,
    kNetworkServiceErrorBadPassphrase = 3,
    kNetworkServiceErrorBadWEPKey = 4,
    kNetworkServiceErrorConnect = 5,
    kNetworkServiceErrorDHCP = 6,
    kNetworkServiceErrorDNSLookup = 7,
    kNetworkServiceErrorEAPAuthentication = 8,
    kNetworkServiceErrorEAPLocalTLS = 9,
    kNetworkServiceErrorEAPRemoteTLS = 10,
    kNetworkServiceErrorHTTPGet = 11,
    kNetworkServiceErrorIPsecCertAuth = 12,
    kNetworkServiceErrorIPsecPSKAuth = 13,
    kNetworkServiceErrorInternal = 14,
    kNetworkServiceErrorNeedEVDO = 15,
    kNetworkServiceErrorNeedHomeNetwork = 16,
    kNetworkServiceErrorOTASP = 17,
    kNetworkServiceErrorOutOfRange = 18,
    kNetworkServiceErrorPPPAuth = 19,
    kNetworkServiceErrorPinMissing = 20,
    kNetworkServiceErrorUnknown = 21,
    kNetworkServiceErrorNotAssociated = 22,
    kNetworkServiceErrorNotAuthenticated = 23,
    kNetworkServiceErrorTooManySTAs = 24,
    kNetworkServiceErrorDisconnect = 25,
    kNetworkServiceErrorSimLocked = 26,
    kNetworkServiceErrorNotRegistered = 27,
    kNetworkServiceErrorInvalidAPN = 28,
    kNetworkServiceErrorMax
  };

  // Reason when a connection initiated by Service::UserInitiatedConnect fails.
  enum UserInitiatedConnectionFailureReason {
    kUserInitiatedConnectionFailureReasonBadPassphrase = 1,
    kUserInitiatedConnectionFailureReasonBadWEPKey = 2,
    kUserInitiatedConnectionFailureReasonConnect = 3,
    kUserInitiatedConnectionFailureReasonDHCP = 4,
    kUserInitiatedConnectionFailureReasonDNSLookup = 5,
    kUserInitiatedConnectionFailureReasonEAPAuthentication = 6,
    kUserInitiatedConnectionFailureReasonEAPLocalTLS = 7,
    kUserInitiatedConnectionFailureReasonEAPRemoteTLS = 8,
    kUserInitiatedConnectionFailureReasonOutOfRange = 9,
    kUserInitiatedConnectionFailureReasonPinMissing = 10,
    kUserInitiatedConnectionFailureReasonUnknown = 11,
    kUserInitiatedConnectionFailureReasonNone = 12,
    kUserInitiatedConnectionFailureReasonNotAssociated = 13,
    kUserInitiatedConnectionFailureReasonNotAuthenticated = 14,
    kUserInitiatedConnectionFailureReasonTooManySTAs = 15,
    kUserInitiatedConnectionFailureReasonMax
  };

 protected:
  MetricsEnums();
  MetricsEnums(const MetricsEnums&) = delete;
  MetricsEnums& operator=(const MetricsEnums&) = delete;
};

}  // namespace shill

#endif  // SHILL_METRICS_ENUMS_H_
