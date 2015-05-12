// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/device_registration_info.h"

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/json/json_writer.h>
#include <base/message_loop/message_loop.h>
#include <base/values.h>
#include <chromeos/bind_lambda.h>
#include <chromeos/data_encoding.h>
#include <chromeos/errors/error_codes.h>
#include <chromeos/http/http_utils.h>
#include <chromeos/key_value_store.h>
#include <chromeos/mime_utils.h>
#include <chromeos/strings/string_utils.h>
#include <chromeos/url_utils.h>

#include "buffet/commands/cloud_command_proxy.h"
#include "buffet/commands/command_definition.h"
#include "buffet/commands/command_manager.h"
#include "buffet/commands/schema_constants.h"
#include "buffet/device_registration_storage_keys.h"
#include "buffet/org.chromium.Buffet.Manager.h"
#include "buffet/states/state_manager.h"
#include "buffet/utils.h"

const char buffet::kErrorDomainOAuth2[] = "oauth2";
const char buffet::kErrorDomainGCD[] = "gcd";
const char buffet::kErrorDomainGCDServer[] = "gcd_server";

namespace buffet {
namespace storage_keys {

// Persistent keys
const char kRefreshToken[] = "refresh_token";
const char kDeviceId[] = "device_id";
const char kRobotAccount[] = "robot_account";
const char kName[] = "name";
const char kDescription[] = "description";
const char kLocation[] = "location";
const char kAnonymousAccessRole[] = "anonymous_access_role";

}  // namespace storage_keys
}  // namespace buffet

namespace {

const int kMaxStartDeviceRetryDelayMinutes{1};
const int64_t kMinStartDeviceRetryDelaySeconds{5};
const int64_t kAbortCommandRetryDelaySeconds{5};

std::pair<std::string, std::string> BuildAuthHeader(
    const std::string& access_token_type,
    const std::string& access_token) {
  std::string authorization =
      chromeos::string_utils::Join(" ", access_token_type, access_token);
  return {chromeos::http::request_header::kAuthorization, authorization};
}

inline void SetUnexpectedError(chromeos::ErrorPtr* error) {
  chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainGCD,
                         "unexpected_response", "Unexpected GCD error");
}

void ParseGCDError(const base::DictionaryValue* json,
                   chromeos::ErrorPtr* error) {
  const base::Value* list_value = nullptr;
  const base::ListValue* error_list = nullptr;
  if (!json->Get("error.errors", &list_value) ||
      !list_value->GetAsList(&error_list)) {
    SetUnexpectedError(error);
    return;
  }

  for (size_t i = 0; i < error_list->GetSize(); i++) {
    const base::Value* error_value = nullptr;
    const base::DictionaryValue* error_object = nullptr;
    if (!error_list->Get(i, &error_value) ||
        !error_value->GetAsDictionary(&error_object)) {
      SetUnexpectedError(error);
      continue;
    }
    std::string error_code, error_message;
    if (error_object->GetString("reason", &error_code) &&
        error_object->GetString("message", &error_message)) {
      chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainGCDServer,
                             error_code, error_message);
    } else {
      SetUnexpectedError(error);
    }
  }
}

std::string BuildURL(const std::string& url,
                     const std::vector<std::string>& subpaths,
                     const chromeos::data_encoding::WebParamList& params) {
  std::string result = chromeos::url::CombineMultiple(url, subpaths);
  return chromeos::url::AppendQueryParams(result, params);
}

void IgnoreCloudError(const chromeos::Error*) {
}

void IgnoreCloudErrorWithCallback(const base::Closure& cb,
                                  const chromeos::Error*) {
  cb.Run();
}

void IgnoreCloudResult(const base::DictionaryValue&) {
}

void IgnoreCloudResultWithCallback(const base::Closure& cb,
                                   const base::DictionaryValue&) {
  cb.Run();
}

}  // anonymous namespace

namespace buffet {

DeviceRegistrationInfo::DeviceRegistrationInfo(
    const std::shared_ptr<CommandManager>& command_manager,
    const std::shared_ptr<StateManager>& state_manager,
    std::unique_ptr<BuffetConfig> config,
    const std::shared_ptr<chromeos::http::Transport>& transport,
    const std::shared_ptr<StorageInterface>& state_store,
    bool xmpp_enabled,
    org::chromium::Buffet::ManagerAdaptor* manager)
    : transport_{transport},
      storage_{state_store},
      command_manager_{command_manager},
      state_manager_{state_manager},
      config_{std::move(config)},
      xmpp_enabled_{xmpp_enabled},
      manager_{manager} {
  OnConfigChanged();
  command_manager_->AddOnCommandDefChanged(
      base::Bind(&DeviceRegistrationInfo::OnCommandDefsChanged,
                 weak_factory_.GetWeakPtr()));
}

DeviceRegistrationInfo::~DeviceRegistrationInfo() = default;

std::pair<std::string, std::string>
    DeviceRegistrationInfo::GetAuthorizationHeader() const {
  return BuildAuthHeader("Bearer", access_token_);
}

std::string DeviceRegistrationInfo::GetServiceURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  return BuildURL(config_->service_url(), {subpath}, params);
}

