// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dns_client.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <memory>
#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "shill/logging.h"
#include "shill/shill_ares.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDNS;
static std::string ObjectID(const DnsClient* d) {
  return d->interface_name();
}
}  // namespace Logging

namespace {

std::vector<std::string> FilterEmptyIPs(
    const std::vector<std::string>& dns_list) {
  std::vector<std::string> results;
  for (const auto& ip : dns_list) {
    if (!ip.empty()) {
      results.push_back(ip);
    }
  }
  return results;
}

}  // namespace

const char DnsClient::kErrorNoData[] = "The query response contains no answers";
const char DnsClient::kErrorFormErr[] = "The server says the query is bad";
const char DnsClient::kErrorServerFail[] = "The server says it had a failure";
const char DnsClient::kErrorNotFound[] = "The queried-for domain was not found";
const char DnsClient::kErrorNotImp[] = "The server doesn't implement operation";
const char DnsClient::kErrorRefused[] = "The server replied, refused the query";
const char DnsClient::kErrorBadQuery[] = "Locally we could not format a query";
const char DnsClient::kErrorNetRefused[] = "The network connection was refused";
const char DnsClient::kErrorTimedOut[] = "The network connection was timed out";
const char DnsClient::kErrorUnknown[] = "DNS Resolver unknown internal error";

// Private to the implementation of resolver so callers don't include ares.h
struct DnsClientState {
  ares_channel channel = nullptr;
  std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      read_handlers;
  std::vector<std::unique_ptr<base::FileDescriptorWatcher::Controller>>
      write_handlers;
  base::TimeTicks start_time;
};

DnsClient::DnsClient(net_base::IPFamily family,
                     const std::string& interface_name,
                     base::TimeDelta timeout,
                     EventDispatcher* dispatcher,
                     const ClientCallback& callback)
    : address_(family),
      interface_name_(interface_name),
      dispatcher_(dispatcher),
      callback_(callback),
      timeout_(timeout),
      running_(false),
      weak_ptr_factory_(this),
      ares_(Ares::GetInstance()) {}

DnsClient::~DnsClient() {
  Stop();
}

bool DnsClient::Start(const std::vector<std::string>& dns_list,
                      const std::string& hostname,
                      Error* error) {
  if (running_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          "Only one DNS request is allowed at a time");
    return false;
  }

  std::vector<std::string> filtered_dns_list = FilterEmptyIPs(dns_list);

  if (!resolver_state_) {
    struct ares_options options;
    memset(&options, 0, sizeof(options));

    if (filtered_dns_list.empty()) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            "No valid DNS server addresses");
      return false;
    }

    options.timeout = timeout_.InMilliseconds() / filtered_dns_list.size();

    resolver_state_ = std::make_unique<DnsClientState>();
    int status = ares_->InitOptions(&resolver_state_->channel, &options,
                                    ARES_OPT_TIMEOUTMS);
    if (status != ARES_SUCCESS) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                            "ARES initialization returns error code: " +
                                base::NumberToString(status));
      resolver_state_ = nullptr;
      return false;
    }

    // Format DNS server addresses string as "host:port[,host:port...]" to be
    // used in call to ares_set_servers_csv for setting DNS server addresses.
    //
    // Alternatively, we can use ares_set_servers instead, where we would
    // explicitly construct a link list of ares_addr_node.
    const auto server_addresses = base::JoinString(filtered_dns_list, ",");
    status = ares_->SetServersCsv(resolver_state_->channel,
                                  server_addresses.c_str());
    if (status != ARES_SUCCESS) {
      Error::PopulateAndLog(
          FROM_HERE, error, Error::kOperationFailed,
          "ARES set DNS servers error code: " + base::NumberToString(status));
      resolver_state_ = nullptr;
      return false;
    }

    ares_->SetLocalDev(resolver_state_->channel, interface_name_.c_str());
  }

  running_ = true;
  resolver_state_->start_time = base::TimeTicks::Now();
  ares_->GetHostByName(resolver_state_->channel, hostname.c_str(),
                       net_base::ToSAFamily(address_.GetFamily()),
                       ReceiveDnsReplyCB, this);

  if (!RefreshHandles()) {
    LOG(ERROR) << interface_name_ << ": Impossibly short timeout.";
    *error = error_;
    Stop();
    return false;
  }

  return true;
}

