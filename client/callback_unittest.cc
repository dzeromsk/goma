// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "callback.h"

#include <memory>

#include <gtest/gtest.h>

#include "compiler_specific.h"

namespace {

void TestFunc0() {}

void TestFunc1(int x) {
  ASSERT_EQ(x, 1);
}
void TestFunc2(int x, int y) {
  ASSERT_EQ(x, 1);
  ASSERT_EQ(y, 2);
}

void TestFunc1UP(std::unique_ptr<int> x) {
  ASSERT_EQ(*x, 1);
}
void TestFunc2UP(std::unique_ptr<int> x, std::unique_ptr<int> y) {
  ASSERT_EQ(*x, 1);
  ASSERT_EQ(*y, 2);
}

class TestObject {
 public:
  void TestMethod0() {}

  void TestMethod1(int x) {
    ASSERT_EQ(x, 1);
  }
  void TestMethod2(int x, int y) {
    ASSERT_EQ(x, 1);
    ASSERT_EQ(y, 2);
  }

  void TestMethod1UP(std::unique_ptr<int> x) {
    ASSERT_EQ(*x, 1);
  }
  void TestMethod2UP(std::unique_ptr<int> x, std::unique_ptr<int> y) {
    ASSERT_EQ(*x, 1);
    ASSERT_EQ(*y, 2);
  }
};

}  // anonymous namespace

TEST(CallbackTest, PermanentCallback) {
  TestObject obj;

  std::unique_ptr<devtools_goma::PermanentClosure> closures[] = {
      devtools_goma::NewPermanentCallback(TestFunc0),
      devtools_goma::NewPermanentCallback(TestFunc1, 1),
      devtools_goma::NewPermanentCallback(TestFunc2, 1, 2),
      devtools_goma::NewPermanentCallback(&obj, &TestObject::TestMethod0),
      devtools_goma::NewPermanentCallback(&obj, &TestObject::TestMethod1, 1),
      devtools_goma::NewPermanentCallback(&obj, &TestObject::TestMethod2, 1, 2),
  };

  // Should OK to run multiple times.
  for (auto& cl : closures) {
    cl->Run();
    cl->Run();
  }
}

TEST(CallbackTest, OneshotCallback) {
  TestObject obj;

  devtools_goma::OneshotClosure* closures[] = {
      devtools_goma::NewCallback(TestFunc0),
      devtools_goma::NewCallback(TestFunc1, 1),
      devtools_goma::NewCallback(TestFunc2, 1, 2),
      devtools_goma::NewCallback(&obj, &TestObject::TestMethod0),
      devtools_goma::NewCallback(&obj, &TestObject::TestMethod1, 1),
      devtools_goma::NewCallback(&obj, &TestObject::TestMethod2, 1, 2),
  };

  for (auto& cl : closures) {
    cl->Run();
  }
}

TEST(CallbackTest, PassUniquePtr) {
  // If we have some memory leak, asan buildbot will detect it.

  {
    devtools_goma::OneshotClosure* c = devtools_goma::NewCallback(TestFunc0);
    c->Run();
  }
  {
    TestObject obj;
    devtools_goma::OneshotClosure* c =
        devtools_goma::NewCallback(&obj, &TestObject::TestMethod0);
    c->Run();
  }

  {
    std::unique_ptr<int> x(new int(1));
    devtools_goma::OneshotClosure* c =
        devtools_goma::NewCallback(TestFunc1UP, std::move(x));
    c->Run();
  }
  {
    std::unique_ptr<int> x(new int(1));

    TestObject obj;
    devtools_goma::OneshotClosure* c =
        devtools_goma::NewCallback(&obj, &TestObject::TestMethod1UP,
                                   std::move(x));
    c->Run();
  }

  {
    std::unique_ptr<int> x(new int(1));
    std::unique_ptr<int> y(new int(2));
    devtools_goma::OneshotClosure* c =
        devtools_goma::NewCallback(TestFunc2UP, std::move(x), std::move(y));
    c->Run();
  }
  {
    std::unique_ptr<int> x(new int(1));
    std::unique_ptr<int> y(new int(2));

    TestObject obj;
    devtools_goma::OneshotClosure* c =
        devtools_goma::NewCallback(&obj, &TestObject::TestMethod2UP,
                                   std::move(x), std::move(y));
    c->Run();
  }
}