std::string DeviceRegistrationInfo::GetDeviceURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  CHECK(!device_id_.empty()) << "Must have a valid device ID";
  return BuildURL(config_->service_url(),
                  {"devices", device_id_, subpath}, params);
}

std::string DeviceRegistrationInfo::GetOAuthURL(
    const std::string& subpath,
    const chromeos::data_encoding::WebParamList& params) const {
  return BuildURL(config_->oauth_url(), {subpath}, params);
}

const std::string& DeviceRegistrationInfo::GetDeviceId() const {
  return device_id_;
}

bool DeviceRegistrationInfo::Load() {
  // Set kInvalidCredentials to trigger on_status_changed_ callback.
  registration_status_ = RegistrationStatus::kInvalidCredentials;
  SetRegistrationStatus(RegistrationStatus::kUnconfigured);

  auto value = storage_->Load();
  const base::DictionaryValue* dict = nullptr;
  if (!value || !value->GetAsDictionary(&dict))
    return false;

  // Read all available data before failing.
  std::string name;
  if (dict->GetString(storage_keys::kName, &name) && !name.empty())
    config_->set_name(name);

  std::string description;
  if (dict->GetString(storage_keys::kDescription, &description))
    config_->set_description(description);

  std::string location;
  if (dict->GetString(storage_keys::kLocation, &location))
    config_->set_location(location);

  std::string access_role;
  if (dict->GetString(storage_keys::kAnonymousAccessRole, &access_role))
    config_->set_anonymous_access_role(access_role);

  dict->GetString(storage_keys::kRefreshToken, &refresh_token_);
  dict->GetString(storage_keys::kRobotAccount, &device_robot_account_);

  std::string device_id;
  if (dict->GetString(storage_keys::kDeviceId, &device_id))
    SetDeviceId(device_id);

  OnConfigChanged();

  if (HaveRegistrationCredentials(nullptr)) {
    // Wait a significant amount of time for local daemons to publish their
    // state to Buffet before publishing it to the cloud.
    // TODO(wiley) We could do a lot of things here to either expose this
    //             timeout as a configurable knob or allow local
    //             daemons to signal that their state is up to date so that
    //             we need not wait for them.
    ScheduleStartDevice(base::TimeDelta::FromSeconds(5));
  }
  return true;
}

bool DeviceRegistrationInfo::Save() const {
  base::DictionaryValue dict;
  dict.SetString(storage_keys::kRefreshToken, refresh_token_);
  dict.SetString(storage_keys::kDeviceId,     device_id_);
  dict.SetString(storage_keys::kRobotAccount, device_robot_account_);
  dict.SetString(storage_keys::kName, config_->name());
  dict.SetString(storage_keys::kDescription, config_->description());
  dict.SetString(storage_keys::kLocation, config_->location());
  dict.SetString(storage_keys::kAnonymousAccessRole,
                 config_->anonymous_access_role());

  return storage_->Save(&dict);
}

void DeviceRegistrationInfo::ScheduleStartDevice(const base::TimeDelta& later) {
  SetRegistrationStatus(RegistrationStatus::kConnecting);
  base::MessageLoop* current = base::MessageLoop::current();
  if (!current)
    return;  // Assume we're in unittests
  base::TimeDelta max_delay =
      base::TimeDelta::FromMinutes(kMaxStartDeviceRetryDelayMinutes);
  base::TimeDelta min_delay =
      base::TimeDelta::FromSeconds(kMinStartDeviceRetryDelaySeconds);
  base::TimeDelta retry_delay = later * 2;
  if (retry_delay > max_delay) { retry_delay = max_delay; }
  if (retry_delay < min_delay) { retry_delay = min_delay; }
  current->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DeviceRegistrationInfo::StartDevice,
                 weak_factory_.GetWeakPtr(), nullptr,
                 retry_delay),
      later);
}

bool DeviceRegistrationInfo::CheckRegistration(chromeos::ErrorPtr* error) {
  return HaveRegistrationCredentials(error) &&
         MaybeRefreshAccessToken(error);
}

