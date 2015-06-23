// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_DEVICE_REGISTRATION_INFO_H_
#define BUFFET_DEVICE_REGISTRATION_INFO_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <chromeos/backoff_entry.h>
#include <chromeos/data_encoding.h>
#include <chromeos/errors/error.h>
#include <chromeos/http/http_transport.h>

#include "buffet/buffet_config.h"
#include "buffet/commands/command_manager.h"
#include "buffet/notification/notification_channel.h"
#include "buffet/notification/notification_delegate.h"
#include "buffet/notification/pull_channel.h"
#include "buffet/registration_status.h"
#include "buffet/storage_interface.h"

namespace base {
class DictionaryValue;
}  // namespace base

namespace chromeos {
class KeyValueStore;
}  // namespace chromeos

namespace buffet {

class StateManager;

extern const char kErrorDomainOAuth2[];
extern const char kErrorDomainGCD[];
extern const char kErrorDomainGCDServer[];

// The DeviceRegistrationInfo class represents device registration information.
class DeviceRegistrationInfo : public NotificationDelegate {
 public:
  using OnRegistrationChangedCallback =
      base::Callback<void(RegistrationStatus)>;
  using CloudRequestCallback =
      base::Callback<void(const base::DictionaryValue&)>;
  using CloudRequestErrorCallback =
      base::Callback<void(const chromeos::Error* error)>;

  DeviceRegistrationInfo(
      const std::shared_ptr<CommandManager>& command_manager,
      const std::shared_ptr<StateManager>& state_manager,
      std::unique_ptr<BuffetConfig> config,
      const std::shared_ptr<chromeos::http::Transport>& transport,
      bool notifications_enabled);

  ~DeviceRegistrationInfo() override;

  // Add callback to listen for changes in registration status.
  void AddOnRegistrationChangedCallback(
      const OnRegistrationChangedCallback& callback);

  // Add callback to listen for changes in config.
  void AddOnConfigChangedCallback(
      const BuffetConfig::OnChangedCallback& callback);

  // Returns the authorization HTTP header that can be used to talk
  // to GCD server for authenticated device communication.
  // Make sure ValidateAndRefreshAccessToken() is called before this call.
  std::pair<std::string, std::string> GetAuthorizationHeader() const;

  // Returns the GCD service request URL. If |subpath| is specified, it is
  // appended to the base URL which is normally
  //    https://www.googleapis.com/clouddevices/v1/".
  // If |params| are specified, each key-value pair is formatted using
  // chromeos::data_encoding::WebParamsEncode() and appended to URL as a query
  // string.
  // So, calling:
  //    GetServiceURL("ticket", {{"key","apiKey"}})
  // will return something like:
  //    https://www.googleapis.com/clouddevices/v1/ticket?key=apiKey
  std::string GetServiceURL(
      const std::string& subpath = {},
      const chromeos::data_encoding::WebParamList& params = {}) const;

  // Returns a service URL to access the registered device on GCD server.
  // The base URL used to construct the full URL looks like this:
  //    https://www.googleapis.com/clouddevices/v1/devices/<device_id>/
  std::string GetDeviceURL(
    const std::string& subpath = {},
    const chromeos::data_encoding::WebParamList& params = {}) const;

  // Similar to GetServiceURL, GetOAuthURL() returns a URL of OAuth 2.0 server.
  // The base URL used is https://accounts.google.com/o/oauth2/.
  std::string GetOAuthURL(
    const std::string& subpath = {},
    const chromeos::data_encoding::WebParamList& params = {}) const;

  // Starts GCD device if credentials available.
  void Start();

  // Checks whether we have credentials generated during registration.
  bool HaveRegistrationCredentials(chromeos::ErrorPtr* error);

  // Gets the full device description JSON object asynchronously.
  // Passes the device info as the first argument to |callback|, or nullptr if
  // the device is not registered or in case of communication failure.
  void GetDeviceInfo(const CloudRequestCallback& success_callback,
                     const CloudRequestErrorCallback& error_callback);

  // Registers the device.
  // Returns a device ID on success.
  std::string RegisterDevice(const std::string& ticket_id,
                             chromeos::ErrorPtr* error);

  // Updates a command.
  void UpdateCommand(const std::string& command_id,
                     const base::DictionaryValue& command_patch,
                     const base::Closure& on_success,
                     const base::Closure& on_error);

  // Updates basic device information.
  bool UpdateDeviceInfo(const std::string& name,
                        const std::string& description,
                        const std::string& location,
                        chromeos::ErrorPtr* error);

  // Updates base device config.
  bool UpdateBaseConfig(const std::string& anonymous_access_role,
                        bool local_discovery_enabled,
                        bool local_pairing_enabled,
                        chromeos::ErrorPtr* error);

  // Updates GCD service configuration. Usually for testing.
  bool UpdateServiceConfig(const std::string& client_id,
                           const std::string& client_secret,
                           const std::string& api_key,
                           const std::string& oauth_url,
                           const std::string& service_url,
                           chromeos::ErrorPtr* error);

  // TODO(vitalybuka): remove getters and pass config to dependent code.
  const BuffetConfig& GetConfig() const { return *config_; }
  BuffetConfig* GetMutableConfig() { return config_.get(); }

  base::WeakPtr<DeviceRegistrationInfo> AsWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  friend class DeviceRegistrationInfoTest;

  // Cause DeviceRegistrationInfo to attempt to StartDevice on its own later.
  void ScheduleStartDevice(const base::TimeDelta& later);

  // Starts device execution.
  // Device will do required start up chores and then start to listen
  // to new commands.
  // TODO(antonm): Consider moving into some other class.
  void StartDevice(chromeos::ErrorPtr* error,
                   const base::TimeDelta& retry_delay);

