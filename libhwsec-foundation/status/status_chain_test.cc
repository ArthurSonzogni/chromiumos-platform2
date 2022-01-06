// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "libhwsec-foundation/status/status_chain.h"
#include "libhwsec-foundation/status/status_chain_macros.h"
#include "libhwsec-foundation/status/status_chain_or.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace hwsec_foundation {
namespace status {

class StatusChainTest : public ::testing::Test {};

namespace {

class FakeBaseError : public Error {
 public:
  using MakeStatusTrait = DefaultMakeStatus<FakeBaseError>;
  using BaseErrorType = FakeBaseError;

  FakeBaseError(std::string message, int val) : Error(message), val_(val) {}
  ~FakeBaseError() override {}

  std::string ToString() const override {
    return "FakeBase: " + Error::ToString();
  }

  int val() const { return val_; }

  void set_val(int val) { val_ = val; }

 protected:
  int val_;
};

class Fake1Error : public FakeBaseError {
 public:
  using MakeStatusTrait = DefaultMakeStatus<Fake1Error>;
  using BaseErrorType = FakeBaseError;

  Fake1Error(std::string message, int val) : FakeBaseError(message, val) {}
  ~Fake1Error() override {}

  std::string ToString() const override {
    return "Fake1: " + Error::ToString();
  }
};

class Fake2Error : public FakeBaseError {
 public:
  struct MakeStatusTrait {
    auto operator()(std::string message, int val) {
      return NewStatus<Fake2Error>(message + ": FROM TRAIT", val);
    }
  };
  using BaseErrorType = FakeBaseError;

  Fake2Error(std::string message, int val) : FakeBaseError(message, val) {}
  ~Fake2Error() override {}

  std::string ToString() const override {
    return "Fake2: " + Error::ToString();
  }
};

class Fake3Error : public FakeBaseError {
 public:
  using MakeStatusTrait = DefaultMakeStatus<Fake3Error>;
  using BaseErrorType = FakeBaseError;

  Fake3Error(std::string message, int val) : FakeBaseError(message, val) {}
  ~Fake3Error() override {}

  void WrapTransform(StatusChain<BaseErrorType>::const_iterator_range range) {
    int new_val = 0;
    for (auto error_obj_ptr : range) {
      if (Error::Is<Fake1Error>(error_obj_ptr)) {
        // shouldn't need to cast since iterator should point to FakeBaseError.
        new_val += error_obj_ptr->val();
      }
    }
    set_val(new_val);
  }

  std::string ToString() const override { return Error::ToString(); }
};

class Fake4Error : public FakeBaseError {
 public:
  using MakeStatusTrait = DefaultMakeStatus<Fake4Error>;
  using BaseErrorType = FakeBaseError;

  Fake4Error(std::string message, int val) : FakeBaseError(message, val) {}
  ~Fake4Error() override {}