bool DeviceRegistrationInfo::HaveRegistrationCredentials(
    chromeos::ErrorPtr* error) {
  const bool have_credentials = !refresh_token_.empty() &&
                                !device_id_.empty() &&
                                !device_robot_account_.empty();

  VLOG(1) << "Device registration record "
          << ((have_credentials) ? "found" : "not found.");
  if (!have_credentials)
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainGCD,
                           "device_not_registered",
                           "No valid device registration record found");
  return have_credentials;
}

std::unique_ptr<base::DictionaryValue>
DeviceRegistrationInfo::ParseOAuthResponse(chromeos::http::Response* response,
                                           chromeos::ErrorPtr* error) {
  int code = 0;
  auto resp = chromeos::http::ParseJsonResponse(response, &code, error);
  if (resp && code >= chromeos::http::status_code::BadRequest) {
    std::string error_code, error_message;
    if (!resp->GetString("error", &error_code)) {
      error_code = "unexpected_response";
    }
    if (error_code == "invalid_grant") {
      LOG(INFO) << "The device's registration has been revoked.";
      SetRegistrationStatus(RegistrationStatus::kInvalidCredentials);
    }
    // I have never actually seen an error_description returned.
    if (!resp->GetString("error_description", &error_message)) {
      error_message = "Unexpected OAuth error";
    }
    chromeos::Error::AddTo(error, FROM_HERE, buffet::kErrorDomainOAuth2,
                           error_code, error_message);
    return std::unique_ptr<base::DictionaryValue>();
  }
  return resp;
}

bool DeviceRegistrationInfo::MaybeRefreshAccessToken(
    chromeos::ErrorPtr* error) {
  LOG(INFO) << "Checking access token expiration.";
  if (!access_token_.empty() &&
      !access_token_expiration_.is_null() &&
      access_token_expiration_ > base::Time::Now()) {
    LOG(INFO) << "Access token is still valid.";
    return true;
  }
  return RefreshAccessToken(error);
}

bool DeviceRegistrationInfo::RefreshAccessToken(
    chromeos::ErrorPtr* error) {
  LOG(INFO) << "Refreshing access token.";
  auto response = chromeos::http::PostFormDataAndBlock(GetOAuthURL("token"), {
    {"refresh_token", refresh_token_},
    {"client_id", config_->client_id()},
    {"client_secret", config_->client_secret()},
    {"grant_type", "refresh_token"},
  }, {}, transport_, error);
  if (!response)
    return false;

  auto json = ParseOAuthResponse(response.get(), error);
  if (!json)
    return false;

  int expires_in = 0;
  if (!json->GetString("access_token", &access_token_) ||
      !json->GetInteger("expires_in", &expires_in) ||
      access_token_.empty() ||
      expires_in <= 0) {
    LOG(ERROR) << "Access token unavailable.";
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainOAuth2,
                           "unexpected_server_response",
                           "Access token unavailable");
    return false;
  }
  access_token_expiration_ = base::Time::Now() +
                             base::TimeDelta::FromSeconds(expires_in);
  LOG(INFO) << "Access token is refreshed for additional " << expires_in
            << " seconds.";

  StartXmpp();

  return true;
}

void DeviceRegistrationInfo::StartXmpp() {
  if (!xmpp_enabled_) {
    LOG(WARNING) << "XMPP support disabled by flag.";
    return;
  }
  // If no MessageLoop assume we're in unittests.
  if (!base::MessageLoop::current()) {
    LOG(INFO) << "No MessageLoop, not starting XMPP";
    return;
  }

  if (!fd_watcher_.StopWatchingFileDescriptor()) {
    LOG(WARNING) << "Failed to stop the previous watcher";
    return;
  }

  std::unique_ptr<XmppConnection> connection(new XmppConnection());
  if (!connection->Initialize()) {
    LOG(WARNING) << "Failed to connect to XMPP server";
    return;
  }
  xmpp_client_.reset(new XmppClient(device_robot_account_, access_token_,
                                    std::move(connection)));
  if (!base::MessageLoopForIO::current()->WatchFileDescriptor(
          xmpp_client_->GetFileDescriptor(), true /* persistent */,
          base::MessageLoopForIO::WATCH_READ, &fd_watcher_, this)) {
    LOG(WARNING) << "Failed to watch XMPP file descriptor";
    return;
  }

  xmpp_client_->StartStream();
}

void DeviceRegistrationInfo::OnFileCanReadWithoutBlocking(int fd) {
  if (!xmpp_client_ || xmpp_client_->GetFileDescriptor() != fd)
    return;
  if (!xmpp_client_->Read()) {
    // Authentication failed or the socket was closed.
    if (!fd_watcher_.StopWatchingFileDescriptor())
      LOG(WARNING) << "Failed to stop the watcher";
    return;
  }
}

