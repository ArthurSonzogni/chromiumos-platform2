// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>
#include <string>

#include "libhwsec-foundation/status/status_chain.h"
#include "libhwsec-foundation/status/status_chain_macros.h"

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

}  // namespace

}  // namespace status
}  // namespace hwsec_foundation