void DnsClient::Stop() {
  SLOG(this, 3) << "In " << __func__;
  if (!resolver_state_) {
    return;
  }

  running_ = false;
  // Eplicitly stop all IO handlers to help isolate b/162714491.
  StopReadHandlers();
  StopWriteHandlers();
  weak_ptr_factory_.InvalidateWeakPtrs();
  error_.Reset();
  address_ = net_base::IPAddress(address_.GetFamily());
  ares_->Destroy(resolver_state_->channel);
  resolver_state_ = nullptr;
}

bool DnsClient::IsActive() const {
  return running_;
}

// We delay our call to completion so that we exit all
// base::FileDescriptorWatchers, and can clean up all of our local state before
// calling the callback, or during the process of the execution of the callee
// (which is free to call our destructor safely).
void DnsClient::HandleCompletion() {
  SLOG(this, 3) << "In " << __func__;

  const Error error(error_);
  const net_base::IPAddress address(address_);
  if (!error.IsSuccess()) {
    // If the DNS request did not succeed, do not trust it for future
    // attempts.
    Stop();
  } else {
    // Prepare our state for the next request without destroying the
    // current ARES state.
    error_.Reset();
    address_ = net_base::IPAddress(address_.GetFamily());
  }

  if (!error.IsSuccess()) {
    callback_.Run(base::unexpected(error));
  } else {
    callback_.Run(address);
  }
}

void DnsClient::HandleDnsRead(int fd) {
  ProcessFd(fd, /*write_fd=*/ARES_SOCKET_BAD);
}

void DnsClient::HandleDnsWrite(int fd) {
  ProcessFd(/*read_fd=*/ARES_SOCKET_BAD, fd);
}

void DnsClient::HandleTimeout() {
  ProcessFd(/*read_fd=*/ARES_SOCKET_BAD, /*write_fd=*/ARES_SOCKET_BAD);
}

void DnsClient::ProcessFd(int read_fd, int write_fd) {
  StopReadHandlers();
  StopWriteHandlers();
  ares_->ProcessFd(resolver_state_->channel, read_fd, write_fd);
  RefreshHandles();
}

void DnsClient::ReceiveDnsReply(int status, struct hostent* hostent) {
  if (!running_) {
    // We can be called during ARES shutdown -- ignore these events.
    return;
  }
  SLOG(this, 3) << "In " << __func__;
  running_ = false;
  timeout_closure_.Cancel();
  dispatcher_->PostTask(FROM_HERE,
                        base::BindOnce(&DnsClient::HandleCompletion,
                                       weak_ptr_factory_.GetWeakPtr()));

  if (status == ARES_SUCCESS && hostent != nullptr &&
      hostent->h_addrtype == net_base::ToSAFamily(address_.GetFamily()) &&
      static_cast<size_t>(hostent->h_length) == address_.GetAddressLength() &&
      hostent->h_addr_list != nullptr && hostent->h_addr_list[0] != nullptr) {
    address_ = *net_base::IPAddress::CreateFromBytes(
        {reinterpret_cast<unsigned char*>(hostent->h_addr_list[0]),
         address_.GetAddressLength()});
  } else {
    switch (status) {
      case ARES_ENODATA:
        error_.Populate(Error::kOperationFailed, kErrorNoData);
        break;
      case ARES_EFORMERR:
        error_.Populate(Error::kOperationFailed, kErrorFormErr);
        break;
      case ARES_ESERVFAIL:
        error_.Populate(Error::kOperationFailed, kErrorServerFail);
        break;
      case ARES_ENOTFOUND:
        error_.Populate(Error::kOperationFailed, kErrorNotFound);
        break;
      case ARES_ENOTIMP:
        error_.Populate(Error::kOperationFailed, kErrorNotImp);
        break;
      case ARES_EREFUSED:
        error_.Populate(Error::kOperationFailed, kErrorRefused);
        break;
      case ARES_EBADQUERY:
      case ARES_EBADNAME:
      case ARES_EBADFAMILY:
      case ARES_EBADRESP:
        error_.Populate(Error::kOperationFailed, kErrorBadQuery);
        break;
      case ARES_ECONNREFUSED:
        error_.Populate(Error::kOperationFailed, kErrorNetRefused);
        break;
      case ARES_ETIMEOUT:
        error_.Populate(Error::kOperationTimeout, kErrorTimedOut);
        break;
      default:
        error_.Populate(Error::kOperationFailed, kErrorUnknown);
        if (status == ARES_SUCCESS) {
          LOG(ERROR) << interface_name_
                     << ": ARES returned success but hostent was invalid!";
        } else {
          LOG(ERROR) << interface_name_
                     << ": ARES returned unhandled error status " << status;
        }
        break;
    }
  }
}

