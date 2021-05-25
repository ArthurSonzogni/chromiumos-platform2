// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/error/caller_info.h"
#include "libhwsec-foundation/error/error.h"
#include "libhwsec-foundation/error/error_message.h"
#include "libhwsec-foundation/error/testing_helper.h"

#include <sstream>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::hwsec_foundation::error::testing::TestForCreateError;
using ::hwsec_foundation::error::testing::TestForCreateErrorWrap;

namespace hwsec_foundation {
namespace error {

class TestingErrorTest : public ::testing::Test {
 public:
  TestingErrorTest() {}
  ~TestingErrorTest() override = default;

 protected:
  // Implement a test error object.
  class TestingErrorObj : public ErrorBaseObj {
   public:
    explicit TestingErrorObj(int err1, double err2, std::string err3)
        : err1_(err1), err2_(err2), err3_(err3) {}
    virtual ~TestingErrorObj() = default;

    std::string ToReadableString() const {
      std::stringstream ss;
      ss << err1_ << " " << err2_ << " " << err3_;
      return ss.str();
    }

    hwsec_foundation::error::ErrorBase SelfCopy() const {
      return std::make_unique<TestingErrorObj>(err1_, err2_, err3_);
    }

   protected:
    TestingErrorObj(TestingErrorObj&&) = default;

   private:
    const int err1_;
    const double err2_;
    const std::string err3_;
  };
  using TestingError = std::unique_ptr<TestingErrorObj>;
};

TEST_F(TestingErrorTest, PrintErrorMessage) {
  auto err = CreateError<TestingError>(87, 0.1234, "AAAA");
  std::stringstream ss;
  ss << *err;
  EXPECT_EQ("87 0.1234 AAAA", ss.str());
}

TEST_F(TestingErrorTest, WrapErrors) {
  auto err = CreateError<TestingError>(87, 0.1234, "AAAA");
  auto err2 = CreateError<TestingError>(12, 0.56, "BBB");
  err2->Wrap(std::move(err));
  EXPECT_EQ(err, nullptr);
  std::stringstream ss;
  ss << *err2;
  EXPECT_EQ("12 0.56 BBB: 87 0.1234 AAAA", ss.str());
  auto err3 = CreateError<TestingError>(555, 9487, "CCC");
  err3->Wrap(std::move(err2));
  EXPECT_EQ(err2, nullptr);
  ss = std::stringstream();
  ss << *err3;
  EXPECT_EQ("555 9487 CCC: 12 0.56 BBB: 87 0.1234 AAAA", ss.str());
}

TEST_F(TestingErrorTest, CreateWrappedError) {
  auto err = CreateError<TestingError>(87, 0.1234, "AAAA");
  auto err2 = CreateErrorWrap<TestingError>(std::move(err), 12, 0.56, "BBB");
  EXPECT_EQ(err, nullptr);
  std::stringstream ss;
  ss << *err2;
  EXPECT_EQ("12 0.56 BBB: 87 0.1234 AAAA", ss.str());
}

TEST_F(TestingErrorTest, UnWrappError) {
  ErrorBase err = CreateError<TestingError>(87, 0.1234, "AAAA");
  ErrorBase err2 =
      CreateErrorWrap<TestingError>(std::move(err), 12, 0.56, "BBB");
  EXPECT_EQ(err, nullptr);
  err = err2->UnWrap();
  std::stringstream ss;
  ss << *err;
  EXPECT_EQ("87 0.1234 AAAA", ss.str());
  ss = std::stringstream();
  ss << *err2;
  EXPECT_EQ("12 0.56 BBB", ss.str());
}

TEST_F(TestingErrorTest, FullCopyError) {
  ErrorBase err = CreateError<TestingError>(87, 0.1234, "AAAA");
  ErrorBase err2 =
      CreateErrorWrap<TestingError>(std::move(err), 12, 0.56, "BBB");
  ErrorBase err3 = err2->FullCopy();
  std::stringstream ss;
  ss << *err2;
  EXPECT_EQ("12 0.56 BBB: 87 0.1234 AAAA", ss.str());
  ss = std::stringstream();
  ss << *err3;
  EXPECT_EQ("12 0.56 BBB: 87 0.1234 AAAA", ss.str());
  err2->UnWrap();
  ss = std::stringstream();
  ss << *err3;
  EXPECT_EQ("12 0.56 BBB: 87 0.1234 AAAA", ss.str());
  err2.reset();
  ss = std::stringstream();
  ss << *err3;
  EXPECT_EQ("12 0.56 BBB: 87 0.1234 AAAA", ss.str());
}

TEST_F(TestingErrorTest, TestForCreateError) {
  EXPECT_FALSE((TestForCreateError<ErrorBase>::Check::value));
  EXPECT_FALSE((TestForCreateError<ErrorBase, int>::Check::value));
  EXPECT_FALSE((TestForCreateError<ErrorBase, int, double>::Check::value));
  EXPECT_FALSE(
      (TestForCreateError<ErrorBase, int, double, std::string>::Check::value));
  EXPECT_FALSE(
      (TestForCreateError<ErrorBase, int, int, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateError<ErrorBase, const int&, const int&,
                                   std::string>::Check::value));
  EXPECT_FALSE((TestForCreateError<ErrorBase, double, int&,
                                   const std::string&>::Check::value));
  EXPECT_FALSE(
      (TestForCreateError<ErrorBase, int, int, std::string&&>::Check::value));
  EXPECT_FALSE((TestForCreateError<ErrorBase, std::string, std::string,
                                   std::string>::Check::value));
  EXPECT_FALSE((TestForCreateError<ErrorBase, bool, bool, bool>::Check::value));
  EXPECT_FALSE((TestForCreateError<ErrorBase, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateError<ErrorBase, char[]>::Check::value));
  EXPECT_FALSE(
      (TestForCreateError<ErrorBase, int, double, char[]>::Check::value));

  EXPECT_FALSE((TestForCreateError<TestingError>::Check::value));
  EXPECT_FALSE((TestForCreateError<TestingError, int>::Check::value));
  EXPECT_FALSE((TestForCreateError<TestingError, int, double>::Check::value));
  EXPECT_TRUE((TestForCreateError<TestingError, int, double,
                                  std::string>::Check::value));
  EXPECT_TRUE(
      (TestForCreateError<TestingError, int, int, std::string>::Check::value));
  EXPECT_TRUE((TestForCreateError<TestingError, const int&, const int&,
                                  std::string>::Check::value));
  EXPECT_TRUE((TestForCreateError<TestingError, double, int&,
                                  const std::string&>::Check::value));
  EXPECT_TRUE((
      TestForCreateError<TestingError, int, int, std::string&&>::Check::value));
  EXPECT_FALSE((TestForCreateError<TestingError, std::string, std::string,
                                   std::string>::Check::value));
  EXPECT_FALSE(
      (TestForCreateError<TestingError, bool, bool, bool>::Check::value));
  EXPECT_FALSE((TestForCreateError<TestingError, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateError<TestingError, char[]>::Check::value));
  EXPECT_TRUE(
      (TestForCreateError<TestingError, int, double, char[]>::Check::value));

  EXPECT_FALSE((TestForCreateError<Error>::Check::value));
  EXPECT_TRUE((TestForCreateError<Error, std::string>::Check::value));
  EXPECT_TRUE((TestForCreateError<Error, std::string&&>::Check::value));
  EXPECT_TRUE((TestForCreateError<Error, const std::string&>::Check::value));
  EXPECT_TRUE((TestForCreateError<Error, std::string&>::Check::value));
  EXPECT_TRUE((TestForCreateError<Error, char[]>::Check::value));
  EXPECT_TRUE((TestForCreateError<Error, char*>::Check::value));
  EXPECT_FALSE((TestForCreateError<Error, char>::Check::value));
  EXPECT_FALSE((TestForCreateError<Error, int>::Check::value));
  EXPECT_FALSE(
      (TestForCreateError<Error, std::string, std::string>::Check::value));

  using StringCallerError = CallerInfoError<Error>;
  EXPECT_FALSE((TestForCreateError<StringCallerError>::Check::value));
  EXPECT_FALSE(
      (TestForCreateError<StringCallerError, std::string>::Check::value));
  EXPECT_FALSE((TestForCreateError<StringCallerError, const char[],
                                   const char[], int>::Check::value));
  EXPECT_TRUE((TestForCreateError<StringCallerError, const char[], const char[],
                                  int, std::string>::Check::value));
}

TEST_F(TestingErrorTest, TestForCreateErrorWrap) {
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TestingError, ErrorBase, int>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TestingError, ErrorBase, int,
                                       double>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TestingError, ErrorBase, int, double,
                                      std::string>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TestingError, ErrorBase, int, int,
                                      std::string>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TestingError, ErrorBase, const int&,
                                      const int&, std::string>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TestingError, ErrorBase, double, int&,
                                      const std::string&>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TestingError, ErrorBase, int, int,
                                      std::string&&>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TestingError, ErrorBase, std::string, std::string,
                              std::string>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TestingError, ErrorBase, bool, bool,
                                       bool>::Check::value));
  EXPECT_FALSE((TestForCreateErrorWrap<TestingError, ErrorBase,
                                       std::string>::Check::value));
  EXPECT_FALSE(
      (TestForCreateErrorWrap<TestingError, ErrorBase, char[]>::Check::value));
  EXPECT_TRUE((TestForCreateErrorWrap<TestingError, ErrorBase, int, double,
                                      char[]>::Check::value));
}