  // Forcibly refreshes the access token.
  void RefreshAccessToken(const base::Closure& success_callback,
                          const CloudRequestErrorCallback& error_callback);

  // Success callback for RefreshAccessToken().
  void OnRefreshAccessTokenSuccess(
      const base::Closure& success_callback,
      const std::shared_ptr<CloudRequestErrorCallback>& error_callback,
      chromeos::http::RequestID id,
      std::unique_ptr<chromeos::http::Response> response);

  // Parse the OAuth response, and sets registration status to
  // kInvalidCredentials if our registration is no longer valid.
  std::unique_ptr<base::DictionaryValue> ParseOAuthResponse(
      chromeos::http::Response* response, chromeos::ErrorPtr* error);

  // This attempts to open a notification channel. The channel needs to be
  // restarted anytime the access_token is refreshed.
  void StartNotificationChannel();

  // Do a HTTPS request to cloud services.
  // Handles many cases like reauthorization, 5xx HTTP response codes
  // and device removal.  It is a recommended way to do cloud API
  // requests.
  // TODO(antonm): Consider moving into some other class.
  void DoCloudRequest(
      const std::string& method,
      const std::string& url,
      const base::DictionaryValue* body,
      const CloudRequestCallback& success_callback,
      const CloudRequestErrorCallback& error_callback);

  // Helper for DoCloudRequest().
  struct CloudRequestData {
    std::string method;
    std::string url;
    std::string body;
    CloudRequestCallback success_callback;
    CloudRequestErrorCallback error_callback;
  };
  void SendCloudRequest(const std::shared_ptr<const CloudRequestData>& data);
  void OnCloudRequestSuccess(
      const std::shared_ptr<const CloudRequestData>& data,
      chromeos::http::RequestID request_id,
      std::unique_ptr<chromeos::http::Response> response);
  void OnCloudRequestError(
      const std::shared_ptr<const CloudRequestData>& data,
      chromeos::http::RequestID request_id,
      const chromeos::Error* error);
  void RetryCloudRequest(
      const std::shared_ptr<const CloudRequestData>& data);
  void OnAccessTokenRefreshed(
      const std::shared_ptr<const CloudRequestData>& data);
  void OnAccessTokenError(
      const std::shared_ptr<const CloudRequestData>& data,
      const chromeos::Error* error);

  void UpdateDeviceResource(const base::Closure& on_success,
                            const CloudRequestErrorCallback& on_failure);

  void FetchCommands(
      const base::Callback<void(const base::ListValue&)>& on_success,
      const CloudRequestErrorCallback& on_failure);

  // Processes the command list that is fetched from the server on connection.
  // Aborts commands which are in transitional states and publishes queued
  // commands which are queued.
  void ProcessInitialCommandList(const base::ListValue& commands);

  void PublishCommands(const base::ListValue& commands);
  void PublishCommand(const base::DictionaryValue& command);

  void PublishStateUpdates();
  void OnPublishStateSuccess(const base::DictionaryValue& reply);
  void OnPublishStateError(const chromeos::Error* error);

  // If unrecoverable error occurred (e.g. error parsing command instance),
  // notify the server that the command is aborted by the device.
  void NotifyCommandAborted(const std::string& command_id,
                            chromeos::ErrorPtr error);

  // When NotifyCommandAborted() fails, RetryNotifyCommandAborted() schedules
  // a retry attempt.
  void RetryNotifyCommandAborted(const std::string& command_id,
                                 chromeos::ErrorPtr error);

  // Builds Cloud API devices collection REST resource which matches
  // current state of the device including command definitions
  // for all supported commands and current device state.
  std::unique_ptr<base::DictionaryValue> BuildDeviceResource(
      chromeos::ErrorPtr* error);

  void SetRegistrationStatus(RegistrationStatus new_status);
  void SetDeviceId(const std::string& device_id);

  // Callback called when command definitions are changed to re-publish new CDD.
  void OnCommandDefsChanged();
  void OnStateChanged();

  // Overrides from NotificationDelegate
  void OnConnected(const std::string& channel_name) override;
  void OnDisconnected() override;
  void OnPermanentFailure() override;
  void OnCommandCreated(const base::DictionaryValue& command) override;

  // Transient data
  std::string access_token_;
  base::Time access_token_expiration_;

  // HTTP transport used for communications.
  std::shared_ptr<chromeos::http::Transport> transport_;
  // Global command manager.
  std::shared_ptr<CommandManager> command_manager_;
  // Device state manager.
  std::shared_ptr<StateManager> state_manager_;

  std::unique_ptr<BuffetConfig> config_;

  // Backoff manager for DoCloudRequest() method.
  std::unique_ptr<chromeos::BackoffEntry::Policy> cloud_backoff_policy_;
  std::unique_ptr<chromeos::BackoffEntry> cloud_backoff_entry_;

  // Flag set to true while a device state update patch request is in flight
  // to the cloud server.
  bool device_state_update_pending_{false};

  const bool notifications_enabled_;
  std::unique_ptr<NotificationChannel> primary_notification_channel_;
  std::unique_ptr<PullChannel> pull_channel_;
  NotificationChannel* current_notification_channel_{nullptr};
  bool notification_channel_starting_{false};

  // Tracks our current registration status.
  RegistrationStatus registration_status_{RegistrationStatus::kUnconfigured};

  std::vector<OnRegistrationChangedCallback> on_registration_changed_;

  base::WeakPtrFactory<DeviceRegistrationInfo> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(DeviceRegistrationInfo);
};

}  // namespace buffet

#endif  // BUFFET_DEVICE_REGISTRATION_INFO_H_
