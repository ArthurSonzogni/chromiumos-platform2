// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/statusor.h"

#include <algorithm>
#include <memory>

#include <base/compiler_specific.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/util/status_macros.h"

namespace reporting {
namespace {

class Base1 {
 public:
  virtual ~Base1() = default;
  int pad;
};

class Base2 {
 public:
  virtual ~Base2() = default;
  int yetotherpad;
};

class Derived : public Base1, public Base2 {
 public:
  ~Derived() override = default;
  int evenmorepad;
};

class CopyNoAssign {
 public:
  explicit CopyNoAssign(int value) : foo(value) {}
  CopyNoAssign(const CopyNoAssign& other) : foo(other.foo) {}
  int foo;

 private:
  const CopyNoAssign& operator=(const CopyNoAssign&);
};

TEST(StatusOr, TestStatusCtor) {
  StatusOr<int> thing(Status(error::CANCELLED, ""));
  EXPECT_FALSE(thing.has_value());
  EXPECT_EQ(Status(error::CANCELLED, ""), thing.error());
}

TEST(StatusOr, TestValueCtor) {
  const int kI = 4;
  StatusOr<int> thing(kI);
  EXPECT_TRUE(thing.has_value());
  EXPECT_EQ(kI, thing.value());
}

TEST(StatusOr, TestCopyCtorStatusOk) {
  const int kI = 4;
  StatusOr<int> original(kI);
  StatusOr<int> copy(original);
  EXPECT_EQ(original.value(), copy.value());
}

TEST(StatusOr, TestCopyCtorStatusNotOk) {
  StatusOr<int> original(Status(error::CANCELLED, ""));
  StatusOr<int> copy(original);
  EXPECT_EQ(original.error(), copy.error());
}

TEST(StatusOr, TestCopyCtorStatusOKConverting) {
  const int kI = 4;
  StatusOr<int> original(kI);
  StatusOr<double> copy(original);
  EXPECT_OK(copy);
  EXPECT_EQ(original.value(), copy.value());
}

TEST(StatusOr, TestCopyCtorStatusNotOkConverting) {
  StatusOr<int> original(Status(error::CANCELLED, ""));
  StatusOr<double> copy(original);
  EXPECT_EQ(original.error(), copy.error());
}

TEST(StatusOr, TestAssignmentStatusOk) {
  const int kI = 4;
  StatusOr<int> source(kI);
  StatusOr<int> target = CreateUnknownErrorStatusOr();
  target = source;
  EXPECT_OK(target);
  EXPECT_EQ(source.value(), target.value());
}

TEST(StatusOr, TestAssignmentStatusNotOk) {
  StatusOr<int> source(Status(error::CANCELLED, ""));
  StatusOr<int> target = CreateUnknownErrorStatusOr();
  target = source;
  EXPECT_EQ(source.error(), target.error());
}

TEST(StatusOr, TestAssignmentStatusOKConverting) {
  const int kI = 4;
  StatusOr<int> source(kI);
  StatusOr<double> target = CreateUnknownErrorStatusOr();
  target = source;
  EXPECT_OK(target);
  EXPECT_DOUBLE_EQ(source.value(), target.value());
}

TEST(StatusOr, TestAssignmentStatusNotOkConverting) {
  StatusOr<int> source(Status(error::CANCELLED, ""));
  StatusOr<double> target = CreateUnknownErrorStatusOr();
  target = source;
  EXPECT_EQ(source.error(), target.error());
}

TEST(StatusOr, TestStatus) {
  StatusOr<int> good(4);
  EXPECT_TRUE(good.has_value());
  StatusOr<int> bad(Status(error::CANCELLED, ""));
  EXPECT_FALSE(bad.has_value());
  EXPECT_EQ(Status(error::CANCELLED, ""), bad.error());
}

TEST(StatusOr, TestValueConst) {
  const int kI = 4;
  const StatusOr<int> thing(kI);
  EXPECT_EQ(kI, thing.value());
}

TEST(StatusOr, TestPointerStatusCtor) {
  StatusOr<int*> thing(Status(error::CANCELLED, ""));
  EXPECT_FALSE(thing.has_value());
  EXPECT_EQ(Status(error::CANCELLED, ""), thing.error());
}

TEST(StatusOr, TestPointerValueCtor) {
  const int kI = 4;
  StatusOr<const int*> thing(&kI);
  EXPECT_TRUE(thing.has_value());
  EXPECT_EQ(&kI, thing.value());
}

TEST(StatusOr, TestPointerCopyCtorStatusOk) {
  const int kI = 0;
  StatusOr<const int*> original(&kI);
  StatusOr<const int*> copy(original);
  EXPECT_OK(copy);
  EXPECT_EQ(original.value(), copy.value());
}

TEST(StatusOr, TestPointerCopyCtorStatusNotOk) {
  StatusOr<int*> original(Status(error::CANCELLED, ""));
  StatusOr<int*> copy(original);
  EXPECT_EQ(original.error(), copy.error());
}

TEST(StatusOr, TestPointerCopyCtorStatusOKConverting) {
  Derived derived;
  StatusOr<Derived*> original(&derived);
  StatusOr<Base2*> copy(original);
  EXPECT_OK(copy);
  EXPECT_EQ(static_cast<const Base2*>(original.value()), copy.value());
}

TEST(StatusOr, TestPointerCopyCtorStatusNotOkConverting) {
  StatusOr<Derived*> original(Status(error::CANCELLED, ""));
  StatusOr<Base2*> copy(original);
  EXPECT_EQ(original.error(), copy.error());
}

TEST(StatusOr, TestPointerAssignmentStatusOk) {
  const int kI = 0;
  StatusOr<const int*> source(&kI);
  StatusOr<const int*> target = CreateUnknownErrorStatusOr();
  target = source;
  EXPECT_OK(target);
  EXPECT_EQ(source.value(), target.value());
}

TEST(StatusOr, TestPointerAssignmentStatusNotOk) {
  StatusOr<int*> source(Status(error::CANCELLED, ""));
  StatusOr<int*> target = CreateUnknownErrorStatusOr();
  target = source;
  EXPECT_EQ(source.error(), target.error());
}

TEST(StatusOr, TestPointerAssignmentStatusOKConverting) {
  Derived derived;
  StatusOr<Derived*> source(&derived);
  StatusOr<Base2*> target = CreateUnknownErrorStatusOr();
  target = source;
  EXPECT_OK(target);
  EXPECT_EQ(static_cast<const Base2*>(source.value()), target.value());
}

TEST(StatusOr, TestPointerAssignmentStatusNotOkConverting) {
  StatusOr<Derived*> source(Status(error::CANCELLED, ""));
  StatusOr<Base2*> target = CreateUnknownErrorStatusOr();
  target = source;
  EXPECT_EQ(source.error(), target.error());
}

TEST(StatusOr, TestPointerStatus) {
  const int kI = 0;
  StatusOr<const int*> good(&kI);
  EXPECT_TRUE(good.has_value());
  StatusOr<const int*> bad(Status(error::CANCELLED, ""));
  EXPECT_EQ(Status(error::CANCELLED, ""), bad.error());
}

TEST(StatusOr, TestPointerValue) {
  const int kI = 0;
  StatusOr<const int*> thing(&kI);
  EXPECT_EQ(&kI, thing.value());
}

TEST(StatusOr, TestPointerValueConst) {
  const int kI = 0;
  const StatusOr<const int*> thing(&kI);
  EXPECT_EQ(&kI, thing.value());
}

TEST(StatusOr, TestMoveStatusOr) {
  const int kI = 0;
  StatusOr<std::unique_ptr<int>> thing(std::make_unique<int>(kI));
  EXPECT_OK(thing);
  StatusOr<std::unique_ptr<int>> moved = std::move(thing);
  EXPECT_EQ(error::UNKNOWN, thing.error().code());
  EXPECT_TRUE(moved.has_value());
  EXPECT_EQ(kI, *moved.value());
}

TEST(StatusOr, TestBinding) {
  class RefCountedValue : public base::RefCounted<RefCountedValue> {
   public:
    explicit RefCountedValue(StatusOr<int> value) : value_(value) {}
    Status error() const { return value_.error(); }
    int value() const { return value_.value(); }

   private:
    friend class base::RefCounted<RefCountedValue>;
    ~RefCountedValue() = default;
    const StatusOr<int> value_;
  };
  const int kI = 0;
  base::OnceCallback<int(StatusOr<scoped_refptr<RefCountedValue>>)> callback =
      base::BindOnce([](StatusOr<scoped_refptr<RefCountedValue>> val) {
        return val.value()->value();
      });
  const int result =
      std::move(callback).Run(base::MakeRefCounted<RefCountedValue>(kI));
  EXPECT_EQ(kI, result);
}

TEST(StatusOr, TestAbort) {
  StatusOr<int> thing1(Status(error::UNKNOWN, "Unknown"));
  EXPECT_DEATH_IF_SUPPORTED(std::ignore = thing1.value(), "");

  StatusOr<std::unique_ptr<int>> thing2(Status(error::UNKNOWN, "Unknown"));
  EXPECT_DEATH_IF_SUPPORTED(std::ignore = std::move(thing2.value()), "");
}
}  // namespace
}  // namespace reporting
