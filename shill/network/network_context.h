// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_CONTEXT_H_
#define SHILL_NETWORK_NETWORK_CONTEXT_H_

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

 private:
  void GenerateLoggingTag();

  const std::string ifname_;
  std::string service_logging_name_;

  std::string logging_tag_;
};

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_CONTEXT_H_
