// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_CONTEXT_H_
#define SHILL_NETWORK_NETWORK_CONTEXT_H_

#include <optional>
#include <string>
#include <string_view>

namespace shill {

// NetworkContext contains the logging-related states for a Network object and
// its subcomponents. Must not contain any PII data that cannot be automatically
// removed by the feedback report redaction tool.
class NetworkContext {
 public:
  explicit NetworkContext(std::string_view ifname);
  ~NetworkContext();

  // Disallow copy to avoid that some object keeps an out-of-date context.
  NetworkContext(const NetworkContext&) = delete;
  NetworkContext& operator=(const NetworkContext&) = delete;

  const std::string& logging_tag() const { return logging_tag_; }

  void SetServiceLoggingName(std::string_view name);
  void ClearServiceLoggingName();

  // session_id is an identifier for each network session (from Network::Start()
  // to Network::Stop()). This id is unique across the lifetime of the shill
  // process (regardless of overflow).
  // TODO(b/371904984): Make it unique across shill restart.
  // Assigns a new session_id to this context.
  void UpdateSessionId();
  // Clears the current session_id in this context.
  void ClearSessionId();

 private:
  static int next_session_id_;

  void GenerateLoggingTag();

  const std::string ifname_;
  std::string service_logging_name_;
  std::optional<int> session_id_;

  std::string logging_tag_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_CONTEXT_H_
