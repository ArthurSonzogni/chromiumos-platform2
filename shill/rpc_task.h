// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_RPC_TASK_
#define SHILL_RPC_TASK_

#include <map>
#include <string>
#include <vector>

#include <base/basictypes.h>
#include <base/memory/scoped_ptr.h>

namespace shill {

// Declared in the header to avoid linking unused code into shims.
static const char kRPCTaskServiceVariable[] = "SHILL_TASK_SERVICE";
static const char kRPCTaskPathVariable[] = "SHILL_TASK_PATH";

class ControlInterface;
class RPCTaskAdaptorInterface;

// TODO(petkov): Switch from delegate interface to registered callbacks
// (crbug.com/212273).
class RPCTaskDelegate {
 public:
  virtual ~RPCTaskDelegate() {}

  virtual void GetLogin(std::string *user, std::string *password) = 0;
  virtual void Notify(const std::string &reason,
                      const std::map<std::string, std::string> &dict) = 0;
};

// RPC tasks are currently used by VPN drivers for communication with external
// VPN processes. The RPC task should be owned by a single owner -- its
// RPCTaskDelegate -- so no need to be reference counted.
class RPCTask {
 public:
  // A constructor for the RPCTask object.
  RPCTask(ControlInterface *control_interface, RPCTaskDelegate *delegate);
  virtual ~RPCTask();

  virtual void GetLogin(std::string *user, std::string *password) const;
  virtual void Notify(const std::string &reason,
                      const std::map<std::string, std::string> &dict);

  // Returns a string that is guaranteed to uniquely identify this RPCTask
  // instance.
  const std::string &UniqueName() const { return unique_name_; }

  // Generates environment variable strings for a child process to
  // communicate back to us over RPC.
  virtual std::vector<std::string> GetEnvironment() const;
  std::string GetRpcIdentifier() const;
  std::string GetRpcConnectionIdentifier() const;

 private:
  RPCTaskDelegate *delegate_;
  static unsigned int serial_number_;
  std::string unique_name_;  // MUST be unique amongst RPC task instances
  scoped_ptr<RPCTaskAdaptorInterface> adaptor_;

  DISALLOW_COPY_AND_ASSIGN(RPCTask);
};

}  // namespace shill

#endif  // SHILL_RPC_TASK_
