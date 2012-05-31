// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dns_client.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <map>
#include <set>
#include <string>
#include <tr1/memory>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/stl_util.h>
#include <base/string_number_conversions.h>

#include "shill/scope_logger.h"
#include "shill/shill_ares.h"
#include "shill/shill_time.h"

using base::Bind;
using base::Unretained;
using std::map;
using std::set;
using std::string;
using std::vector;

namespace shill {

const int DNSClient::kDefaultTimeoutMS = 2000;
const char DNSClient::kErrorNoData[] = "The query response contains no answers";
const char DNSClient::kErrorFormErr[] = "The server says the query is bad";
const char DNSClient::kErrorServerFail[] = "The server says it had a failure";
const char DNSClient::kErrorNotFound[] = "The queried-for domain was not found";
const char DNSClient::kErrorNotImp[] = "The server doesn't implement operation";
const char DNSClient::kErrorRefused[] = "The server replied, refused the query";
const char DNSClient::kErrorBadQuery[] = "Locally we could not format a query";
const char DNSClient::kErrorNetRefused[] = "The network connection was refused";
const char DNSClient::kErrorTimedOut[] = "The network connection was timed out";
const char DNSClient::kErrorUnknown[] = "DNS Resolver unknown internal error";

// Private to the implementation of resolver so callers don't include ares.h
struct DNSClientState {
  DNSClientState() : channel(NULL), start_time((struct timeval){0}) {}

  ares_channel channel;
  map< ares_socket_t, std::tr1::shared_ptr<IOHandler> > read_handlers;
  map< ares_socket_t, std::tr1::shared_ptr<IOHandler> > write_handlers;
  struct timeval start_time;
};

DNSClient::DNSClient(IPAddress::Family family,
                     const string &interface_name,
                     const vector<string> &dns_servers,
                     int timeout_ms,
                     EventDispatcher *dispatcher,
                     const ClientCallback &callback)
    : address_(IPAddress(family)),
      interface_name_(interface_name),
      dns_servers_(dns_servers),
      dispatcher_(dispatcher),
      callback_(callback),
      timeout_ms_(timeout_ms),
      running_(false),
      resolver_state_(NULL),
      weak_ptr_factory_(this),
      ares_(Ares::GetInstance()),
      time_(Time::GetInstance()) {}

DNSClient::~DNSClient() {
  Stop();
}

bool DNSClient::Start(const string &hostname, Error *error) {
  if (running_) {
    Error::PopulateAndLog(error, Error::kInProgress,
                          "Only one DNS request is allowed at a time");
    return false;
  }

  if (!resolver_state_.get()) {
    struct ares_options options;
    memset(&options, 0, sizeof(options));

    vector<struct in_addr> server_addresses;
    for (vector<string>::iterator it = dns_servers_.begin();
         it != dns_servers_.end();
         ++it) {
      struct in_addr addr;
      if (inet_aton(it->c_str(), &addr) != 0) {
        server_addresses.push_back(addr);
      }
    }

    if (server_addresses.empty()) {
      Error::PopulateAndLog(error, Error::kInvalidArguments,
                            "No valid DNS server addresses");
      return false;
    }

    options.servers = server_addresses.data();
    options.nservers = server_addresses.size();
    options.timeout = timeout_ms_;

    resolver_state_.reset(new DNSClientState);
    int status = ares_->InitOptions(&resolver_state_->channel,
                                   &options,
                                   ARES_OPT_SERVERS | ARES_OPT_TIMEOUTMS);
    if (status != ARES_SUCCESS) {
      Error::PopulateAndLog(error, Error::kOperationFailed,
                            "ARES initialization returns error code: " +
                            base::IntToString(status));
      resolver_state_.reset();
      return false;
    }

    ares_->SetLocalDev(resolver_state_->channel, interface_name_.c_str());
  }

  running_ = true;
  time_->GetTimeMonotonic(&resolver_state_->start_time);
  ares_->GetHostByName(resolver_state_->channel, hostname.c_str(),
                       address_.family(), ReceiveDNSReplyCB, this);

  if (!RefreshHandles()) {
    LOG(ERROR) << "Impossibly short timeout.";
    error->CopyFrom(error_);
    Stop();
    return false;
  }

  return true;
}

void DNSClient::Stop() {
  SLOG(DNS, 3) << "In " << __func__;
  if (!resolver_state_.get()) {
    return;
  }

  running_ = false;
  weak_ptr_factory_.InvalidateWeakPtrs();
  error_.Reset();
  address_.SetAddressToDefault();
  ares_->Destroy(resolver_state_->channel);
  resolver_state_.reset();
}

// We delay our call to completion so that we exit all IOHandlers, and
// can clean up all of our local state before calling the callback, or
// during the process of the execution of the callee (which is free to
// call our destructor safely).
void DNSClient::HandleCompletion() {
  SLOG(DNS, 3) << "In " << __func__;
  Error error;
  error.CopyFrom(error_);
  IPAddress address(address_);
  if (!error.IsSuccess()) {
    // If the DNS request did not succeed, do not trust it for future
    // attempts.
    Stop();
  } else {
    // Prepare our state for the next request without destroying the
    // current ARES state.
    error_.Reset();
    address_.SetAddressToDefault();
  }
  callback_.Run(error, address);
}

void DNSClient::HandleDNSRead(int fd) {
  ares_->ProcessFd(resolver_state_->channel, fd, ARES_SOCKET_BAD);
  RefreshHandles();
}

void DNSClient::HandleDNSWrite(int fd) {
  ares_->ProcessFd(resolver_state_->channel, ARES_SOCKET_BAD, fd);
  RefreshHandles();
}

void DNSClient::HandleTimeout() {
  ares_->ProcessFd(resolver_state_->channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
  RefreshHandles();
}

void DNSClient::ReceiveDNSReply(int status, struct hostent *hostent) {
  if (!running_) {
    // We can be called during ARES shutdown -- ignore these events.
    return;
  }
  SLOG(DNS, 3) << "In " << __func__;
  running_ = false;
  timeout_closure_.Cancel();
  dispatcher_->PostTask(Bind(&DNSClient::HandleCompletion,
                             weak_ptr_factory_.GetWeakPtr()));

  if (status == ARES_SUCCESS &&
      hostent != NULL &&
      hostent->h_addrtype == address_.family() &&
      static_cast<size_t>(hostent->h_length) ==
      IPAddress::GetAddressLength(address_.family()) &&
      hostent->h_addr_list != NULL &&
      hostent->h_addr_list[0] != NULL) {
    address_ = IPAddress(address_.family(),
                         ByteString(reinterpret_cast<unsigned char *>(
                             hostent->h_addr_list[0]), hostent->h_length));
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
          LOG(ERROR) << "ARES returned success but hostent was invalid!";
        } else {
          LOG(ERROR) << "ARES returned unhandled error status " << status;
        }
        break;
    }
  }
}

