// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_CALLBACK_H_
#define DEVTOOLS_GOMA_CLIENT_CALLBACK_H_

#include <memory>
#include <tuple>
#include <utility>

#include "basictypes.h"

namespace devtools_goma {

// Closure has two types: OneshotClosure and PermanentClosure.
// * Both has a fundamental type Closure.
// * OneshotClosure can take move-only arguments, especially std::unique_ptr.
//   When Run() is called, such arguments are passed with std::move.
// * PermanentClosure cannot take a move-only argument, because arguments can be
//   passed to the internal function several times. So, we cannot move them.

class Closure {
 public:
  Closure() {}
  virtual ~Closure() {}
  virtual void Run() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(Closure);
};

class OneshotClosure : public Closure {
 public:
  OneshotClosure() {}
  ~OneshotClosure() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(OneshotClosure);
};

class PermanentClosure : public Closure {
 public:
  PermanentClosure() {}
  ~PermanentClosure() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(PermanentClosure);
};

namespace internal {

// ----------------------------------------------------------------------
// Making integer sequence (template).
//
// TODO: C++14 has std::index_sequence for the same purpose.
// We cannot implement the same thing since C++14 version uses
// template variable, which does not exist in C++11.
// Currently we need to call type().

// index_sequence will be made via index_sequence_for<Args>::type().
// The template arguments are <0, 1, 2, ..., N - 1> where N is the size of Args.
template<size_t...>
struct index_sequence {};

template<size_t I, typename T, typename... Types>
struct make_indexes_impl;

template<size_t I, size_t... Indexes, typename T, typename... Types>
struct make_indexes_impl<I, index_sequence<Indexes...>, T, Types...> {
  typedef typename make_indexes_impl<I + 1,
                                     index_sequence<Indexes..., I>,
                                     Types...>::type type;
};

template<size_t I, size_t... Indexes>
struct make_indexes_impl<I, index_sequence<Indexes...>> {
  typedef index_sequence<Indexes...> type;
};

template<typename... Types>
struct index_sequence_for : make_indexes_impl<0, index_sequence<>, Types...> {
};

// ----------------------------------------------------------------------

template<typename... Args>
class OneshotFunctionClosure : public OneshotClosure {
 public:
  typedef void (*FunctionType)(Args...);
  OneshotFunctionClosure(FunctionType f, Args&&... args)
      : function_(f), args_(std::forward<Args>(args)...) {
  }
  ~OneshotFunctionClosure() override {}

  void Run() override {
    Apply(typename index_sequence_for<Args...>::type());
    delete this;
  }

 private:
  template<size_t... Indexes>
  void Apply(index_sequence<Indexes...>) {
    function_(std::forward<Args>(std::get<Indexes>(args_))...);
  }

  typename std::decay<FunctionType>::type function_;
  std::tuple<typename std::decay<Args>::type...> args_;
};

template<typename Class, typename... Args>
class OneshotMethodClosure : public OneshotClosure {
 public:
  typedef void (Class::*MethodType)(Args... args);

  OneshotMethodClosure(Class* object, MethodType method, Args&&... args)
      : object_(object), method_(method), args_(std::forward<Args>(args)...) {}
  ~OneshotMethodClosure() override {}

  void Run() override {
    Apply(typename index_sequence_for<Args...>::type());
    delete this;
  }

 private:
  template<size_t... Indexes>
  void Apply(index_sequence<Indexes...>) {
    (object_->*method_)(std::forward<Args>(std::get<Indexes>(args_))...);
  }

  Class* object_;
  typename std::decay<MethodType>::type method_;
  std::tuple<typename std::decay<Args>::type...> args_;
};

template<typename... Args>
class PermanentFunctionClosure : public PermanentClosure {
 public:
  typedef void (*FunctionType)(Args...);
  PermanentFunctionClosure(FunctionType f, Args... args)
      : function_(f), args_(std::forward<Args>(args)...) {
  }
  ~PermanentFunctionClosure() override {}

  void Run() override {
    Apply(typename index_sequence_for<Args...>::type());
  }

 private:
  template<size_t... Indexes>
  void Apply(index_sequence<Indexes...>) {
    function_(std::get<Indexes>(args_)...);
  }

  typename std::decay<FunctionType>::type function_;
  std::tuple<typename std::decay<Args>::type...> args_;
};

template<typename Class, typename... Args>
class PermanentMethodClosure : public PermanentClosure {
 public:
  typedef void (Class::*MethodType)(Args... args);

  PermanentMethodClosure(Class* object, MethodType method, Args... args)
      : object_(object), method_(method), args_(std::forward<Args>(args)...) {}
  ~PermanentMethodClosure() override {}

  void Run() override {
    Apply(typename index_sequence_for<Args...>::type());
  }

 private:
  template<size_t... Indexes>
  void Apply(index_sequence<Indexes...>) {
    (object_->*method_)(std::get<Indexes>(args_)...);
  }

  Class* object_;
  typename std::decay<MethodType>::type method_;
  std::tuple<typename std::decay<Args>::type...> args_;
};

}  // namespace internal

// TODO: NewCallback might have to return std::unique_ptr, too.

template<typename... Args>
inline OneshotClosure* NewCallback(void (*function)(Args...), Args... args) {
  return new internal::OneshotFunctionClosure<Args...>(
      function, std::forward<Args>(args)...);
}

template<typename Class, typename... Args>
inline OneshotClosure* NewCallback(
    Class* object, void (Class::*method)(Args...), Args... args) {
  return new internal::OneshotMethodClosure<Class, Args...>(
      object, method, std::forward<Args>(args)...);
}

template<typename... Args>
inline std::unique_ptr<PermanentClosure> NewPermanentCallback(
    void (*function)(Args...), Args... args) {
  return std::unique_ptr<PermanentClosure>(
      new internal::PermanentFunctionClosure<Args...>(
          function, std::forward<Args>(args)...));
}

template<typename Class, typename... Args>
inline std::unique_ptr<PermanentClosure> NewPermanentCallback(
    Class* object, void (Class::*method)(Args...), Args... args) {
  return std::unique_ptr<PermanentClosure>(
      new internal::PermanentMethodClosure<Class, Args...>(
          object, method, std::forward<Args>(args)...));
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CALLBACK_H_
