// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/mm1_modem_modem3gpp_proxy.h"

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

ModemModem3gppProxy::ModemModem3gppProxy(const scoped_refptr<dbus::Bus>& bus,
                                         const RpcIdentifier& path,
                                         const std::string& service)
    : proxy_(new org::freedesktop::ModemManager1::Modem::Modem3gppProxy(
          bus, service, path)) {}

ModemModem3gppProxy::~ModemModem3gppProxy() = default;

void ModemModem3gppProxy::Register(const std::string& operator_id,
                                   Error* error,
                                   const ResultCallback& callback,
                                   int timeout) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__ << ": " << operator_id;
  proxy_->RegisterAsync(operator_id,
                        base::BindOnce(&ModemModem3gppProxy::OnRegisterSuccess,
                                       weak_factory_.GetWeakPtr(), callback),
                        base::BindOnce(&ModemModem3gppProxy::OnRegisterFailure,
                                       weak_factory_.GetWeakPtr(), callback),
                        timeout);
}

void ModemModem3gppProxy::Scan(Error* error,
                               KeyValueStoresCallback callback,
                               int timeout) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  auto split_callback = base::SplitOnceCallback(std::move(callback));
  proxy_->ScanAsync(base::BindOnce(&ModemModem3gppProxy::OnScanSuccess,
                                   weak_factory_.GetWeakPtr(),
                                   std::move(split_callback.first)),
                    base::BindOnce(&ModemModem3gppProxy::OnScanFailure,
                                   weak_factory_.GetWeakPtr(),
                                   std::move(split_callback.second)),
                    timeout);
}

void ModemModem3gppProxy::SetInitialEpsBearerSettings(
    const KeyValueStore& properties,
    Error* error,
    const ResultCallback& callback,
    int timeout) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  brillo::VariantDictionary properties_dict =
      KeyValueStore::ConvertToVariantDictionary(properties);
  proxy_->SetInitialEpsBearerSettingsAsync(
      properties_dict,
      base::BindOnce(&ModemModem3gppProxy::OnSetInitialEpsBearerSettingsSuccess,
                     weak_factory_.GetWeakPtr(), callback),
      base::BindOnce(&ModemModem3gppProxy::OnSetInitialEpsBearerSettingsFailure,
                     weak_factory_.GetWeakPtr(), callback),
      timeout);
}

void ModemModem3gppProxy::OnRegisterSuccess(const ResultCallback& callback) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  callback.Run(Error());
}

void ModemModem3gppProxy::OnRegisterFailure(const ResultCallback& callback,
                                            brillo::Error* dbus_error) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  Error error;
  CellularError::FromMM1ChromeosDBusError(dbus_error, &error);
  callback.Run(error);
}

void ModemModem3gppProxy::OnScanSuccess(
    KeyValueStoresCallback callback,
    const std::vector<brillo::VariantDictionary>& results) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  std::vector<KeyValueStore> result_stores;
  for (const auto& result : results) {
    KeyValueStore result_store =
        KeyValueStore::ConvertFromVariantDictionary(result);
    result_stores.push_back(result_store);
  }
  std::move(callback).Run(result_stores, Error());
}

void ModemModem3gppProxy::OnScanFailure(KeyValueStoresCallback callback,
                                        brillo::Error* dbus_error) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  Error error;
  CellularError::FromMM1ChromeosDBusError(dbus_error, &error);
  std::move(callback).Run(std::vector<KeyValueStore>(), error);
}

void ModemModem3gppProxy::OnSetInitialEpsBearerSettingsSuccess(
    const ResultCallback& callback) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  callback.Run(Error());
}

void ModemModem3gppProxy::OnSetInitialEpsBearerSettingsFailure(
    const ResultCallback& callback, brillo::Error* dbus_error) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  Error error;
  CellularError::FromMM1ChromeosDBusError(dbus_error, &error);
  callback.Run(error);
}

}  // namespace mm1
}  // namespace shill
