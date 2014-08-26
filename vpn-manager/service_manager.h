// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VPN_MANAGER_SERVICE_MANAGER_H_
#define VPN_MANAGER_SERVICE_MANAGER_H_

#include <netinet/in.h>

#include <string>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "vpn-manager/service_error.h"

namespace base {
class ScopedTempDir;
}

namespace vpn_manager {

// Generic code to manage setting up and stopping a set of layered
// tunnel services.  This object contains the code to manage a single
// layer.  Services are meant to be started from outermost to innermost.
// Services are meant to be stopped from the innermost out.  To
// stop the entire set of services, call Stop on the innermost.
// Services go from not-yet-started to started to in_running to
// was_stopped.
class ServiceManager {
 public:
  explicit ServiceManager(const std::string& service_name);
  virtual ~ServiceManager() = default;

  // Initialize directories used by services.  |scoped_temp_dir| will
  // be set to manage an appropriate temp directory.  This function
  // uses a reference to |scoped_temp_dir| and so its lifetime must be
  // equal to that of all objects derived from ServiceManager.
  static void InitializeDirectories(base::ScopedTempDir* scoped_temp_path);

  // Call to initiate this service.  If starting fails immediately this
  // returns false.  If something fails after this returns, OnStopped
  // will be called.  Code outside of the service manager stack
  // must only call Start on the outermost function.
  virtual bool Start() = 0;

  // Callback when this service has successfully started.
  virtual void OnStarted();

  // Call to stop this service.  Must not be called on a separate
  // thread from Start().  Code outside of the service manager stack
  // must only call Stop on the innermost service.  It is ok to
  // stop an already stopped service.
  virtual void Stop() = 0;

  // Returns the maximum amount of time to wait before this call should be
  // called again in milliseconds.
  virtual int Poll() = 0;

  // Process output from child process.
  virtual void ProcessOutput() = 0;

  // Returns if |pid| is a child process of this service.
  virtual bool IsChild(pid_t pid) = 0;

  // Callback when this service has stopped after having started
  // successfully.  |was_error| indicates if an error occurred.
  virtual void OnStopped(bool was_error);

  // Callback when WriteFdToSyslog() outputs a line to syslog. The default
  // implementation is a no-op. A derived class can override this method to
  // extract information such as errors from the log messages.
  virtual void OnSyslogOutput(const std::string& prefix,
                              const std::string& line);

  // Registers the given |error| if |error| is more specific than the
  // currently registered |error_|.
  void RegisterError(ServiceError error);

  // Returns the most specific error that has been registered by this
  // service manager. If this service manager has an inner service,
  // this method always returns the error registered by an inner service.
  ServiceError GetError() const;

  // Queries if this service is currently running.
  bool is_running() const {
    return is_running_;
  }

  // Queries if this service was once running and is now stopped.
  bool was_stopped() const {
    return was_stopped_;
  }

  // Accessor for debug_ field.
  bool debug() const {
    return debug_;
  }

  // Setter for debug_ field.
  void set_debug(bool debug) {
    debug_ = debug;
  }

  // Set up layering between two service managers |outer| and |inner|.
  // This function may be called multiple times to chain servics together,
  // for instance:
  //   ServiceManager::SetLayerOrder(&turkey, &duck);
  //   ServiceManager::SetLayerOrder(&duck, &chicken);
  static void SetLayerOrder(ServiceManager* outer,
                            ServiceManager* inner) {
    outer->inner_service_ = inner;
    inner->outer_service_ = outer;
  }

  const std::string& service_name() {
    return service_name_;
  }

  // Repeat data from the given |fd| which is assumed to be ready and
  // send it out to syslog, placing |prefix| before each line of
  // output.  Function will block reading fd so it assumes fd is
  // ready.  It will also only read a fixed size per call.  Any
  // partial line read is stored into |partial_line|.  This variable
  // is used on each call to prefix any newly read data.
  void WriteFdToSyslog(int fd, const std::string& prefix,
                       std::string* partial_line);

  // Resolve given |name| into an IP address |socket_address| or return
  // false if an error occurs.
  static bool ResolveNameToSockAddr(const std::string& name,
                                    struct sockaddr* socket_address);

  // Convert given |address| into a string representation |address_text|.
  static bool ConvertSockAddrToIPString(const struct sockaddr& address,
                                        std::string* address_text);

  // Convert given |address_text| in string representaton to |address|
  // or return false if unable.
  static bool ConvertIPStringToSockAddr(const std::string& address_text,
                                        struct sockaddr* address);

  // Find the |local_address| when making a connection to the given
  // |remote_address| or return false on error.
  static bool GetLocalAddressFromRemote(const struct sockaddr& remote_address,
                                        struct sockaddr* local_address);

 protected:
  friend class IpsecManagerTest;
  friend class L2tpManagerTest;
  friend class ServiceManagerTest;
  FRIEND_TEST(L2tpManagerTest, PollNothingIfRunning);
  FRIEND_TEST(IpsecManagerTest, PollNothingIfRunning);
  FRIEND_TEST(ServiceManagerTest, GetRootPersistentPath);
  FRIEND_TEST(ServiceManagerTest, InitializeDirectories);
  FRIEND_TEST(ServiceManagerTest, OnStoppedFromFailure);
  FRIEND_TEST(ServiceManagerTest, OnStoppedFromSuccess);

  static base::FilePath GetRootPersistentPath();

  ServiceManager* inner_service() { return inner_service_; }

  ServiceManager* outer_service() { return outer_service_; }

  static const base::FilePath* temp_path() { return temp_path_; }
  static const char* temp_base_path() { return temp_base_path_; }

 private:
  // The default value for temp_base_path_.
  static const char kDefaultTempBasePath[];

  // Path name under |temp_base_path_| that the root filesystem links to.
  static const char kPersistentSubdir[];

  // Indicates if this service is currently running.
  bool is_running_;

  // Indicates if this service was running and is now stopped.
  bool was_stopped_;

  // Indicates if extra debugging information should be emitted.
  bool debug_;

  // Pointer to the next layer or NULL if innermost.
  ServiceManager* inner_service_;

  // Pointer to the outer layer or NULL if outermost.
  ServiceManager* outer_service_;

  // Name of this service.
  std::string service_name_;

  // Most specific error that has been registerred by this service manager.
  ServiceError error_;

  // Path to temporary directory on cryptohome.
  static const base::FilePath* temp_path_;

  // Path to base directory of temporary directory on cryptohome.
  static const char* temp_base_path_;
};

}  // namespace vpn_manager

#endif  // VPN_MANAGER_SERVICE_MANAGER_H_
