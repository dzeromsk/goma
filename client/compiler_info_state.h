// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_STATE_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_STATE_H_

#include <memory>
#include <string>

#include "compiler_flags.h"
#include "compiler_info.h"

using std::string;

namespace devtools_goma {

// CompilerInfoState contains CompilerInfo (created from local system) and
// disabled status (updated by response from remote).
// ref counted.
class CompilerInfoState {
 public:
  // Constructor creates with refcnt_==0.
  // Before sharing it, caller should call Ref.
  // Takes ownership of data.
  explicit CompilerInfoState(std::unique_ptr<CompilerInfoData> data);

  const CompilerInfo& info() const { return *compiler_info_; }

  // refcnt returns the current reference count.
  // potentially race. when you get the value, actual refcnt may be updated.
  // use be careful.
  int refcnt() const;

  // Returns if it has been disabled (e.g. compiler not found in backend)
  // Potential race (i.e. even if caller gets disabled()==false, it will
  // become true while checking input files or calling rpc), but it might
  // be acceptable.
  bool disabled() const;
  string GetDisabledReason() const;
  void SetDisabled(bool disabled, const string& disabled_reason);

  void Use(const string& local_compiler_path, const CompilerFlags& flags);
  int used() const;

  void UpdateLastUsedAt();

 private:
  friend class ScopedCompilerInfoState;
  friend class CompilerInfoCache;
  friend class CompilerInfoCacheTest;
  ~CompilerInfoState();

  static std::unique_ptr<CompilerInfo> MakeCompilerInfo(
      std::unique_ptr<CompilerInfoData> data);

  void Ref();
  void Deref();

  std::unique_ptr<CompilerInfo> compiler_info_;

  mutable Lock mu_;
  int refcnt_ GUARDED_BY(mu_);
  // When server side does not have the information about this compiler,
  // it's disabled.
  bool disabled_ GUARDED_BY(mu_);
  string disabled_reason_ GUARDED_BY(mu_);

  int used_;

  DISALLOW_COPY_AND_ASSIGN(CompilerInfoState);
};

// ScopedCompilerInfoState manages lifecycle of CompilerInfoState.
// thread-unsafe.
//
// Initializes
//   ScopedCompilerInfoState cis;
//   cis.FillFromCompilerOutputs(...);
//
// share compiler_info_state with cis:
//   ScopedCompilerInfoState state;
//   state.reset(cis.get());
//
//   ScopedCompilerInfoState state2(cis);
//
// transfer compiler_info_state from cis:
//   ScopedCompilerInfoState state(std::move(cis));
class ScopedCompilerInfoState {
 public:
  ScopedCompilerInfoState() : state_(nullptr) {}
  explicit ScopedCompilerInfoState(CompilerInfoState* state);
  ~ScopedCompilerInfoState();

  ScopedCompilerInfoState(ScopedCompilerInfoState&& state) noexcept
      : state_(std::move(state.state_)) {
    state.state_ = nullptr;
  }

  ScopedCompilerInfoState& operator=(ScopedCompilerInfoState&& other) {
    std::swap(state_, other.state_);
    return *this;
  }

  CompilerInfoState* get() const { return state_; }

  // reset derefs current state and refs given state.
  void reset(CompilerInfoState* state);

  // swap swaps state with other.
  // useful to transfer state from other without modifying refcnt.
  void swap(ScopedCompilerInfoState* other);

  bool disabled() const;
  string GetDisabledReason() const;

 private:
  CompilerInfoState* state_;

  DISALLOW_COPY_AND_ASSIGN(ScopedCompilerInfoState);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_STATE_H_
