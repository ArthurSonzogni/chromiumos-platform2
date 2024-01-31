// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helpers.h"

#include <string>

#include <gtest/gtest.h>

namespace {

int MockResolver(const char* node,
                 const char* service,
                 const struct addrinfo* hints,
                 struct addrinfo** res) {
  if (node == std::string("host4.local")) {
    struct sockaddr_in* ip =
        (struct sockaddr_in*)calloc(1, sizeof(struct sockaddr_in));
    ip->sin_family = AF_INET;
    ip->sin_port = 0;
    ip->sin_addr.s_addr = 0x0100007f;  // 127.0.0.1

    struct addrinfo* result =
        (struct addrinfo*)calloc(1, sizeof(struct addrinfo));
    result->ai_family = AF_INET;
    result->ai_socktype = SOCK_STREAM;
    result->ai_protocol = 0;
    result->ai_addr = (struct sockaddr*)ip;
    result->ai_addrlen = sizeof(struct sockaddr_in);
    *res = result;
    return 0;
  } else if (node == std::string("host6.local")) {
    struct sockaddr_in6* ip =
        (struct sockaddr_in6*)calloc(1, sizeof(struct sockaddr_in6));
    ip->sin6_family = AF_INET6;
    ip->sin6_port = 0;
    ip->sin6_addr.s6_addr[15] = 1;  // ::1

    struct addrinfo* result =
        (struct addrinfo*)calloc(1, sizeof(struct addrinfo));
    result->ai_family = AF_INET6;
    result->ai_socktype = SOCK_STREAM;
    result->ai_protocol = 0;
    result->ai_addr = (struct sockaddr*)ip;
    result->ai_addrlen = sizeof(struct sockaddr_in6);
    *res = result;
    return 0;
  } else {
    *res = nullptr;
    return EAI_FAIL;
  }
}

}  // namespace

TEST(ConvertIppToHttp, InvalidUrl) {
  std::string url = "http:missing//";
  EXPECT_FALSE(ConvertIppToHttp(url));
}

TEST(ConvertIppToHttp, InvalidProtocol) {
  std::string url = "proto://ok";
  EXPECT_FALSE(ConvertIppToHttp(url));
}

TEST(ConvertIppToHttp, ConvertToHttp) {
  std::string url = "ipp://ala.ma.kota/abcd/1234";
  EXPECT_TRUE(ConvertIppToHttp(url));
  EXPECT_EQ(url, "http://ala.ma.kota:631/abcd/1234");
}

TEST(ConvertIppToHttp, ConvertToHttps) {
  std::string url = "ipps://blebleble";
  EXPECT_TRUE(ConvertIppToHttp(url));
  EXPECT_EQ(url, "https://blebleble:443");
}

TEST(ConvertIppToHttp, DoNothing) {
  std::string url = "https://ala.ma.kota:123/abcd?a=1234";
  EXPECT_TRUE(ConvertIppToHttp(url));
  EXPECT_EQ(url, "https://ala.ma.kota:123/abcd?a=1234");
}

TEST(ResolveZeroconfHostname, InvalidUrlMissingProtocol) {
  std::string url = "http:missing//";
  EXPECT_FALSE(ResolveZeroconfHostname(url));
}

TEST(ResolveZeroconfHostname, InvalidUrlMissingPath) {
  std::string url = "http://hostname";
  EXPECT_FALSE(ResolveZeroconfHostname(url));
}

TEST(ResolveZeroconfHostname, NonZeroconfUnchanged) {
  std::string url = "http://hostname/";
  EXPECT_TRUE(ResolveZeroconfHostname(url));
  EXPECT_EQ(url, "http://hostname/");
}

TEST(ResolveZeroconfHostname, ResolverError) {
  std::string url = "http://hostname.local/";
  EXPECT_FALSE(ResolveZeroconfHostname(url, MockResolver));
}

TEST(ResolveZeroconfHostname, ResolveIPv4) {
  std::string url = "http://host4.local/ipp/print";
  EXPECT_TRUE(ResolveZeroconfHostname(url, MockResolver));
  EXPECT_EQ(url, "http://127.0.0.1/ipp/print");
}

TEST(ResolveZeroconfHostname, ResolveIPv6) {
  std::string url = "https://host6.local:631/";
  EXPECT_TRUE(ResolveZeroconfHostname(url, MockResolver));
  EXPECT_EQ(url, "https://[::1]:631/");
}
