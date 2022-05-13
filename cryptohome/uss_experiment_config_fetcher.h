// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USS_EXPERIMENT_CONFIG_FETCHER_H_
#define CRYPTOHOME_USS_EXPERIMENT_CONFIG_FETCHER_H_

#include <memory>
#include <string>

#include <dbus/bus.h>
#include <shill/dbus-proxies.h>
#include <brillo/http/http_transport.h>
#include <brillo/http/http_request.h>

namespace cryptohome {

class UssExperimentConfigFetcher {
 public:
  UssExperimentConfigFetcher() = default;
  UssExperimentConfigFetcher(const UssExperimentConfigFetcher&) = delete;
  UssExperimentConfigFetcher& operator=(const UssExperimentConfigFetcher&) =
      delete;
  virtual ~UssExperimentConfigFetcher() = default;

  // Factory method.
  static std::unique_ptr<UssExperimentConfigFetcher> Create(
      const scoped_refptr<dbus::Bus>& bus);

 private:
  using FetchSuccessCallback =
      base::RepeatingCallback<void(int last_invalid, double population)>;

  void Initialize(const scoped_refptr<dbus::Bus>& bus);

  // This is called when it received the signal that we successfully registered
  // shill manager's property changes. It will check whether the connection
  // state property is already "online" after registration.
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);

  // Whenever it receives a property change signal, it checks whether it is a
  // property change of the connection state to "online". After connection state
  // is online it will fetch the config from the gstatic url.
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  // Fetch the USS experiment config from the gstatic url, and run the callback
  // with the successfully parsed fields (`last_invalid` and `population`).
  void Fetch(FetchSuccessCallback success_callback);

  // Called when the experiment config is fetched successfully. Parse the
  // fetched file and run the callback with the successfully parsed fields.
  void OnFetchSuccess(FetchSuccessCallback success_callback,
                      brillo::http::RequestID request_id,
                      std::unique_ptr<brillo::http::Response> response);

  // Called when fetching the config failed. If haven't exceed the retry count
  // limit, retry after 1 second. Otherwise report that fetching failed.
  void RetryFetch(FetchSuccessCallback success_callback);
  void RetryFetchOnGetError(FetchSuccessCallback success_callback,
                            brillo::http::RequestID request_id,
                            const brillo::Error* error);

  // Called when fetching and parsing the config succeeded. Set the USS
  // experiment flag and report metrics.
  void SetUssExperimentFlag(int last_invalid, double population);

  void SetReleaseTrackForTesting(std::string track);

  void SetTransportForTesting(
      std::shared_ptr<brillo::http::Transport> transport);

  void SetProxyForTesting(
      std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
          manager_proxy);

  // Retry count of fetching the config.
  int retries_ = 0;
  // Whether we already initiated the config fetching.
  bool fetch_initiated_ = false;
  // Used for determining the channel as different channel will have different
  // configs.
  std::string chromeos_release_track_;
  // brillo http interfaces always take Transport as a shared_ptr in its APIs.
  std::shared_ptr<brillo::http::Transport> transport_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      manager_proxy_;
  base::WeakPtrFactory<UssExperimentConfigFetcher> weak_factory_{this};

  friend class UssExperimentConfigFetcherTest;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USS_EXPERIMENT_CONFIG_FETCHER_H_