  std::string ToString() const override {
    return "Fake4: " + Error::ToString();
  }
};

TEST_F(StatusChainTest, CtorAssign) {
  StatusChain<Fake1Error> ok;
  EXPECT_TRUE(ok.ok());

  StatusChain<Fake1Error> assign_ok;
  assign_ok = std::move(ok);
  EXPECT_TRUE(assign_ok.ok());

  StatusChain<Fake1Error> nullptr_ok = nullptr;
  EXPECT_TRUE(nullptr_ok.ok());

  StatusChain<Fake1Error> assign_nullptr_ok;
  assign_nullptr_ok = std::move(nullptr_ok);
  EXPECT_TRUE(assign_nullptr_ok.ok());

  StatusChain<Fake1Error> ptr(new Fake1Error("e1", 1));
  EXPECT_EQ(ptr->val(), 1);
  ptr.WrapInPlace(MakeStatus<Fake2Error>("e2", 2));
  EXPECT_EQ(ptr.Find<Fake2Error>()->val(), 2);

  StatusChain<Fake1Error> ctor_type_match = std::move(ptr);
  EXPECT_TRUE(ptr.ok());
  EXPECT_EQ(ctor_type_match->val(), 1);
  EXPECT_EQ(ctor_type_match.Find<Fake2Error>()->val(), 2);

  StatusChain<Fake1Error> assign_type_match;
  assign_type_match = std::move(ctor_type_match);
  EXPECT_TRUE(ctor_type_match.ok());
  EXPECT_EQ(assign_type_match->val(), 1);
  EXPECT_EQ(assign_type_match.Find<Fake2Error>()->val(), 2);

  StatusChain<FakeBaseError> ctor_type_mismatch = std::move(assign_type_match);
  EXPECT_TRUE(assign_type_match.ok());
  EXPECT_EQ(ctor_type_mismatch->val(), 1);
  EXPECT_EQ(ctor_type_mismatch.Find<Fake2Error>()->val(), 2);

  StatusChain<FakeBaseError> assign_type_mismatch;
  assign_type_mismatch =
      MakeStatus<Fake4Error>("e3", 3).Wrap(std::move(ctor_type_mismatch));
  EXPECT_TRUE(ctor_type_mismatch.ok());
  EXPECT_EQ(assign_type_mismatch->val(), 3);
  EXPECT_EQ(assign_type_mismatch.Find<Fake1Error>()->val(), 1);
  EXPECT_EQ(assign_type_mismatch.Find<Fake2Error>()->val(), 2);

  StatusChain<FakeBaseError> from_release(assign_type_mismatch.release_stack());
  EXPECT_TRUE(assign_type_mismatch.ok());
  EXPECT_EQ(from_release->val(), 3);
  EXPECT_EQ(from_release.Find<Fake1Error>()->val(), 1);
  EXPECT_EQ(from_release.Find<Fake2Error>()->val(), 2);
}

TEST_F(StatusChainTest, PointerAccessSwapReset) {
  StatusChain<Fake1Error> ptr1;
  EXPECT_EQ(ptr1.get(), StatusChain<Fake1Error>::pointer());

  StatusChain<Fake1Error> ptr2(new Fake1Error("e1", 1));
  ptr2.WrapInPlace(MakeStatus<Fake2Error>("e2", 2));
  EXPECT_EQ(ptr2->val(), 1);
  EXPECT_EQ(ptr2.get()->val(), 1);
  EXPECT_EQ((*ptr2).val(), 1);
  EXPECT_EQ(ptr2.error().val(), 1);
  EXPECT_EQ(ptr2.Find<Fake2Error>()->val(), 2);

  ptr1.reset(new Fake1Error("e3", 3));
  ptr1.WrapInPlace(MakeStatus<Fake2Error>("e4", 4));
  EXPECT_EQ(ptr1->val(), 3);
  EXPECT_EQ(ptr1.get()->val(), 3);
  EXPECT_EQ((*ptr1).val(), 3);
  EXPECT_EQ(ptr1.error().val(), 3);
  EXPECT_EQ(ptr1.Find<Fake2Error>()->val(), 4);

  std::swap(ptr1, ptr2);
  EXPECT_EQ(ptr1->val(), 1);
  EXPECT_EQ(ptr1.get()->val(), 1);
  EXPECT_EQ((*ptr1).val(), 1);
  EXPECT_EQ(ptr1.error().val(), 1);
  EXPECT_EQ(ptr1.Find<Fake2Error>()->val(), 2);

  EXPECT_EQ(ptr2->val(), 3);
  EXPECT_EQ(ptr2.get()->val(), 3);
  EXPECT_EQ((*ptr2).val(), 3);
  EXPECT_EQ(ptr2.error().val(), 3);
  EXPECT_EQ(ptr2.Find<Fake2Error>()->val(), 4);

  ptr1.swap(ptr2);
  EXPECT_EQ(ptr1->val(), 3);
  EXPECT_EQ(ptr1.get()->val(), 3);
  EXPECT_EQ((*ptr1).val(), 3);
  EXPECT_EQ(ptr1.error().val(), 3);
  EXPECT_EQ(ptr1.Find<Fake2Error>()->val(), 4);

  EXPECT_EQ(ptr2->val(), 1);
  EXPECT_EQ(ptr2.get()->val(), 1);
  EXPECT_EQ((*ptr2).val(), 1);
  EXPECT_EQ(ptr2.error().val(), 1);
  EXPECT_EQ(ptr2.Find<Fake2Error>()->val(), 2);

  ptr1.reset();
  EXPECT_TRUE(ptr1.ok());

  ptr2.reset(new Fake1Error("e5", 5));
  EXPECT_EQ(ptr2->val(), 5);
  EXPECT_EQ(ptr2.get()->val(), 5);
  EXPECT_EQ((*ptr2).val(), 5);
  EXPECT_EQ(ptr2.error().val(), 5);
  EXPECT_EQ(ptr2.Find<Fake2Error>(), nullptr);
}

TEST_F(StatusChainTest, StackElementAccess) {
  StatusChain<FakeBaseError> e1 = MakeStatus<Fake1Error>("e1", 1);
  StatusChain<FakeBaseError> e2 =
      MakeStatus<FakeBaseError>("e2", 2).Wrap(std::move(e1));
  StatusChain<FakeBaseError> e3 =
      MakeStatus<Fake1Error>("e3", 4).Wrap(std::move(e2));
  StatusChain<FakeBaseError> e4 =
      MakeStatus<Fake2Error>("e4", 8).Wrap(std::move(e3));
  StatusChain<FakeBaseError> e5 =
      MakeStatus<Fake1Error>("e5", 16).Wrap(std::move(e4));
  StatusChain<FakeBaseError> e6 =
      MakeStatus<Fake2Error>("e6", 32).Wrap(std::move(e5));

  EXPECT_FALSE(e6.Is<Fake3Error>());
  EXPECT_FALSE(e6.Is<Fake1Error>());
  EXPECT_TRUE(e6.Is<Fake2Error>());
  EXPECT_EQ(e6.Cast<Fake2Error>()->val(), 32);

  EXPECT_EQ(e6.Find<Fake3Error>(), nullptr);
  EXPECT_EQ(e6.Find<Fake1Error>()->val(), 16);
}

TEST_F(StatusChainTest, WrappingUnwrapping) {
  StatusChain<FakeBaseError> e0;
  EXPECT_FALSE(e0.IsWrapping());

  e0 = MakeStatus<Fake1Error>("e0", -1);
  EXPECT_FALSE(e0.IsWrapping());
  EXPECT_EQ(e0.Cast<Fake1Error>()->val(), -1);

  StatusChain<FakeBaseError> e1 =
      MakeStatus<Fake1Error>("e1", 1).Wrap(std::move(e0));
  EXPECT_FALSE(e0.IsWrapping());
  EXPECT_TRUE(e1.IsWrapping());
  EXPECT_EQ(e1.Cast<Fake1Error>()->val(), 1);

  StatusChain<FakeBaseError> e2 =
      MakeStatus<Fake1Error>("e2", 2).Wrap(std::move(e1));
  EXPECT_FALSE(e1.IsWrapping());
  EXPECT_TRUE(e2.IsWrapping());
  EXPECT_EQ(e2.Cast<Fake1Error>()->val(), 2);

  auto e1_unwrap = std::move(e2).Unwrap();
  EXPECT_FALSE(e2.IsWrapping());
  EXPECT_TRUE(e1_unwrap.IsWrapping());
  EXPECT_EQ(e1_unwrap.Cast<Fake1Error>()->val(), 1);

  StatusChain<FakeBaseError> e3 =
      MakeStatus<Fake1Error>("e3", 3).Wrap(std::move(e1_unwrap));
  EXPECT_FALSE(e1_unwrap.IsWrapping());
  EXPECT_TRUE(e3.IsWrapping());
  EXPECT_EQ(e3.Cast<Fake1Error>()->val(), 3);

  auto e0_unwrap = std::move(e3).Unwrap().Unwrap();
  EXPECT_FALSE(e3.IsWrapping());
  EXPECT_FALSE(e0_unwrap.IsWrapping());
  EXPECT_EQ(e0_unwrap.Cast<Fake1Error>()->val(), -1);

  e0_unwrap.WrapInPlace(MakeStatus<Fake2Error>("e4", 4));
  EXPECT_TRUE(e0_unwrap.IsWrapping());
  EXPECT_EQ(e0_unwrap.Find<Fake2Error>()->val(), 4);

  e0_unwrap.UnwrapInPlace().UnwrapInPlace();
  EXPECT_FALSE(e0_unwrap);
  EXPECT_FALSE(e0_unwrap.IsWrapping());
}

TEST_F(StatusChainTest, RangesAndIterators) {
  StatusChain<FakeBaseError> e1 = MakeStatus<Fake1Error>("+", 1);
  StatusChain<FakeBaseError> e2 =
      MakeStatus<FakeBaseError>("-", 2).Wrap(std::move(e1));
  StatusChain<FakeBaseError> e3 =
      MakeStatus<Fake1Error>("+", 4).Wrap(std::move(e2));
  StatusChain<FakeBaseError> e4 =
      MakeStatus<Fake2Error>("-", 8).Wrap(std::move(e3));
  StatusChain<FakeBaseError> e5 =
      MakeStatus<Fake1Error>("+", 16).Wrap(std::move(e4));
  StatusChain<Fake3Error> e6 =
      MakeStatus<Fake3Error>("-", 32).Wrap(std::move(e5));

  // Check various ways to iterate. In all case val should be a sum of all
  // Fake1Error vals (marked with "+" error message above for clarity).

  // Non-const range-for loop.
  int val = 0;
  for (auto error_obj_ptr : e6.range()) {
    if (Error::Is<Fake1Error>(error_obj_ptr)) {
      // shouldn't need to cast since iterator should point to FakeBaseError.
      val += error_obj_ptr->val();
    }
  }
  EXPECT_EQ(val, 1 + 4 + 16);

  // const range-for loop.
  val = 0;
  for (const auto error_obj_ptr : e6.const_range()) {
    if (Error::Is<Fake1Error>(error_obj_ptr)) {
      // shouldn't need to cast since iterator should point to FakeBaseError.
      val += error_obj_ptr->val();
    }
  }
  EXPECT_EQ(val, 1 + 4 + 16);

  // Manual non-const loop.
  val = 0;
  for (auto it = e6.range().begin(); it != e6.range().end(); ++it) {
    if (Error::Is<Fake1Error>(*it)) {
      // shouldn't need to cast since iterator should point to FakeBaseError.
      val += it->val();
    }
  }
  EXPECT_EQ(val, 1 + 4 + 16);

  // Manual const loop.
  val = 0;
  for (auto it = e6.const_range().begin(); it != e6.const_range().end(); ++it) {
    if (Error::Is<Fake1Error>(*it)) {
      // shouldn't need to cast since iterator should point to FakeBaseError.
      val += it->val();
    }
  }
  EXPECT_EQ(val, 1 + 4 + 16);

  // non-const range should be assignable to const one, and so iterator.
  StatusChain<Fake3Error>::const_iterator_range crange = e6.range();
  StatusChain<Fake3Error>::const_iterator cit = e6.range().begin();
  EXPECT_EQ(crange, e6.range());
  EXPECT_EQ(cit, e6.range().begin());
}

TEST_F(StatusChainTest, WrapTransform) {
  StatusChain<FakeBaseError> e1 = MakeStatus<Fake1Error>("+", 1);
  StatusChain<FakeBaseError> e2 =
      MakeStatus<FakeBaseError>("-", 2).Wrap(std::move(e1));
  StatusChain<FakeBaseError> e3 =
      MakeStatus<Fake1Error>("+", 4).Wrap(std::move(e2));
  StatusChain<FakeBaseError> e4 =
      MakeStatus<Fake2Error>("-", 8).Wrap(std::move(e3));
  StatusChain<FakeBaseError> e5 =
      MakeStatus<Fake1Error>("+", 16).Wrap(std::move(e4));
  StatusChain<Fake3Error> e6 =
      MakeStatus<Fake3Error>("!", 32).Wrap(std::move(e5));

  // The transform above sums all Fake1Error vals (marked with "+" error message
  // above for clarity).
  EXPECT_EQ(e6->val(), 1 + 4 + 16);
  EXPECT_EQ(e6.Find<Fake1Error>()->val(), 16);

  StatusChain<Fake3Error> e7_with_drop =
      MakeStatus<Fake3Error>("!", 64).Wrap(std::move(e6), WrapTransformOnly);
  EXPECT_EQ(e7_with_drop->val(), 1 + 4 + 16);
  EXPECT_EQ(e6.Find<Fake1Error>(), nullptr);
}

TEST_F(StatusChainTest, BoolsOksAndMessages) {
  StatusChain<FakeBaseError> base_ok;
  EXPECT_FALSE(base_ok);
  EXPECT_TRUE(base_ok.ok());

  StatusChain<FakeBaseError> base_error =
      MakeStatus<FakeBaseError>("base_error", 0);
  EXPECT_TRUE(base_error);
  EXPECT_FALSE(base_error.ok());
  EXPECT_EQ(base_error.ToFullString(), "FakeBase: base_error");

  StatusChain<Fake1Error> fake_1_error = MakeStatus<Fake1Error>("fake1", 0);
  EXPECT_TRUE(fake_1_error);
  EXPECT_FALSE(fake_1_error.ok());
  EXPECT_EQ(fake_1_error.ToFullString(), "Fake1: fake1");

  StatusChain<Fake2Error> fake_2_error = MakeStatus<Fake2Error>("fake2", 0);
  EXPECT_TRUE(fake_2_error);
  EXPECT_FALSE(fake_2_error.ok());
  EXPECT_EQ(fake_2_error.ToFullString(), "Fake2: fake2: FROM TRAIT");

  auto tmp_1 = std::move(fake_1_error).Wrap(std::move(base_error));
  auto tmp_2 = std::move(fake_2_error).Wrap(std::move(tmp_1));
  StatusChain<FakeBaseError> stack = std::move(tmp_2);
  EXPECT_TRUE(stack);
  EXPECT_FALSE(stack.ok());

  EXPECT_EQ(stack.ToFullString(),
            "Fake2: fake2: FROM TRAIT: Fake1: fake1: FakeBase: base_error");
}

TEST_F(StatusChainTest, Macros) {
  auto lambda_as_is = []() {
    RETURN_IF_ERROR(MakeStatus<Fake1Error>("lambda 1", 0), AsIs());
    return OkStatus<Fake1Error>();
  };
  EXPECT_EQ(lambda_as_is().ToFullString(), "Fake1: lambda 1");

  auto lambda_as_is_with_log = []() {
    RETURN_IF_ERROR(MakeStatus<Fake1Error>("lambda 2", 0), AsIsWithLog("log"));
    return OkStatus<Fake1Error>();
  };
  EXPECT_EQ(lambda_as_is_with_log().ToFullString(), "Fake1: lambda 2");

  auto lambda_as_status = []() {
    RETURN_IF_ERROR(MakeStatus<Fake1Error>("lambda 3", 0),
                    AsStatus<Fake2Error>("wrap", 0));
    return OkStatus<Fake2Error>();
  };
  EXPECT_EQ(lambda_as_status().ToFullString(),
            "Fake2: wrap: FROM TRAIT: Fake1: lambda 3");

  auto lambda_as_value = []() {
    RETURN_IF_ERROR(MakeStatus<Fake1Error>("lambda 4", 0), AsValue(42));
    return 15;
  };
  EXPECT_EQ(lambda_as_value(), 42);

  auto lambda_as_value_with_log = []() {
    RETURN_IF_ERROR(MakeStatus<Fake1Error>("lambda 5", 0),
                    AsValueWithLog(42, "log"));
    return 15;
  };
  EXPECT_EQ(lambda_as_value_with_log(), 42);

  auto lambda_as_false_with_log = []() {
    RETURN_IF_ERROR(MakeStatus<Fake1Error>("lambda 6", 0),
                    AsFalseWithLog("log"));
    return true;
  };
  EXPECT_FALSE(lambda_as_false_with_log());

  auto lambda_success = []() {
    RETURN_IF_ERROR(OkStatus<Fake1Error>(), AsFalseWithLog("log"));
    return true;
  };
  EXPECT_TRUE(lambda_success());
}

TEST_F(StatusChainTest, StatusChainOrAssignAndRead) {
  StatusChainOr<std::string, Fake1Error> status_or1("data");
  StatusChainOr<std::string, Fake1Error> status_or2("");
  StatusChainOr<std::string, Fake1Error> status_or3(
      MakeStatus<Fake1Error>("fake1", 0));

  // Make sure the StatusChainOr is only constructable with expected nullptr.
  static_assert(
      std::is_constructible_v<StatusChainOr<std::unique_ptr<int>, Fake1Error>,
                              nullptr_t>,
      "should be constructable with nullptr");
  static_assert(
      !std::is_constructible_v<StatusChainOr<int, Fake1Error>, nullptr_t>,
      "should not be constructable with nullptr");

  // Make sure converting between StatusChainOr and bool work as intended.
  static_assert(!std::is_convertible_v<StatusChainOr<int, Fake1Error>, bool>,
                "should not be convertible to bool");
  static_assert(!std::is_convertible_v<StatusChainOr<bool, Fake1Error>, bool>,
                "should not be convertible to bool");
  static_assert(
      !std::is_convertible_v<bool, StatusChainOr<std::string, Fake1Error>>,
      "should not be convertible from bool");
  static_assert(std::is_convertible_v<bool, StatusChainOr<bool, Fake1Error>>,
                "should be convertible from bool");

  EXPECT_TRUE(status_or1.ok());
  EXPECT_TRUE(status_or2.ok());
  EXPECT_FALSE(status_or3.ok());

  EXPECT_EQ(*status_or1, "data");
  EXPECT_TRUE(status_or2->empty());
  EXPECT_EQ(status_or3.status().ToFullString(), "Fake1: fake1");

  // StatusChainOr should be moveable.
  StatusChainOr<std::string, Fake1Error> status_or4 = std::move(status_or1);
  EXPECT_TRUE(status_or4.ok());
  EXPECT_EQ(*status_or4, "data");

  EXPECT_DEATH_IF_SUPPORTED(
      (StatusChainOr<std::string, Fake1Error>(OkStatus<Fake1Error>())),
      "Check failed");
}

TEST_F(StatusChainTest, StatusChainOrLambda) {
  using StatusChainOrType1 = StatusChainOr<std::unique_ptr<int>, FakeBaseError>;
  auto lambda1 = [](int value) -> StatusChainOrType1 {
    if (value == 0) {
      return MakeStatus<Fake1Error>("value shouldn't be zero", 0);
    } else if (value < 0) {
      return std::unique_ptr<int>(nullptr);
    } else {
      return std::make_unique<int>(123);
    }
  };

  using StatusChainOrType2 =
      StatusChainOr<std::tuple<bool, std::unique_ptr<std::string>, int>,
                    Fake1Error>;
  auto lambda2 = [](int value) -> StatusChainOrType2 {
    if (value == 0) {
      return MakeStatus<Fake1Error>("value shouldn't be zero", 0);
    } else if (value < 0) {
      return {std::in_place, false, nullptr, 0};
    } else {
      return std::make_tuple(true, std::make_unique<std::string>("data"),
                             0x1337);
    }
  };

  auto lambda3 = [&lambda1](int value) -> StatusChainOrType1 {
    StatusChainOrType1 result = lambda1(value);
    if (!result.ok()) {
      return MakeStatus<Fake4Error>("lambda1 failed", 4)
          .Wrap(std::move(result).status());
    }
    return std::move(*result);
  };

  auto lambda4 = [&lambda1](int value) -> StatusChain<FakeBaseError> {
    if (value < 0) {
      return MakeStatus<Fake4Error>("value shouldn't be negative", value);
    }
    RETURN_IF_ERROR(std::move(lambda1(value)).status(), AsIs());
    return OkStatus<Fake3Error>();
  };

  using StatusChainOrType3 = StatusChainOr<std::vector<int>, Fake1Error>;

  auto lambda5 = [](int value) -> StatusChainOrType3 {
    if (value == 0) {
      return MakeStatus<Fake1Error>("value shouldn't be zero", 0);
    } else if (value < 0) {
      return {std::in_place};
    } else {
      return {std::in_place,
              {
                  value,
                  value + 1,
                  value + 2,
                  value + 3,
              }};
    }
  };

  EXPECT_FALSE(lambda1(0).ok());
  EXPECT_FALSE(lambda1(0).status().ok());
  EXPECT_TRUE(lambda1(-1).ok());
  EXPECT_TRUE(lambda1(-1).status().ok());
  EXPECT_TRUE(lambda1(123).ok());
  EXPECT_TRUE(lambda1(123).status().ok());

  auto result0 = lambda1(0);
  auto result1 = lambda1(-1);
  auto result123 = lambda1(123);
  const auto& result0_status = result0.status();
  const auto& result1_status = result1.status();
  const auto& result123_status = result123.status();

  EXPECT_FALSE(result0.ok());
  EXPECT_FALSE(result0_status.ok());
  EXPECT_TRUE(result1.ok());
  EXPECT_TRUE(result1_status.ok());
  EXPECT_TRUE(result123.ok());
  EXPECT_TRUE(result123_status.ok());

  EXPECT_EQ(result0.status().ToFullString(), "Fake1: value shouldn't be zero");
  EXPECT_EQ(*result1, nullptr);
  EXPECT_NE(*result123, nullptr);
  EXPECT_EQ(**result123, 123);

  EXPECT_FALSE(lambda2(0).ok());
  EXPECT_FALSE(lambda1(0).status().ok());
  EXPECT_TRUE(lambda2(-1).ok());
  EXPECT_TRUE(lambda2(-1).status().ok());
  EXPECT_TRUE(lambda2(123).ok());
  EXPECT_TRUE(lambda2(123).status().ok());

  auto result30 = lambda3(0);
  auto result31 = lambda3(-1);
  auto result3123 = lambda3(123);

  EXPECT_FALSE(result30.ok());
  EXPECT_FALSE(result30.status().ok());
  EXPECT_TRUE(result31.ok());
  EXPECT_TRUE(result31.status().ok());
  EXPECT_TRUE(result3123.ok());
  EXPECT_TRUE(result3123.status().ok());

  EXPECT_EQ(result30.status().ToFullString(),
            "Fake4: lambda1 failed: Fake1: value shouldn't be zero");
  EXPECT_EQ(*result31, nullptr);
  EXPECT_NE(*result3123, nullptr);
  EXPECT_EQ(**result3123, 123);

  EXPECT_FALSE(lambda4(0).ok());
  EXPECT_FALSE(lambda4(-1).ok());
  EXPECT_TRUE(lambda4(123).ok());

  EXPECT_EQ(lambda4(0).ToFullString(), "Fake1: value shouldn't be zero");
  EXPECT_EQ(lambda4(-1).ToFullString(), "Fake4: value shouldn't be negative");

  auto result50 = lambda5(0);
  auto result51 = lambda5(-1);
  auto result5123 = lambda5(123);

  EXPECT_FALSE(result50.ok());
  EXPECT_TRUE(result51.ok());
  EXPECT_TRUE(result5123.ok());

  EXPECT_TRUE(result51->empty());
  EXPECT_EQ(result5123->size(), 4);
  EXPECT_EQ(result5123->at(3), 126);
}

TEST_F(StatusChainTest, StatusChainOrDerive) {
  class BaseStruct {
   public:
    virtual ~BaseStruct() = default;
  };
  class DeriveStruct : public BaseStruct {
   public:
    virtual ~DeriveStruct() = default;
  };

  using StatusChainOrBase =
      StatusChainOr<std::unique_ptr<BaseStruct>, FakeBaseError>;
  using StatusChainOrDerive =
      StatusChainOr<std::unique_ptr<DeriveStruct>, FakeBaseError>;

  auto lambda1 = [](int value) -> StatusChainOrDerive {
    if (value == 0) {
      return MakeStatus<Fake1Error>("value shouldn't be zero", 0);
    } else {
      return std::make_unique<DeriveStruct>();
    }
  };

  auto lambda2 = [&lambda1](int value) -> StatusChainOrBase {
    if (value < 0) {
      return std::make_unique<BaseStruct>();
    }
    if (value == 123) {
      return std::make_unique<DeriveStruct>();
    }
    auto result = lambda1(value);
    if (!result.ok()) {
      return MakeStatus<Fake4Error>("lambda1 failed", 4)
          .Wrap(std::move(result).status());
    }
    return std::move(result).value();
  };

  auto result0 = lambda2(0);
  auto result1 = lambda2(-1);
  auto result123 = lambda2(123);
  auto result456 = lambda2(456);

  EXPECT_FALSE(result0.ok());
  EXPECT_TRUE(result1.ok());
  EXPECT_TRUE(result123.ok());
  EXPECT_TRUE(result456.ok());
}

}  // namespace

}  // namespace status
}  // namespace hwsec_foundation
