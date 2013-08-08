// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef P2P_HTTP_SERVER_CONNECTION_DELEGATE_H__
#define P2P_HTTP_SERVER_CONNECTION_DELEGATE_H__

#include <string>
#include <map>

#include <glib.h>

#include <base/command_line.h>
#include <base/threading/simple_thread.h>

namespace p2p {

namespace http_server {

class Server;

// Class used for handling a single HTTP connection.
class ConnectionDelegate : public base::DelegateSimpleThread::Delegate {
 public:
  // Constructs a new ConnectionDelegate object.
  //
  // Use base::DelegateSimpleThreadPool()'s AddWork() method to start
  // handling the connection.
  ConnectionDelegate(int dirfd,
                     int fd,
                     const std::string& pretty_addr,
                     Server* server,
                     int64_t max_download_rate);

  virtual ~ConnectionDelegate();

  // Overrides DelegateSimpleThread::Delegate
  virtual void Run();

 private:
  // Reads from the socket until a '\n' character is encountered
  // and appends the data to |str| (including the '\n' character)
  // and returns true on success.
  //
  // Fails if the line is longer than kMaxLineLength.
  bool ReadLine(std::string* str);

  // Reads data from the other peer and - if the data is a valid HTTP 1.1
  // request - send a response. As for what is a valid HTTP/1.1 request,
  // see RFC 2616
  //
  //  http://www.ietf.org/rfc/rfc2616.txt
  //
  // For reference, a typical HTTP 1.1 request is shown here
  //
  //  GET / HTTP/1.1\r\n
  //  User-Agent: curl/7.22.0 (x86_64-pc-linux-gnu) libcurl/7.22.0 OpenSSL/1.0.1 zlib/1.2.3.4 libidn/1.23 librtmp/2.3\r\n
  //  Host: localhost:16725\r\n
  //  Accept: */*\r\n
  //  \r\n
  //
  // where \r\n is represents the two byte sequence 0x0d 0x0a.
  void ParseHttpRequest();

  // Handles a HTTP request - called by ParseHttpRequest() if the data
  // read from the other peer is a valid HTTP 1.1 request.
  void ServiceHttpRequest(const std::string& method,
                          const std::string& uri,
                          const std::string& http_version,
                          const std::map<std::string, std::string>& headers);

  // Sends |num_bytes_to_send_bytes| from the file represented by the
  // file descriptor |file_fd|. Returns false if an error occurs while
  // doing this.
  //
  // The implementation will read |kPayloadBufferSize| at once (except
  // for at the end where it is clipped accordingly) and send this
  // to the other end.
  //
  // If read(2) returns EOF, will sleep for one second and then retry.
  // This is for situations where the final file size is known in
  // advance (e.g. read from the user.cros-p2p-filesize xattr) but
  // all content has not yet been downloaded.
  //
  // The implementation will limit download speed by sleeping after
  // sending each chunk, if necessary. See the |max_download_rate_|
  // instance variable.
  bool SendFile(int file_fd, size_t num_bytes_to_send);

  // Sends a HTTP response.
  bool SendResponse(int http_response_code,
                    const std::string& http_response_status,
                    const std::map<std::string, std::string>& headers,
                    const std::string& body);

  // Sends a simple HTTP response.
  bool SendSimpleResponse(int http_response_code,
                          const std::string& http_response_status);

  // Checks if the other end-point is still connected.
  bool IsStillConnected();

  // Generates a HTML document with a directory listing of the
  // .p2p files available.
  std::string GenerateIndexDotHtml();

  // The passed-in file descriptor for the directory we're serving
  // files from.
  int dirfd_;

  // The file descriptor for the socket.
  int fd_;

  // A textual representation (e.g. literal IPv4 or IPv6 address) of the
  // other endpoint of the socket.
  std::string pretty_addr_;

  // A pointer to the Server object to call ConenectionTerminated()
  // on when done serving.
  Server* server_;

  // The maximum allowed download rate (in bytes/second) or 0 if there
  // is no limit.
  int64_t max_download_rate_;

  // Maximum number of headers support in HTTP request.
  static const unsigned int kMaxHeaders = 100;

  // Maximum length of the request line and header lines.
  static const unsigned int kMaxLineLength = 1000;

  // Number of bytes to read at once when processing HTTP headers.
  static const unsigned int kLineBufSize = 256;

  // Number of bytes to read/send at once.
  //
  // TODO(zeuthen): verify this is a good buffer size e.g. that it's a
  //                good tradeoff between wakeups and smooth streaming.
  //                Many factors to consider here. This is tracked in
  //
  //                https://code.google.com/p/chromium/issues/detail?id=246325
  static const unsigned int kPayloadBufferSize = 1048576;

  DISALLOW_COPY_AND_ASSIGN(ConnectionDelegate);
};

}  // namespace http_server

}  // namespace p2p

#endif  // P2P_HTTP_SERVER_CONNECTION_DELEGATE_H__
