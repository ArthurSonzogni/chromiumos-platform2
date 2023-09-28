// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/openvpn_management_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <string>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/data_encoding.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/logging.h"
#include "shill/vpn/openvpn_driver.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
}  // namespace Logging

namespace {
constexpr char kPasswordTagAuth[] = "Auth";
}  // namespace

OpenVPNManagementServer::OpenVPNManagementServer(OpenVPNDriver* driver)
    : driver_(driver), hold_waiting_(false), hold_release_(false) {}

OpenVPNManagementServer::~OpenVPNManagementServer() {
  OpenVPNManagementServer::Stop();
}

bool OpenVPNManagementServer::Start(
    std::vector<std::vector<std::string>>* options) {
  SLOG(2) << __func__;
  if (IsStarted()) {
    return true;
  }

  auto socket =
      socket_factory_->Create(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
  if (!socket) {
    PLOG(ERROR) << "Unable to create management server socket.";
    return false;
  }

  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (!socket->Bind(reinterpret_cast<struct sockaddr*>(&addr), addrlen) ||
      !socket->Listen(1) ||
      !socket->GetSockName(reinterpret_cast<struct sockaddr*>(&addr),
                           &addrlen)) {
    PLOG(ERROR) << "Socket setup failed.";
    return false;
  }

  SLOG(2) << "Listening socket: " << socket;
  socket_ = std::move(socket);

  socket_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      socket_->Get(),
      base::BindRepeating(&OpenVPNManagementServer::OnAcceptReady,
                          base::Unretained(this)));
  if (!socket_watcher_) {
    LOG(ERROR) << "Failed to watch on listening socket.";
    return false;
  }

  // Append openvpn management API options.
  driver_->AppendOption("management", inet_ntoa(addr.sin_addr),
                        base::NumberToString(ntohs(addr.sin_port)), options);
  driver_->AppendOption("management-client", options);
  driver_->AppendOption("management-hold", options);
  hold_release_ = false;
  hold_waiting_ = false;

  driver_->AppendOption("management-query-passwords", options);
  if (driver_->AppendValueOption(kOpenVPNStaticChallengeProperty,
                                 "static-challenge", options)) {
    options->back().push_back("1");  // Force echo.
  }
  return true;
}

void OpenVPNManagementServer::Stop() {
  SLOG(2) << __func__;
  if (!IsStarted()) {
    return;
  }
  state_.clear();

  connected_socket_watcher_.reset();
  connected_socket_.reset();
  socket_watcher_.reset();
  socket_.reset();
}

void OpenVPNManagementServer::ReleaseHold() {
  SLOG(2) << __func__;
  hold_release_ = true;
  if (!hold_waiting_) {
    return;
  }
  LOG(INFO) << "Releasing hold.";
  hold_waiting_ = false;
  SendHoldRelease();
}

void OpenVPNManagementServer::Hold() {
  SLOG(2) << __func__;
  hold_release_ = false;
}

void OpenVPNManagementServer::Restart() {
  LOG(INFO) << "Restart.";
  SendSignal("SIGUSR1");
}

void OpenVPNManagementServer::OnAcceptReady() {
  SLOG(2) << __func__;

  connected_socket_watcher_.reset();
  connected_socket_ = socket_->Accept(nullptr, nullptr);
  if (!connected_socket_) {
    PLOG(ERROR) << "Accept on listen socket failed.";
    return;
  }
  socket_watcher_.reset();

  connected_socket_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      connected_socket_->Get(),
      base::BindRepeating(&OpenVPNManagementServer::OnInputReady,
                          base::Unretained(this)));
  if (!connected_socket_watcher_) {
    LOG(ERROR) << "Failed on watching the connected socket.";
    return;
  }
  SendState("on");
}

void OpenVPNManagementServer::OnInputReady() {
  uint8_t buf[4096];
  ssize_t len = read(connected_socket_->Get(), buf, sizeof(buf));
  if (len > 0) {
    OnInput({buf, static_cast<size_t>(len)});
  } else {
    PLOG(ERROR) << "Failed to read from connected socket";
    driver_->FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
  }
}