void DNSClient::ReceiveDNSReplyCB(void *arg, int status,
                                  int /*timeouts*/,
                                  struct hostent *hostent) {
  DNSClient *res = static_cast<DNSClient *>(arg);
  res->ReceiveDNSReply(status, hostent);
}

bool DNSClient::RefreshHandles() {
  map< ares_socket_t, std::tr1::shared_ptr<IOHandler> > old_read =
      resolver_state_->read_handlers;
  map< ares_socket_t, std::tr1::shared_ptr<IOHandler> > old_write =
      resolver_state_->write_handlers;

  resolver_state_->read_handlers.clear();
  resolver_state_->write_handlers.clear();

  ares_socket_t sockets[ARES_GETSOCK_MAXNUM];
  int action_bits = ares_->GetSock(resolver_state_->channel, sockets,
                                   ARES_GETSOCK_MAXNUM);

  base::Callback<void(int)> read_callback(
      Bind(&DNSClient::HandleDNSRead, weak_ptr_factory_.GetWeakPtr()));
  base::Callback<void(int)> write_callback(
      Bind(&DNSClient::HandleDNSWrite, weak_ptr_factory_.GetWeakPtr()));
  for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
    if (ARES_GETSOCK_READABLE(action_bits, i)) {
      if (ContainsKey(old_read, sockets[i])) {
        resolver_state_->read_handlers[sockets[i]] = old_read[sockets[i]];
      } else {
        resolver_state_->read_handlers[sockets[i]] =
            std::tr1::shared_ptr<IOHandler> (
                dispatcher_->CreateReadyHandler(sockets[i],
                                                IOHandler::kModeInput,
                                                read_callback));
      }
    }
    if (ARES_GETSOCK_WRITABLE(action_bits, i)) {
      if (ContainsKey(old_write, sockets[i])) {
        resolver_state_->write_handlers[sockets[i]] = old_write[sockets[i]];
      } else {
        resolver_state_->write_handlers[sockets[i]] =
            std::tr1::shared_ptr<IOHandler> (
                dispatcher_->CreateReadyHandler(sockets[i],
                                                IOHandler::kModeOutput,
                                                write_callback));
      }
    }
  }

  if (!running_) {
    // We are here just to clean up socket handles, and the ARES state was
    // cleaned up during the last call to ares_->ProcessFd().
    return false;
  }

  // Schedule timer event for the earlier of our timeout or one requested by
  // the resolver library.
  struct timeval now, elapsed_time, timeout_tv;
  time_->GetTimeMonotonic(&now);
  timersub(&now, &resolver_state_->start_time, &elapsed_time);
  timeout_tv.tv_sec = timeout_ms_ / 1000;
  timeout_tv.tv_usec = (timeout_ms_ % 1000) * 1000;
  timeout_closure_.Cancel();

  if (timercmp(&elapsed_time, &timeout_tv, >=)) {
    // There are 3 cases of interest:
    //  - If we got here from Start(), when we return, Stop() will be
    //    called, so our cleanup task will not run, so we will not have the
    //    side-effect of both invoking the callback and returning False
    //    in Start().
    //  - If we got here from the tail of an IO event, we can't call
    //    Stop() since that will blow away the IOHandler we are running
    //    in.  We will perform the cleanup in the posted task below.
    //  - If we got here from a timeout handler, we will perform cleanup
    //    in the posted task.
    running_ = false;
    error_.Populate(Error::kOperationTimeout, kErrorTimedOut);
    dispatcher_->PostTask(Bind(&DNSClient::HandleCompletion,
                               weak_ptr_factory_.GetWeakPtr()));
    return false;
  } else {
    struct timeval max, ret_tv;
    timersub(&timeout_tv, &elapsed_time, &max);
    struct timeval *tv = ares_->Timeout(resolver_state_->channel,
                                        &max, &ret_tv);
    timeout_closure_.Reset(
        Bind(&DNSClient::HandleTimeout, weak_ptr_factory_.GetWeakPtr()));
    dispatcher_->PostDelayedTask(timeout_closure_.callback(),
                                 tv->tv_sec * 1000 + tv->tv_usec / 1000);
  }

  return true;
}

}  // namespace shill
