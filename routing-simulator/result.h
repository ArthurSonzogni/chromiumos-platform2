// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_RESULT_H_
#define ROUTING_SIMULATOR_RESULT_H_

#include <ostream>

namespace routing_simulator {

// The base class represents the result of one stage in routing, e.g., routing
// decision or the processing in a iptables builtin chain.
class Result {
 public:
  virtual ~Result() = default;

  // Outputs the result of a packet routing.
  virtual void Output(std::ostream& std_output) const = 0;

 protected:
  Result() = default;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_RESULT_H_