void OpenVPNManagementServer::OnInput(base::span<const uint8_t> data) {
  SLOG(2) << __func__ << "(" << data.size() << ")";
  const std::vector<std::string> messages = base::SplitString(
      std::string(reinterpret_cast<const char*>(data.data()), data.size()),
      "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const auto& message : messages) {
    if (!IsStarted()) {
      break;
    }
    ProcessMessage(message);
  }
}

void OpenVPNManagementServer::ProcessMessage(std::string_view message) {
  SLOG(2) << __func__ << "(" << message << ")";
  if (message.empty()) {
    return;
  }
  if (!ProcessInfoMessage(message) && !ProcessNeedPasswordMessage(message) &&
      !ProcessFailedPasswordMessage(message) &&
      !ProcessAuthTokenMessage(message) && !ProcessStateMessage(message) &&
      !ProcessHoldMessage(message) && !ProcessSuccessMessage(message) &&
      !ProcessStatusMessage(message)) {
    LOG(WARNING) << "Message ignored: " << message;
  }
}

bool OpenVPNManagementServer::ProcessInfoMessage(std::string_view message) {
  if (!base::StartsWith(message, ">INFO:", base::CompareCase::SENSITIVE)) {
    return false;
  }
  LOG(INFO) << message;
  return true;
}

bool OpenVPNManagementServer::ProcessNeedPasswordMessage(
    std::string_view message) {
  if (!base::StartsWith(message, ">PASSWORD:Need ",
                        base::CompareCase::SENSITIVE)) {
    return false;
  }
  LOG(INFO) << "Processing need-password message.";
  const std::string_view tag = ParsePasswordTag(message);
  if (tag == kPasswordTagAuth) {
    if (message.find("SC:") != std::string::npos) {
      PerformStaticChallenge(tag);
    } else {
      PerformAuthentication(tag);
    }
  } else if (base::StartsWith(tag, "User-Specific TPM Token",
                              base::CompareCase::SENSITIVE)) {
    SupplyTPMToken(tag);
  } else {
    NOTIMPLEMENTED() << ": Unsupported need-password message: " << message;
    driver_->FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
  }
  return true;
}

// static
std::string_view OpenVPNManagementServer::ParseSubstring(
    std::string_view message, std::string_view start, std::string_view end) {
  SLOG(2) << __func__ << "(" << message << ", " << start << ", " << end << ")";
  size_t start_pos = message.find(start);
  if (start_pos == std::string::npos) {
    return "";
  }
  size_t end_pos = message.find(end, start_pos + start.size());
  if (end_pos == std::string::npos) {
    return "";
  }
  return message.substr(start_pos + start.size(),
                        end_pos - start_pos - start.size());
}

// static
std::string_view OpenVPNManagementServer::ParsePasswordTag(
    std::string_view message) {
  return ParseSubstring(message, "'", "'");
}

// static
std::string_view OpenVPNManagementServer::ParsePasswordFailedReason(
    std::string_view message) {
  return ParseSubstring(message, "['", "']");
}

void OpenVPNManagementServer::PerformStaticChallenge(std::string_view tag) {
  LOG(INFO) << "Perform static challenge: " << tag;
  const auto user =
      driver_->args()->Lookup<std::string>(kOpenVPNUserProperty, "");
  const auto password =
      driver_->args()->Lookup<std::string>(kOpenVPNPasswordProperty, "");
  const auto otp =
      driver_->args()->Lookup<std::string>(kOpenVPNOTPProperty, "");
  const auto token =
      driver_->args()->Lookup<std::string>(kOpenVPNTokenProperty, "");
  if (user.empty() || (token.empty() && (password.empty() || otp.empty()))) {
    NOTIMPLEMENTED() << ": Missing credentials:"
                     << (user.empty() ? " no-user" : "")
                     << (token.empty() ? " no-token" : "")
                     << (password.empty() ? " no-password" : "")
                     << (otp.empty() ? " no-otp" : "");
    driver_->FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
    return;
  }

  std::string password_encoded;
  if (!token.empty()) {
    password_encoded = token;
    // Don't reuse token.
    driver_->args()->Remove(kOpenVPNTokenProperty);
  } else {
    std::string b64_password(brillo::data_encoding::Base64Encode(password));
    std::string b64_otp(brillo::data_encoding::Base64Encode(otp));
    password_encoded = base::StringPrintf("SCRV1:%s:%s", b64_password.c_str(),
                                          b64_otp.c_str());
    // Don't reuse OTP.
    driver_->args()->Remove(kOpenVPNOTPProperty);
  }
  SendUsername(tag, user);
  SendPassword(tag, password_encoded);
}