std::unique_ptr<base::DictionaryValue>
DeviceRegistrationInfo::BuildDeviceResource(chromeos::ErrorPtr* error) {
  // Limit only to commands that are visible to the cloud.
  auto commands = command_manager_->GetCommandDictionary().GetCommandsAsJson(
      [](const CommandDefinition* def) { return def->GetVisibility().cloud; },
      true, error);
  if (!commands)
    return nullptr;

  std::unique_ptr<base::DictionaryValue> state =
      state_manager_->GetStateValuesAsJson(error);
  if (!state)
    return nullptr;

  std::unique_ptr<base::DictionaryValue> resource{new base::DictionaryValue};
  if (!device_id_.empty())
    resource->SetString("id", device_id_);
  resource->SetString("name", config_->name());
  if (!config_->description().empty())
    resource->SetString("description", config_->description());
  if (!config_->location().empty())
    resource->SetString("location", config_->location());
  resource->SetString("modelManifestId", config_->model_id());
  resource->SetString("deviceKind", config_->device_kind());
  resource->SetString("channel.supportedType", "xmpp");
  resource->Set("commandDefs", commands.release());
  resource->Set("state", state.release());

  return resource;
}

std::unique_ptr<base::DictionaryValue> DeviceRegistrationInfo::GetDeviceInfo(
    chromeos::ErrorPtr* error) {
  if (!CheckRegistration(error))
    return std::unique_ptr<base::DictionaryValue>();

  // TODO(antonm): Switch to DoCloudRequest later.
  auto response = chromeos::http::GetAndBlock(
      GetDeviceURL(), {GetAuthorizationHeader()}, transport_, error);
  int status_code = 0;
  std::unique_ptr<base::DictionaryValue> json =
      chromeos::http::ParseJsonResponse(response.get(), &status_code, error);
  if (json) {
    if (status_code >= chromeos::http::status_code::BadRequest) {
      LOG(WARNING) << "Failed to retrieve the device info. Response code = "
                   << status_code;
      ParseGCDError(json.get(), error);
      return std::unique_ptr<base::DictionaryValue>();
    }
  }
  return json;
}

namespace {

bool GetWithDefault(const std::map<std::string, std::string>& source,
                    const std::string& key,
                    const std::string& default_value,
                    std::string* output) {
  auto it = source.find(key);
  if (it == source.end()) {
    *output = default_value;
    return false;
  }
  *output = it->second;
  return true;
}

}  // namespace

std::string DeviceRegistrationInfo::RegisterDevice(
    const std::map<std::string, std::string>& params,
    chromeos::ErrorPtr* error) {
  std::string ticket_id;
  if (!GetWithDefault(params, "ticket_id", "", &ticket_id)) {
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainBuffet,
                           "missing_parameter",
                           "Need ticket_id parameter for RegisterDevice()");
    return std::string();
  }

  // These fields are optional, and will default to values from the manufacturer
  // supplied config.
  std::string name;
  GetWithDefault(params, storage_keys::kName, config_->name(), &name);
  std::string description;
  GetWithDefault(params, storage_keys::kDescription, config_->description(),
                 &description);
  std::string location;
  GetWithDefault(params, storage_keys::kLocation, config_->location(),
                 &location);
  if (!UpdateDeviceInfo(name, description, location, error))
    return std::string();

  std::unique_ptr<base::DictionaryValue> device_draft =
      BuildDeviceResource(error);
  if (!device_draft)
    return std::string();

  base::DictionaryValue req_json;
  req_json.SetString("id", ticket_id);
  req_json.SetString("oauthClientId", config_->client_id());
  req_json.Set("deviceDraft", device_draft.release());

  auto url = GetServiceURL("registrationTickets/" + ticket_id,
                           {{"key", config_->api_key()}});
  std::unique_ptr<chromeos::http::Response> response =
      chromeos::http::PatchJsonAndBlock(url, &req_json, {}, transport_, error);
  auto json_resp = chromeos::http::ParseJsonResponse(response.get(), nullptr,
                                                     error);
  if (!json_resp)
    return std::string();
  if (!response->IsSuccessful()) {
    ParseGCDError(json_resp.get(), error);
    return std::string();
  }

  url = GetServiceURL("registrationTickets/" + ticket_id +
                      "/finalize?key=" + config_->api_key());
  response = chromeos::http::SendRequestWithNoDataAndBlock(
      chromeos::http::request_type::kPost, url, {}, transport_, error);
  if (!response)
    return std::string();
  json_resp = chromeos::http::ParseJsonResponse(response.get(), nullptr, error);
  if (!json_resp)
    return std::string();
  if (!response->IsSuccessful()) {
    ParseGCDError(json_resp.get(), error);
    return std::string();
  }

  std::string auth_code;
  std::string device_id;
  if (!json_resp->GetString("robotAccountEmail", &device_robot_account_) ||
      !json_resp->GetString("robotAccountAuthorizationCode", &auth_code) ||
      !json_resp->GetString("deviceDraft.id", &device_id)) {
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainGCD,
                           "unexpected_response",
                           "Device account missing in response");
    return std::string();
  }
  SetDeviceId(device_id);

  // Now get access_token and refresh_token
  response = chromeos::http::PostFormDataAndBlock(GetOAuthURL("token"), {
    {"code", auth_code},
    {"client_id", config_->client_id()},
    {"client_secret", config_->client_secret()},
    {"redirect_uri", "oob"},
    {"scope", "https://www.googleapis.com/auth/clouddevices"},
    {"grant_type", "authorization_code"}
  }, {}, transport_, error);
  if (!response)
    return std::string();

  json_resp = ParseOAuthResponse(response.get(), error);
  int expires_in = 0;
  if (!json_resp ||
      !json_resp->GetString("access_token", &access_token_) ||
      !json_resp->GetString("refresh_token", &refresh_token_) ||
      !json_resp->GetInteger("expires_in", &expires_in) ||
      access_token_.empty() ||
      refresh_token_.empty() ||
      expires_in <= 0) {
    chromeos::Error::AddTo(error, FROM_HERE,
                           kErrorDomainGCD, "unexpected_response",
                           "Device access_token missing in response");
    return std::string();
  }

  access_token_expiration_ = base::Time::Now() +
                             base::TimeDelta::FromSeconds(expires_in);

  Save();
  StartXmpp();

  // We're going to respond with our success immediately and we'll StartDevice
  // shortly after.
  ScheduleStartDevice(base::TimeDelta::FromSeconds(0));
  return device_id_;
}