void DnsClient::ReceiveDnsReplyCB(void* arg,
                                  int status,
                                  int /*timeouts*/,
                                  struct hostent* hostent) {
  DnsClient* res = static_cast<DnsClient*>(arg);
  res->ReceiveDnsReply(status, hostent);
}

bool DnsClient::RefreshHandles() {
  ares_socket_t sockets[ARES_GETSOCK_MAXNUM];
  int action_bits =
      ares_->GetSock(resolver_state_->channel, sockets, ARES_GETSOCK_MAXNUM);

  for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    if (ARES_GETSOCK_READABLE(action_bits, i)) {
      resolver_state_->read_handlers.push_back(
          base::FileDescriptorWatcher::WatchReadable(
              sockets[i],
              base::BindRepeating(&DnsClient::HandleDnsRead,
                                  base::Unretained(this), sockets[i])));
    }
    if (ARES_GETSOCK_WRITABLE(action_bits, i)) {
      resolver_state_->write_handlers.push_back(
          base::FileDescriptorWatcher::WatchWritable(
              sockets[i],
              base::BindRepeating(&DnsClient::HandleDnsWrite,
                                  base::Unretained(this), sockets[i])));
    }
  }

  if (!running_) {
    // We are here just to clean up socket handles, and the ARES state was
    // cleaned up during the last call to ares_->ProcessFd().
    return false;
  }

  // Schedule timer event for the earlier of our timeout or one requested by
  // the resolver library.
  const base::TimeDelta elapsed_time =
      base::TimeTicks::Now() - resolver_state_->start_time;
  timeout_closure_.Cancel();

  if (elapsed_time >= timeout_) {
    // There are 3 cases of interest:
    //  - If we got here from Start(), when we return, Stop() will be
    //    called, so our cleanup task will not run, so we will not have the
    //    side-effect of both invoking the callback and returning False
    //    in Start().
    //  - If we got here from the tail of an IO event, we can't call
    //    Stop() since that will blow away the base::FileDescriptorWatcher we
    //    are running in.  We will perform the cleanup in the posted task below.
    //  - If we got here from a timeout handler, we will perform cleanup
    //    in the posted task.
    running_ = false;
    error_.Populate(Error::kOperationTimeout, kErrorTimedOut);
    dispatcher_->PostTask(FROM_HERE,
                          base::BindOnce(&DnsClient::HandleCompletion,
                                         weak_ptr_factory_.GetWeakPtr()));
    return false;
  } else {
    const base::TimeDelta max = timeout_ - elapsed_time;
    struct timeval max_tv = {
        .tv_sec = static_cast<time_t>(max.InSeconds()),
        .tv_usec = static_cast<suseconds_t>(
            (max - base::Seconds(max.InSeconds())).InMicroseconds()),
    };

    struct timeval ret_tv;
    struct timeval* tv =
        ares_->Timeout(resolver_state_->channel, &max_tv, &ret_tv);
    timeout_closure_.Reset(base::BindOnce(&DnsClient::HandleTimeout,
                                          weak_ptr_factory_.GetWeakPtr()));
    dispatcher_->PostDelayedTask(
        FROM_HERE, timeout_closure_.callback(),
        base::Seconds(tv->tv_sec) + base::Microseconds(tv->tv_usec));
  }

  return true;
}

void DnsClient::StopReadHandlers() {
  resolver_state_->read_handlers.clear();
}

void DnsClient::StopWriteHandlers() {
  resolver_state_->write_handlers.clear();
}

}  // namespace shill