TEST_F(TestingErrorTest, Error) {
  auto err = CreateError<Error>("Magic");
  auto err2 = CreateErrorWrap<TestingError>(std::move(err), 12, 0.56, "BBB");
  std::stringstream ss;
  ss << *err2;
  EXPECT_EQ("12 0.56 BBB: Magic", ss.str());
}

TEST_F(TestingErrorTest, AsIsCast) {
  auto err = CreateError<Error>("Magic");
  auto err2 = CreateErrorWrap<TestingError>(std::move(err), 12, 0.56, "BBB");
  ErrorBase err3 = CreateErrorWrap<TestingError>(std::move(err2), 1, 0, "XD");
  EXPECT_TRUE(err3->Is<TestingError>());
  EXPECT_FALSE(err3->Is<Error>());
  auto err4 = err3->As<Error>();
  EXPECT_TRUE(err4->Is<Error>());
  EXPECT_FALSE(err4->Is<TestingError>());
  EXPECT_EQ("Magic", err4->ToFullReadableString());
  auto err5 = err3->Cast<Error>();
  EXPECT_EQ(nullptr, err5);
  auto err6 = err3->Cast<TestingError>();
  EXPECT_NE(nullptr, err6);
  EXPECT_EQ("1 0 XD", err6->ToReadableString());
  EXPECT_EQ("1 0 XD: 12 0.56 BBB: Magic", err6->ToFullReadableString());
}

TEST_F(TestingErrorTest, CallerInfoError) {
  using StringCallerError = CallerInfoError<Error>;
  auto err = CreateError<StringCallerError>(CALLER_INFO_ARGS, "Magic");
  EXPECT_NE(err->ToFullReadableString().find("Magic"), std::string::npos);
  auto err2 = err->FullCopy();
}

}  // namespace error
}  // namespace hwsec_foundation