namespace {

template <class T>
void PostToCallback(base::Callback<void(const T&)> callback,
                    std::unique_ptr<T> value) {
  auto cb = [callback] (T* result) {
    callback.Run(*result);
  };
  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(cb, base::Owned(value.release())));
}

using ResponsePtr = std::unique_ptr<chromeos::http::Response>;

void SendRequestWithRetries(
    const std::string& method,
    const std::string& url,
    const std::string& data,
    const std::string& mime_type,
    const chromeos::http::HeaderList& headers,
    std::shared_ptr<chromeos::http::Transport> transport,
    int num_retries,
    const chromeos::http::SuccessCallback& success_callback,
    const chromeos::http::ErrorCallback& error_callback) {
  auto on_failure =
      [method, url, data, mime_type, headers, transport, num_retries,
      success_callback, error_callback](int request_id,
                                        const chromeos::Error* error) {
    if (num_retries > 0) {
      SendRequestWithRetries(method, url, data, mime_type,
                             headers, transport, num_retries - 1,
                             success_callback, error_callback);
    } else {
      error_callback.Run(request_id, error);
    }
  };

  auto on_success =
      [on_failure, success_callback, error_callback](int request_id,
                                                     ResponsePtr response) {
    int status_code = response->GetStatusCode();
    if (status_code >= chromeos::http::status_code::Continue &&
        status_code < chromeos::http::status_code::BadRequest) {
      success_callback.Run(request_id, std::move(response));
      return;
    }

    // TODO(antonm): Should add some useful information to error.
    LOG(WARNING) << "Request failed. Response code = " << status_code;

    chromeos::ErrorPtr error;
    chromeos::Error::AddTo(&error, FROM_HERE, chromeos::errors::http::kDomain,
                           std::to_string(status_code),
                           response->GetStatusText());
    if (status_code >= chromeos::http::status_code::InternalServerError &&
        status_code < 600) {
      // Request was valid, but server failed, retry.
      // TODO(antonm): Implement exponential backoff.
      // TODO(antonm): Reconsider status codes, maybe only some require
      // retry.
      // TODO(antonm): Support Retry-After header.
      on_failure(request_id, error.get());
    } else {
      error_callback.Run(request_id, error.get());
    }
  };

  chromeos::http::SendRequest(method, url, data.c_str(), data.size(),
                              mime_type, headers, transport,
                              base::Bind(on_success),
                              base::Bind(on_failure));
}

}  // namespace

