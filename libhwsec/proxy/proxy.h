// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_PROXY_PROXY_H_
#define LIBHWSEC_PROXY_PROXY_H_

// Forward declarations
namespace hwsec {
namespace overalls {
class Overalls;
}  // namespace overalls
}  // namespace hwsec
namespace trunks {
class CommandTransceiver;
class TrunksFactory;
}  // namespace trunks
namespace org {
namespace chromium {
class TpmManagerProxyInterface;
class TpmNvramProxyInterface;
}  // namespace chromium
}  // namespace org

namespace hwsec {

// Proxy is a layer to abstract the communication between backend and the
// underlying services(e.g. tcsd, trunksd, tpm_managerd). And provide the
// ability to replace with mock or simulator proxy.
class Proxy {
 public:
  static inline constexpr int kDefaultDBusTimeoutMs = 300000;
  virtual ~Proxy() = default;

  // These functions shouldn't be virtual function.
  hwsec::overalls::Overalls& GetOveralls() const;
  trunks::CommandTransceiver& GetTrunksCommandTransceiver() const;
  trunks::TrunksFactory& GetTrunksFactory() const;
  org::chromium::TpmManagerProxyInterface& GetTpmManager() const;
  org::chromium::TpmNvramProxyInterface& GetTpmNvram() const;

 protected:
  Proxy() = default;

  void SetOveralls(hwsec::overalls::Overalls* overalls);
  void SetTrunksCommandTransceiver(
      trunks::CommandTransceiver* trunks_command_transceiver);
  void SetTrunksFactory(trunks::TrunksFactory* trunks_factory);
  void SetTpmManager(org::chromium::TpmManagerProxyInterface* tpm_manager);
  void SetTpmNvram(org::chromium::TpmNvramProxyInterface* tpm_nvram);

 private:
  hwsec::overalls::Overalls* overalls_ptr_;
  trunks::CommandTransceiver* trunks_command_transceiver_;
  trunks::TrunksFactory* trunks_factory_ptr_;
  org::chromium::TpmManagerProxyInterface* tpm_manager_;
  org::chromium::TpmNvramProxyInterface* tpm_nvram_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_PROXY_PROXY_H_