void OpenVPNManagementServer::PerformAuthentication(std::string_view tag) {
  LOG(INFO) << "Perform authentication: " << tag;
  const auto user =
      driver_->args()->Lookup<std::string>(kOpenVPNUserProperty, "");
  const auto password =
      driver_->args()->Lookup<std::string>(kOpenVPNPasswordProperty, "");
  if (user.empty() || password.empty()) {
    NOTIMPLEMENTED() << ": Missing credentials:"
                     << (user.empty() ? " no-user" : "")
                     << (password.empty() ? " no-password" : "");
    driver_->FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
    return;
  }
  SendUsername(tag, user);
  SendPassword(tag, password);
}

void OpenVPNManagementServer::SupplyTPMToken(std::string_view tag) {
  SLOG(2) << __func__ << "(" << tag << ")";
  const auto pin =
      driver_->args()->Lookup<std::string>(kOpenVPNPinProperty, "");
  if (pin.empty()) {
    NOTIMPLEMENTED() << ": Missing PIN.";
    driver_->FailService(Service::kFailureInternal, Service::kErrorDetailsNone);
    return;
  }
  SendPassword(tag, pin);
}

bool OpenVPNManagementServer::ProcessFailedPasswordMessage(
    std::string_view message) {
  if (!base::StartsWith(message, ">PASSWORD:Verification Failed:",
                        base::CompareCase::SENSITIVE)) {
    return false;
  }
  LOG(INFO) << message;
  std::string reason;
  if (ParsePasswordTag(message) == kPasswordTagAuth) {
    reason = ParsePasswordFailedReason(message);
  }
  driver_->FailService(Service::kFailureConnect, reason);
  return true;
}

bool OpenVPNManagementServer::ProcessAuthTokenMessage(
    std::string_view message) {
  if (!base::StartsWith(
          message, ">PASSWORD:Auth-Token:", base::CompareCase::SENSITIVE)) {
    return false;
  }
  LOG(INFO) << "Auth-Token message ignored.";
  return true;
}

// >STATE:* message support. State messages are of the form:
//    >STATE:<date>,<state>,<detail>,<local-ip>,<remote-ip>
// where:
// <date> is the current time (since epoch) in seconds
// <state> is one of:
//    INITIAL, CONNECTING, WAIT, AUTH, GET_CONFIG, ASSIGN_IP, ADD_ROUTES,
//    CONNECTED, RECONNECTING, EXITING, RESOLVE, TCP_CONNECT
// <detail> is a free-form string giving details about the state change
// <local-ip> is a dotted-quad for the local IPv4 address (when available)
// <remote-ip> is a dotted-quad for the remote IPv4 address (when available)
bool OpenVPNManagementServer::ProcessStateMessage(std::string_view message) {
  if (!base::StartsWith(message, ">STATE:", base::CompareCase::SENSITIVE)) {
    return false;
  }
  auto details = base::SplitString(message, ",", base::TRIM_WHITESPACE,
                                   base::SPLIT_WANT_ALL);
  if (details.size() > 1) {
    std::string new_state = details[1];
    std::string reason;
    if (details.size() > 2) {
      reason = details[2];
    }
    LOG(INFO) << "OpenVPN state: " << state_ << " -> " << new_state << " ("
              << reason << ")";

    if (new_state == kStateReconnecting) {
      if (state_ == kStateResolve) {
        // RESOLVE -> RECONNECTING means DNS lookup failed.
        driver_->FailService(Service::kFailureDNSLookup,
                             Service::kErrorDetailsNone);
      } else if (state_ == kStateAuth && reason == "tls-error") {
        // AUTH -> RECONNECTING,tls_error means cert validation or auth
        // failed.  Unfortunately OpenVPN doesn't tell us whether it
        // was a local or remote failure.  The UI will say:
        // "Authentication certificate rejected by network"
        driver_->FailService(Service::kFailureIPsecCertAuth,
                             Service::kErrorDetailsNone);
      } else {
        OpenVPNDriver::ReconnectReason reconnect_reason =
            OpenVPNDriver::kReconnectReasonUnknown;
        if (reason == "tls-error") {
          reconnect_reason = OpenVPNDriver::kReconnectReasonTLSError;
        }
        driver_->OnReconnecting(reconnect_reason);
      }
    }
    if (new_state == kStateConnected) {
      // Ask for status once state become connected to collect cipher info
      SendStatus();
    }
    state_ = new_state;
  }

  return true;
}