void DeviceRegistrationInfo::DoCloudRequest(
    const std::string& method,
    const std::string& url,
    const base::DictionaryValue* body,
    const CloudRequestCallback& success_callback,
    const CloudRequestErrorCallback& error_callback) {
  // TODO(antonm): Add reauthorization on access token expiration (do not
  // forget about 5xx when fetching new access token).
  // TODO(antonm): Add support for device removal.

  std::string data;
  if (body)
    base::JSONWriter::Write(body, &data);

  const std::string mime_type{chromeos::mime::AppendParameter(
      chromeos::mime::application::kJson,
      chromeos::mime::parameters::kCharset,
      "utf-8")};

  auto status_cb = base::Bind(&DeviceRegistrationInfo::SetRegistrationStatus,
                              weak_factory_.GetWeakPtr());

  auto request_cb = [success_callback, error_callback, status_cb](
      int request_id, ResponsePtr response) {
    status_cb.Run(RegistrationStatus::kConnected);
    chromeos::ErrorPtr error;

    std::unique_ptr<base::DictionaryValue> json_resp{
        chromeos::http::ParseJsonResponse(response.get(), nullptr, &error)};
    if (!json_resp) {
      error_callback.Run(error.get());
      return;
    }

    success_callback.Run(*json_resp);
  };

  auto error_cb =
      [error_callback](int request_id, const chromeos::Error* error) {
    error_callback.Run(error);
  };

  auto transport = transport_;
  auto error_callackback_with_reauthorization = base::Bind(
      [method, url, data, mime_type, transport, request_cb, error_cb,
       status_cb](DeviceRegistrationInfo* self, int request_id,
                  const chromeos::Error* error) {
        status_cb.Run(RegistrationStatus::kConnecting);
        if (error->HasError(
                chromeos::errors::http::kDomain,
                std::to_string(chromeos::http::status_code::Denied))) {
          chromeos::ErrorPtr reauthorization_error;
          // Forcibly refresh the access token.
          if (!self->RefreshAccessToken(&reauthorization_error)) {
            // TODO(antonm): Check if the device has been actually removed.
            error_cb(request_id, reauthorization_error.get());
            return;
          }
          SendRequestWithRetries(method, url, data, mime_type,
                                 {self->GetAuthorizationHeader()}, transport, 7,
                                 base::Bind(request_cb), base::Bind(error_cb));
        } else {
          error_cb(request_id, error);
        }
      },
      base::Unretained(this));

  SendRequestWithRetries(method, url,
                         data, mime_type,
                         {GetAuthorizationHeader()},
                         transport,
                         7,
                         base::Bind(request_cb),
                         error_callackback_with_reauthorization);
}

void DeviceRegistrationInfo::StartDevice(
    chromeos::ErrorPtr* error,
    const base::TimeDelta& retry_delay) {
  if (!HaveRegistrationCredentials(error))
    return;
  auto handle_start_device_failure_cb = base::Bind(
      &IgnoreCloudErrorWithCallback,
      base::Bind(&DeviceRegistrationInfo::ScheduleStartDevice,
                 weak_factory_.GetWeakPtr(),
                 retry_delay));
  // "Starting" a device just means that we:
  //   1) push an updated device resource
  //   2) fetch an initial set of outstanding commands
  //   3) abort any commands that we've previously marked as "in progress"
  //      or as being in an error state.
  //   4) Initiate periodic polling for commands.
  auto periodically_poll_commands_cb = base::Bind(
      &DeviceRegistrationInfo::PeriodicallyPollCommands,
      weak_factory_.GetWeakPtr());
  auto abort_commands_cb = base::Bind(
      &DeviceRegistrationInfo::AbortLimboCommands,
      weak_factory_.GetWeakPtr(),
      periodically_poll_commands_cb);
  auto fetch_commands_cb = base::Bind(
      &DeviceRegistrationInfo::FetchCommands,
      weak_factory_.GetWeakPtr(),
      abort_commands_cb,
      handle_start_device_failure_cb);
  UpdateDeviceResource(fetch_commands_cb, handle_start_device_failure_cb);
}

bool DeviceRegistrationInfo::UpdateDeviceInfo(const std::string& name,
                                              const std::string& description,
                                              const std::string& location,
                                              chromeos::ErrorPtr* error) {
  if (name.empty()) {
    chromeos::Error::AddTo(error, FROM_HERE, kErrorDomainBuffet,
                           "invalid_parameter", "Empty device name");
    return false;
  }
  config_->set_name(name);
  config_->set_description(description);
  config_->set_location(location);

  Save();

  OnConfigChanged();

  if (HaveRegistrationCredentials(nullptr)) {
    UpdateDeviceResource(base::Bind(&base::DoNothing),
                         base::Bind(&IgnoreCloudError));
  }

  return true;
}

void DeviceRegistrationInfo::UpdateCommand(
    const std::string& command_id,
    const base::DictionaryValue& command_patch,
    const base::Closure& on_success,
    const base::Closure& on_error) {
  DoCloudRequest(
      chromeos::http::request_type::kPatch,
      GetServiceURL("commands/" + command_id),
      &command_patch,
      base::Bind(&IgnoreCloudResultWithCallback, on_success),
      base::Bind(&IgnoreCloudErrorWithCallback, on_error));
}

