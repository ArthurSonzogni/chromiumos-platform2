// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helpers.h"

#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <error.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <base/strings/strcat.h>

bool ConvertIppToHttp(std::string& url) {
  auto pos = url.find("://");
  if (pos == std::string::npos) {
    std::cerr << "Incorrect URL: " << url << ".\n";
    std::cerr << "You have to set url parameter, e.g.:";
    std::cerr << " --url=ipp://10.11.12.13/ipp/print." << std::endl;
    return false;
  }
  const auto protocol = url.substr(0, pos);
  if (protocol == "http" || protocol == "https") {
    return true;
  }
  std::string default_port;
  if (protocol == "ipp") {
    default_port = "631";
  } else if (protocol == "ipps") {
    default_port = "443";
  } else {
    std::cerr << "Incorrect URL protocol: " << protocol << ".\n";
    std::cerr << "Supported protocols: http, https, ipp, ipps." << std::endl;
    return false;
  }
  url = "htt" + url.substr(2);
  pos += 4;
  pos = url.find_first_of(":/?#", pos);
  if (pos == std::string::npos) {
    url += ":" + default_port;
  } else if (url[pos] != ':') {
    url = url.substr(0, pos) + ":" + default_port + url.substr(pos);
  }
  return true;
}

bool ResolveZeroconfHostname(std::string& url, ResolveFunc resolver) {
  if (!resolver) {
    resolver = &getaddrinfo;
  }

  auto host_start = url.find("://");
  if (host_start == std::string::npos || host_start < 3) {
    std::cerr << "URL missing protocol: " << url << ".\n";
    return false;
  }
  host_start += 3;

  auto host_end = url.find_first_of(":/", host_start);
  if (host_end == std::string::npos) {
    std::cerr << "URL missing end of hostname: " << url << ".\n";
    return false;
  }

  std::string hostname = url.substr(host_start, host_end - host_start);
  if (!hostname.ends_with(".local")) {
    return true;
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  struct addrinfo* res = nullptr;
  int err = resolver(hostname.c_str(), NULL, &hints, &res);
  if (err != 0) {
    std::cerr << "Failed to look up hostname " << hostname << ": "
              << gai_strerror(err);
    return false;
  }

  std::string new_host;
  switch (res->ai_family) {
    case AF_INET: {
      struct sockaddr_in* ip4 = (struct sockaddr_in*)res->ai_addr;
      char ip4_str[INET_ADDRSTRLEN];
      if (!inet_ntop(res->ai_family, &(ip4->sin_addr), ip4_str,
                     INET_ADDRSTRLEN)) {
        std::cerr << "Failed to convert address to text: " << strerror(errno);
        return false;
      }
      new_host = ip4_str;
      freeaddrinfo(res);
      break;
    }
    case AF_INET6: {
      struct sockaddr_in6* ip6 = (struct sockaddr_in6*)res->ai_addr;
      char ip6_str[INET6_ADDRSTRLEN];
      if (!inet_ntop(res->ai_family, &(ip6->sin6_addr), ip6_str,
                     INET6_ADDRSTRLEN)) {
        std::cerr << "Failed to convert address to text: " << strerror(errno);
        return false;
      }
      new_host = base::StrCat({"[", ip6_str, "]"});
      freeaddrinfo(res);
      break;
    }
    default:
      std::cerr << "Unknown address family " << res->ai_family;
      freeaddrinfo(res);
      return false;
  }

  url.replace(url.begin() + host_start, url.begin() + host_end, new_host);
  return true;
}
