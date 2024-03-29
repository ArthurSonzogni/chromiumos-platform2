// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/mm1_modem_location_proxy.h"

#include <memory>
#include <utility>

#include "shill/cellular/cellular_error.h"
#include "shill/logging.h"

#include <base/logging.h>

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static std::string ObjectID(const dbus::ObjectPath* p) {
  return p->value();
}
}  // namespace Logging

namespace mm1 {

ModemLocationProxy::ModemLocationProxy(const scoped_refptr<dbus::Bus>& bus,
                                       const RpcIdentifier& path,
                                       const std::string& service)
    : proxy_(new org::freedesktop::ModemManager1::Modem::LocationProxy(
          bus, service, path)) {}

ModemLocationProxy::~ModemLocationProxy() = default;

void ModemLocationProxy::Setup(uint32_t sources,
                               bool signal_location,
                               ResultCallback callback,
                               base::TimeDelta timeout) {
  SLOG(&proxy_->GetObjectPath(), 2)
      << __func__ << ": " << sources << ", " << signal_location;
  auto split_callback = base::SplitOnceCallback(std::move(callback));
  proxy_->SetupAsync(sources, signal_location,
                     base::BindOnce(&ModemLocationProxy::OnSetupSuccess,
                                    weak_factory_.GetWeakPtr(),
                                    std::move(split_callback.first)),
                     base::BindOnce(&ModemLocationProxy::OnSetupFailure,
                                    weak_factory_.GetWeakPtr(),
                                    std::move(split_callback.second)),
                     timeout.InMilliseconds());
}

void ModemLocationProxy::GetLocation(BrilloAnyCallback callback,
                                     base::TimeDelta timeout) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  auto split_callback = base::SplitOnceCallback(std::move(callback));
  proxy_->GetLocationAsync(
      base::BindOnce(&ModemLocationProxy::OnGetLocationSuccess,
                     weak_factory_.GetWeakPtr(),
                     std::move(split_callback.first)),
      base::BindOnce(&ModemLocationProxy::OnGetLocationFailure,
                     weak_factory_.GetWeakPtr(),
                     std::move(split_callback.second)),
      timeout.InMilliseconds());
}

void ModemLocationProxy::OnSetupSuccess(ResultCallback callback) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  std::move(callback).Run(Error());
}

void ModemLocationProxy::OnSetupFailure(ResultCallback callback,
                                        brillo::Error* dbus_error) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  Error error;
  CellularError::FromMM1ChromeosDBusError(dbus_error, &error);
  std::move(callback).Run(error);
}

void ModemLocationProxy::OnGetLocationSuccess(
    BrilloAnyCallback callback,
    const std::map<uint32_t, brillo::Any>& results) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  std::move(callback).Run(results, Error());
}

void ModemLocationProxy::OnGetLocationFailure(BrilloAnyCallback callback,
                                              brillo::Error* dbus_error) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  Error error;
  CellularError::FromMM1ChromeosDBusError(dbus_error, &error);
  std::move(callback).Run(std::map<uint32_t, brillo::Any>(), error);
}

}  // namespace mm1
}  // namespace shill