bool OpenVPNManagementServer::ProcessHoldMessage(std::string_view message) {
  if (!base::StartsWith(message, ">HOLD:Waiting for hold release",
                        base::CompareCase::SENSITIVE)) {
    return false;
  }
  LOG(INFO) << "Client waiting for hold release.";
  hold_waiting_ = true;
  if (hold_release_) {
    ReleaseHold();
  }
  return true;
}

bool OpenVPNManagementServer::ProcessSuccessMessage(std::string_view message) {
  if (!base::StartsWith(message, "SUCCESS: ", base::CompareCase::SENSITIVE)) {
    return false;
  }
  LOG(INFO) << message;
  return true;
}

bool OpenVPNManagementServer::ProcessStatusMessage(std::string_view message) {
  if (base::StartsWith(message, "OpenVPN STATISTICS",
                       base::CompareCase::SENSITIVE) ||
      base::StartsWith(message, "Updated,", base::CompareCase::SENSITIVE) ||
      base::StartsWith(message, "TUN/TAP ", base::CompareCase::SENSITIVE) ||
      base::StartsWith(message, "TCP/UDP ", base::CompareCase::SENSITIVE) ||
      base::StartsWith(message, "Auth read bytes,",
                       base::CompareCase::SENSITIVE) ||
      message == "END") {
    // Ignore unconcerned status lines
    return true;
  }
  // Note that this line comes from a CHROMIUM-only patch in crrev.com/c/3256270
  // and is not in upstream openvpn code.
  if (!base::StartsWith(message, "Data channel cipher,",
                        base::CompareCase::SENSITIVE)) {
    return false;
  }
  auto details = base::SplitString(message, ",", base::TRIM_WHITESPACE,
                                   base::SPLIT_WANT_ALL);
  if (details.size() == 2) {
    std::string cipher = details[1];
    LOG(INFO) << "Negotiated cipher: " << cipher;
    driver_->ReportCipherMetrics(cipher);
  }
  return true;
}

// static
std::string OpenVPNManagementServer::EscapeToQuote(std::string_view str) {
  std::string escaped;
  for (auto ch : str) {
    if (ch == '\\' || ch == '"') {
      escaped += '\\';
    }
    escaped += ch;
  }
  return escaped;
}

void OpenVPNManagementServer::Send(std::string_view data) {
  SLOG(2) << __func__;
  if (!connected_socket_) {
    LOG(ERROR) << "Send() is called but the socket is not accepted yet";
    return;
  }
  const auto len = connected_socket_->Send(
      {reinterpret_cast<const uint8_t*>(data.data()), data.size()},
      MSG_NOSIGNAL);
  PLOG_IF(ERROR, len != data.size()) << "Send failed.";
}

void OpenVPNManagementServer::SendState(std::string_view state) {
  SLOG(2) << __func__ << "(" << state << ")";
  Send(base::StrCat({"state ", state, "\n"}));
}

void OpenVPNManagementServer::SendUsername(std::string_view tag,
                                           std::string_view username) {
  SLOG(2) << __func__;
  Send(base::StringPrintf("username \"%s\" \"%s\"\n",
                          EscapeToQuote(tag).c_str(),
                          EscapeToQuote(username).c_str()));
}

void OpenVPNManagementServer::SendPassword(std::string_view tag,
                                           std::string_view password) {
  SLOG(2) << __func__;
  Send(base::StringPrintf("password \"%s\" \"%s\"\n",
                          EscapeToQuote(tag).c_str(),
                          EscapeToQuote(password).c_str()));
}

void OpenVPNManagementServer::SendSignal(std::string_view signal) {
  SLOG(2) << __func__ << "(" << signal << ")";
  Send(base::StrCat({"signal ", signal, "\n"}));
}

void OpenVPNManagementServer::SendStatus() {
  SLOG(2) << __func__;
  Send("status\n");
}

void OpenVPNManagementServer::SendHoldRelease() {
  SLOG(2) << __func__;
  Send("hold release\n");
}

}  // namespace shill