void DeviceRegistrationInfo::NotifyCommandAborted(
    const std::string& command_id,
    chromeos::ErrorPtr error) {
  base::DictionaryValue command_patch;
  command_patch.SetString(commands::attributes::kCommand_State,
                          CommandInstance::kStatusAborted);
  if (error) {
    command_patch.SetString(commands::attributes::kCommand_ErrorCode,
                            chromeos::string_utils::Join(":",
                                                         error->GetDomain(),
                                                         error->GetCode()));
    std::vector<std::string> messages;
    const chromeos::Error* current_error = error.get();
    while (current_error) {
      messages.push_back(current_error->GetMessage());
      current_error = current_error->GetInnerError();
    }
    command_patch.SetString(commands::attributes::kCommand_ErrorMessage,
                            chromeos::string_utils::Join(";", messages));
  }
  UpdateCommand(command_id,
                command_patch,
                base::Bind(&base::DoNothing),
                base::Bind(&DeviceRegistrationInfo::RetryNotifyCommandAborted,
                           weak_factory_.GetWeakPtr(),
                           command_id, base::Passed(std::move(error))));
}

void DeviceRegistrationInfo::RetryNotifyCommandAborted(
    const std::string& command_id,
    chromeos::ErrorPtr error) {
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DeviceRegistrationInfo::NotifyCommandAborted,
                 weak_factory_.GetWeakPtr(),
                 command_id, base::Passed(std::move(error))),
      base::TimeDelta::FromSeconds(kAbortCommandRetryDelaySeconds));
}

void DeviceRegistrationInfo::UpdateDeviceResource(
    const base::Closure& on_success,
    const CloudRequestErrorCallback& on_failure) {
  VLOG(1) << "Updating GCD server with CDD...";
  std::unique_ptr<base::DictionaryValue> device_resource =
      BuildDeviceResource(nullptr);
  if (!device_resource)
    return;

  DoCloudRequest(
      chromeos::http::request_type::kPut,
      GetDeviceURL(),
      device_resource.get(),
      base::Bind(&IgnoreCloudResultWithCallback, on_success),
      on_failure);
}

namespace {

void HandleFetchCommandsResult(
    const base::Callback<void(const base::ListValue&)>& callback,
    const base::DictionaryValue& json) {
  const base::ListValue* commands{nullptr};
  if (!json.GetList("commands", &commands)) {
    VLOG(1) << "No commands in the response.";
  }
  const base::ListValue empty;
  callback.Run(commands ? *commands : empty);
}

}  // namespace

void DeviceRegistrationInfo::FetchCommands(
    const base::Callback<void(const base::ListValue&)>& on_success,
    const CloudRequestErrorCallback& on_failure) {
  DoCloudRequest(
      chromeos::http::request_type::kGet,
      GetServiceURL("commands/queue", {{"deviceId", device_id_}}),
      nullptr,
      base::Bind(&HandleFetchCommandsResult, on_success),
      on_failure);
}

void DeviceRegistrationInfo::AbortLimboCommands(
    const base::Closure& callback, const base::ListValue& commands) {
  const size_t size{commands.GetSize()};
  for (size_t i = 0; i < size; ++i) {
    const base::DictionaryValue* command{nullptr};
    if (!commands.GetDictionary(i, &command)) {
      LOG(WARNING) << "No command resource at " << i;
      continue;
    }
    std::string command_state;
    if (!command->GetString("state", &command_state)) {
      LOG(WARNING) << "Command with no state at " << i;
      continue;
    }
    if (command_state != "error" &&
        command_state != "inProgress" &&
        command_state != "paused") {
      // It's not a limbo command, ignore.
      continue;
    }
    std::string command_id;
    if (!command->GetString("id", &command_id)) {
      LOG(WARNING) << "Command with no ID at " << i;
      continue;
    }

    std::unique_ptr<base::DictionaryValue> command_copy{command->DeepCopy()};
    command_copy->SetString("state", "aborted");
    // TODO(wiley) We could consider handling this error case more gracefully.
    DoCloudRequest(
        chromeos::http::request_type::kPut,
        GetServiceURL("commands/" + command_id),
        command_copy.get(),
        base::Bind(&IgnoreCloudResult), base::Bind(&IgnoreCloudError));
  }

  base::MessageLoop::current()->PostTask(FROM_HERE, callback);
}

