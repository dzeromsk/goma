// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compiler_info_state.h"

#include "absl/memory/memory.h"
#include "autolock_timer.h"
#include "compiler_info_builder.h"
#include "cxx/cxx_compiler_info.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "java/java_compiler_info.h"

using std::string;

namespace devtools_goma {

// static
std::unique_ptr<CompilerInfo> CompilerInfoState::MakeCompilerInfo(
    std::unique_ptr<CompilerInfoData> data) {
  switch (data->language_extension_case()) {
    case CompilerInfoData::kCxx:
      return absl::make_unique<CxxCompilerInfo>(std::move(data));
    case CompilerInfoData::kJavac:
      return absl::make_unique<JavacCompilerInfo>(std::move(data));
    case CompilerInfoData::kJava:
      return absl::make_unique<JavaCompilerInfo>(std::move(data));
    case CompilerInfoData::LANGUAGE_EXTENSION_NOT_SET:
      CHECK(false) << "CompilerInfoData does not have any language extension";
      return nullptr;
  }

  CHECK(false) << "unexpected language extension case: "
               << static_cast<int>(data->language_extension_case());
  return nullptr;
}

CompilerInfoState::CompilerInfoState(std::unique_ptr<CompilerInfoData> data)
    : compiler_info_(MakeCompilerInfo(std::move(data))),
      refcnt_(0),
      disabled_(false),
      used_(0) {
  LOG(INFO) << "New CompilerInfoState " << this;
  if (!compiler_info_->found() && !compiler_info_->HasError()) {
    CompilerInfoBuilder::AddErrorMessage("compiler not found",
                                         compiler_info_->mutable_data());
  }
}

CompilerInfoState::~CompilerInfoState() {}

void CompilerInfoState::Ref() {
  AUTOLOCK(lock, &mu_);
  refcnt_++;
}

void CompilerInfoState::Deref() {
  int refcnt;
  {
    AUTOLOCK(lock, &mu_);
    refcnt_--;
    refcnt = refcnt_;
  }
  if (refcnt == 0) {
    LOG(INFO) << "Delete CompilerInfoState " << this;
    delete this;
  }
}

int CompilerInfoState::refcnt() const {
  AUTOLOCK(lock, &mu_);
  return refcnt_;
}

bool CompilerInfoState::disabled() const {
  AUTOLOCK(lock, &mu_);
  return disabled_;
}

string CompilerInfoState::GetDisabledReason() const {
  AUTOLOCK(lock, &mu_);
  return disabled_reason_;
}

void CompilerInfoState::SetDisabled(bool disabled,
                                    const string& disabled_reason) {
  AUTOLOCK(lock, &mu_);
  LOG(INFO) << "CompilerInfoState " << this << " disabled=" << disabled
            << " reason=" << disabled_reason;
  disabled_ = true;
  disabled_reason_ = disabled_reason;
}

void CompilerInfoState::Use(const string& local_compiler_path,
                            const CompilerFlags& flags) {
  {
    AUTOLOCK(lock, &mu_);
    if (used_++ > 0)
      return;
  }

  // CompilerInfo::DebugString() could be too large for glog.
  // glog message size is 30000 by default.
  // https://github.com/google/glog/blob/bf766fac4f828c81556499d7c16d53cc871d8bd2/src/logging.cc#L335
  // So, split info log at max 20000.
  //
  // TODO: It might be good to introduce a new compact printer for
  // CompilerInfo. I tried implementing it with
  // google::protobuf::TextFormat::Printer, but it is hardcoding ':'
  // (key: value), so I gave it up to make a neat Printer with
  // TextFormat::Printer.
  string info = compiler_info_->DebugString();
  absl::string_view piece(info);

  LOG(INFO) << "compiler_info_state=" << this << " path=" << local_compiler_path
            << ": flags=" << flags.compiler_info_flags() << ": info="
            << piece.substr(0,
                            std::min(static_cast<size_t>(20000), piece.size()));

  size_t begin_pos = 20000;
  while (begin_pos < piece.size()) {
    size_t len = std::min(static_cast<size_t>(20000), piece.size() - begin_pos);
    LOG(INFO) << "info continued:"
              << " compiler_info_state=" << this
              << " info(continued)=" << piece.substr(begin_pos, len);
    begin_pos += len;
  }
}

int CompilerInfoState::used() const {
  AUTOLOCK(lock, &mu_);
  return used_;
}

void CompilerInfoState::UpdateLastUsedAt() {
  compiler_info_->set_last_used_at(time(nullptr));
}

ScopedCompilerInfoState::ScopedCompilerInfoState(CompilerInfoState* state)
    : state_(state) {
  if (state_ != nullptr)
    state_->Ref();
}

ScopedCompilerInfoState::~ScopedCompilerInfoState() {
  if (state_ != nullptr)
    state_->Deref();
}

void ScopedCompilerInfoState::reset(CompilerInfoState* state) {
  if (state != nullptr)
    state->Ref();
  if (state_ != nullptr)
    state_->Deref();
  state_ = state;
}

void ScopedCompilerInfoState::swap(ScopedCompilerInfoState* other) {
  CompilerInfoState* other_state = other->state_;
  other->state_ = state_;
  state_ = other_state;
}

bool ScopedCompilerInfoState::disabled() const {
  if (state_ == nullptr)
    return true;

  return state_->disabled();
}

string ScopedCompilerInfoState::GetDisabledReason() const {
  if (state_ == nullptr)
    return string();

  return state_->GetDisabledReason();
}

}  // namespace devtools_goma
