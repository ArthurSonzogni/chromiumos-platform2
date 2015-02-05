// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_LIBWEBSERV_CONNECTION_H_
#define WEBSERVER_LIBWEBSERV_CONNECTION_H_

#include <map>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_ptr.h>
#include <chromeos/errors/error.h>
#include <libwebserv/export.h>

struct MHD_Connection;
struct MHD_PostProcessor;

namespace base {
class TaskRunner;
}  // namespace base

namespace libwebserv {

class Request;
class RequestHandlerInterface;
class Response;
class Server;

// A wrapper class around low-level HTTP connection.
class LIBWEBSERV_EXPORT Connection final : public base::RefCounted<Connection> {
 public:
  ~Connection();

  // Factory creator method. Creates an instance of the connection and
  // initializes some complex data members. This is safer and easier to
  // report possible failures than reply on just the constructor.
  static scoped_refptr<Connection> Create(Server* server,
                                          const std::string& url,
                                          const std::string& method,
                                          MHD_Connection* connection,
                                          RequestHandlerInterface* handler);

 private:
  LIBWEBSERV_PRIVATE Connection(
      const scoped_refptr<base::TaskRunner>& task_runner,
      MHD_Connection* connection,
      RequestHandlerInterface* handler);

  // Helper callback methods used by Server's ConnectionHandler to transfer
  // request headers and data to the Connection's Request object.
  LIBWEBSERV_PRIVATE bool BeginRequestData();
  LIBWEBSERV_PRIVATE bool AddRequestData(const void* data, size_t size);
  LIBWEBSERV_PRIVATE void EndRequestData();

  // Callback for libmicrohttpd's PostProcessor.
  LIBWEBSERV_PRIVATE bool ProcessPostData(const char* key,
                                          const char* filename,
                                          const char* content_type,
                                          const char* transfer_encoding,
                                          const char* data,
                                          uint64_t off,
                                          size_t size);

  scoped_refptr<base::TaskRunner> task_runner_;
  MHD_Connection* raw_connection_{nullptr};
  RequestHandlerInterface* handler_{nullptr};
  MHD_PostProcessor* post_processor_{nullptr};
  scoped_ptr<Request> request_;
  scoped_ptr<Response> response_;

  enum class State { kIdle, kRequestSent, kResponseReceived, kDone };
  State state_{State::kIdle};
  int response_status_code_{0};
  std::vector<uint8_t> response_data_;
  std::multimap<std::string, std::string> response_headers_;

  friend class ConnectionHelper;
  friend class Request;
  friend class Response;
  friend class ServerHelper;
  DISALLOW_COPY_AND_ASSIGN(Connection);
};

}  // namespace libwebserv

#endif  // WEBSERVER_LIBWEBSERV_CONNECTION_H_