void DeviceRegistrationInfo::PeriodicallyPollCommands() {
  VLOG(1) << "Poll commands";
  command_poll_timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromMilliseconds(config_->polling_period_ms()),
      base::Bind(&DeviceRegistrationInfo::FetchCommands,
                 base::Unretained(this),
                 base::Bind(&DeviceRegistrationInfo::PublishCommands,
                            base::Unretained(this)),
                            base::Bind(&IgnoreCloudError)));
  // TODO(antonm): Use better trigger: when StateManager registers new updates,
  // it should call closure which will post a task, probably with some
  // throttling, to publish state updates.
  state_push_timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromMilliseconds(config_->polling_period_ms()),
      base::Bind(&DeviceRegistrationInfo::PublishStateUpdates,
                 base::Unretained(this)));
}

void DeviceRegistrationInfo::PublishCommands(const base::ListValue& commands) {
  const CommandDictionary& command_dictionary =
      command_manager_->GetCommandDictionary();

  const size_t size{commands.GetSize()};
  for (size_t i = 0; i < size; ++i) {
    const base::DictionaryValue* command{nullptr};
    if (!commands.GetDictionary(i, &command)) {
      LOG(WARNING) << "No command resource at " << i;
      continue;
    }

    std::string command_id;
    chromeos::ErrorPtr error;
    auto command_instance = CommandInstance::FromJson(
        command, commands::attributes::kCommand_Visibility_Cloud,
        command_dictionary, &command_id, &error);
    if (!command_instance) {
      LOG(WARNING) << "Failed to parse a command with ID: " << command_id;
      if (!command_id.empty())
        NotifyCommandAborted(command_id, std::move(error));
      continue;
    }

    // TODO(antonm): Properly process cancellation of commands.
    if (!command_manager_->FindCommand(command_instance->GetID())) {
      std::unique_ptr<CommandProxyInterface> cloud_proxy{
          new CloudCommandProxy(command_instance.get(), this)};
      command_instance->AddProxy(std::move(cloud_proxy));
      command_manager_->AddCommand(std::move(command_instance));
    }
  }
}

void DeviceRegistrationInfo::PublishStateUpdates() {
  VLOG(1) << "PublishStateUpdates";
  const std::vector<StateChange> state_changes{
      state_manager_->GetAndClearRecordedStateChanges()};
  if (state_changes.empty())
    return;

  std::unique_ptr<base::ListValue> patches{new base::ListValue};
  for (const auto& state_change : state_changes) {
    std::unique_ptr<base::DictionaryValue> patch{new base::DictionaryValue};
    patch->SetString("timeMs",
                     std::to_string(state_change.timestamp.ToJavaTime()));

    std::unique_ptr<base::DictionaryValue> changes{new base::DictionaryValue};
    for (const auto& pair : state_change.changed_properties) {
      auto value = pair.second->ToJson(nullptr);
      if (!value) {
        return;
      }
      // The key in |pair.first| is the full property name in format
      // "package.property_name", so must use DictionaryValue::Set() instead of
      // DictionaryValue::SetWithoutPathExpansion to recreate the JSON
      // property tree properly.
      changes->Set(pair.first, value.release());
    }
    patch->Set("patch", changes.release());

    patches->Append(patch.release());
  }

  base::DictionaryValue body;
  body.SetString("requestTimeMs",
                 std::to_string(base::Time::Now().ToJavaTime()));
  body.Set("patches", patches.release());

  DoCloudRequest(
      chromeos::http::request_type::kPost,
      GetDeviceURL("patchState"),
      &body,
      base::Bind(&IgnoreCloudResult), base::Bind(&IgnoreCloudError));
}

void DeviceRegistrationInfo::SetRegistrationStatus(
    RegistrationStatus new_status) {
  registration_status_ = new_status;
  if (manager_)
    manager_->SetStatus(StatusToString(registration_status_));
  VLOG_IF(1, new_status != registration_status_)
      << "Changing registration status to " << StatusToString(new_status);
}

void DeviceRegistrationInfo::SetDeviceId(const std::string& device_id) {
  device_id_ = device_id;
  if (manager_)
    manager_->SetDeviceId(device_id_);
}

void DeviceRegistrationInfo::OnConfigChanged() {
  if (!manager_)
    return;
  manager_->SetOemName(config_->oem_name());
  manager_->SetModelName(config_->model_name());
  manager_->SetModelId(config_->model_id());
  manager_->SetName(config_->name());
  manager_->SetDescription(config_->description());
  manager_->SetLocation(config_->location());
  manager_->SetAnonymousAccessRole(config_->anonymous_access_role());
}

void DeviceRegistrationInfo::OnCommandDefsChanged() {
  VLOG(1) << "CommandDefinitionChanged notification received";
  if (!HaveRegistrationCredentials(nullptr))
    return;

  UpdateDeviceResource(base::Bind(&base::DoNothing),
                       base::Bind(&IgnoreCloudError));
}

}  // namespace buffet
