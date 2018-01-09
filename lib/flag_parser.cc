// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "flag_parser.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "glog/logging.h"
#include "string_piece.h"
#include "string_piece_utils.h"
#include "strutil.h"
using std::string;

namespace {

struct FlagLengthComparator {
  bool operator() (FlagParser::Flag* a, FlagParser::Flag* b) const {
    return a->name().size() > b->name().size();
  }
};

}  // anonymous namespace

FlagParser::Options::Options()
    : flag_prefix('-'),
      alt_flag_prefix('\0'),
      allows_equal_arg(false),
      allows_nonspace_arg(false),
      has_command_name(true) {
}

FlagParser::Flag::Flag(const char* name,
                       bool require_value,
                       bool allows_space_arg,
                       const FlagParser::Options& options)
    : name_(name),
      require_value_(require_value),
      flag_prefix_(options.flag_prefix),
      alt_flag_prefix_(options.alt_flag_prefix),
      allows_equal_arg_(options.allows_equal_arg),
      allows_nonspace_arg_(options.allows_nonspace_arg),
      allows_space_arg_(allows_space_arg),
      seen_(false),
      seen_output_(nullptr),
      output_(nullptr),
      value_callback_(nullptr),
      values_output_(nullptr),
      parse_callback_(nullptr) {
}

FlagParser::Flag::~Flag() {
}

void FlagParser::Flag::SetSeenOutput(bool* seen_output) {
  seen_output_ = seen_output;
  *seen_output_ = false;
}

void FlagParser::Flag::SetOutput(std::vector<string>* output) {
  output_ = output;
}

void FlagParser::Flag::SetValueOutputWithCallback(
    Callback* callback, std::vector<string>* values) {
  value_callback_ = callback;
  values_output_ = values;
}

void FlagParser::Flag::SetCallbackForParsedArgs(Callback* callback) {
  parse_callback_ = callback;
}

bool FlagParser::Flag::Parse(const std::vector<string>& args, size_t i,
                             size_t* last_i) {
  absl::string_view key;
  if (!flag_prefix_) {
    key = args[i];
  } else if (args[i].size() > 1 &&
             (args[i][0] == flag_prefix_
              || (alt_flag_prefix_ && args[i][0] == alt_flag_prefix_))) {
    key = absl::ClippedSubstr(absl::string_view(args[i]), 1);
  } else {
    // non flag args
    VLOG(3) << "non flag arg:" << args[i];
  }
  VLOG(4) << "check flag '" << key << "' by '" << name_ << "'";
  if (name_.empty()) {
    if (key.empty()) {
      VLOG(3) << "FlagParser: non flag: " << args[i];
      Output(i, args[i], &args[i]);
      *last_i = i;
      return true;
    } else if (args[i][0] != flag_prefix_) {
      VLOG(3) << "FlagParser: maybe non flag? " << args[i];
      Output(i, args[i], &args[i]);
      *last_i = i;
      return true;
    }
    return false;
  }
  if (!strings::StartsWith(key, name_)) {
    return false;
  }
  if (key == name_) {
    if (!require_value_) {
      // E.g., "-c"
      VLOG(3) << "FlagParser: no require value: " << key;
      Output(i, args[i], nullptr);
      *last_i = i;
      return true;
    } else if (!allows_space_arg_) {
      // E.g., "-O"
      VLOG(3) << "FlagParser: no allow space arg: " << key;
      string no_value;
      Output(i, args[i], &no_value);
      *last_i = i;
      return true;
    } else {
      // E.g., "-x c++"
      if (i + 1U == args.size()) {
        VLOG(2) << "FlagParser: " << args[i] << " should take an argument";
        return false;
      }
      VLOG(3) << "FlagParser: key-value argument with space: " << args[i];
      Output(i, args[i], nullptr);
      Output(i + 1, args[i + 1], &args[i + 1]);
      *last_i = i + 1;
      return true;
    }
  }
  if (!require_value_) {
    // e.g. -clang-syntax for -c.
    return false;
  }
  if (allows_equal_arg_) {
    size_t equal_index = key.find('=');
    if (equal_index != string::npos &&
        key.substr(0, equal_index) == name_) {
      // E.g., "-isysroot=/foobar"
      VLOG(3) << "FlagParser: key-value argument with equal: " << args[i];
      const string value = string(absl::ClippedSubstr(key, equal_index + 1));
      Output(i, args[i], &value);
      *last_i = i;
      return true;
    }
  }
  if (allows_nonspace_arg_) {
    // E.g. "-xc++" or "-O2"
    VLOG(3) << "FlagParser: key-value argument without separator: " << args[i];
    const string value =
        args[i].substr(name_.size() + (flag_prefix_ ? 1 : 0));
    Output(i, args[i], &value);
    *last_i = i;
    return true;
  }
  return false;
}

const string& FlagParser::Flag::value(int i) const {
  CHECK_GE(i, 0);
  CHECK_LT(i, static_cast<int>(values_.size()));
  return values_[i];
}

string FlagParser::Flag::GetLastValue() const {
  if (values_.empty())
    return "";
  return values_[values_.size() - 1];
}

const string& FlagParser::Flag::GetParsedArgs(int i) const {
  unordered_map<int, string>::const_iterator found = parsed_args_.find(i);
  CHECK(found != parsed_args_.end()) << name_ << " at " << i;
  return found->second;
}

void FlagParser::Flag::Output(int i, const string& arg, const string* value) {
  VLOG(4) << "Output:" << i << " " << arg << " value="
          << (value ? *value : "(null)");
  seen_ = true;
  if (seen_output_)
    *seen_output_ = true;
  if (output_)
    output_->push_back(arg);

  if (value == nullptr) {
    CHECK(parsed_args_.insert(std::make_pair(i, arg)).second);
    return;
  }
  values_.push_back(*value);
  if (values_output_ != nullptr) {
    string v;
    if (value_callback_)
      v = value_callback_->ParseFlagValue(*this, *value);
    else
      v = *value;
    values_output_->push_back(v);
  }

  string parsed_value = *value;
  if (parse_callback_)
    parsed_value = parse_callback_->ParseFlagValue(*this, *value);
  string parsed_arg = arg;
  if (parsed_value != *value) {
    parsed_arg = StringReplace(arg, *value, parsed_value, true);
  }
  CHECK(parsed_args_.insert(std::make_pair(i, parsed_arg)).second);
}

FlagParser::FlagParser() {
}

FlagParser::~FlagParser() {
  for (const auto& iter : flags_) {
    Flag* flag = iter.second;
    delete flag;
  }
}

FlagParser::Flag* FlagParser::AddBoolFlag(const char* name) {
  Flag* flag = nullptr;
  std::pair<std::map<string, Flag*>::iterator, bool> p =
      flags_.insert(std::make_pair(name, flag));
  if (p.second) {
    p.first->second = new Flag(name, false, false, opts_);
  }
  return p.first->second;
}

FlagParser::Flag* FlagParser::AddPrefixFlag(const char* name) {
  Flag* flag = nullptr;
  std::pair<std::map<string, Flag*>::iterator, bool> p =
      flags_.insert(std::make_pair(name, flag));
  if (p.second) {
    p.first->second = new Flag(name, true, false, opts_);
  }
  return p.first->second;
}

FlagParser::Flag* FlagParser::AddFlag(const char* name) {
  Flag* flag = nullptr;
  std::pair<std::map<string, Flag*>::iterator, bool> p =
      flags_.insert(std::make_pair(name, flag));
  if (p.second) {
    p.first->second = new Flag(name, true, true, opts_);
  }
  return p.first->second;
}

FlagParser::Flag* FlagParser::AddNonFlag() {
  Flag* flag = nullptr;
  std::pair<std::map<string, Flag*>::iterator, bool> p =
      flags_.insert(std::make_pair("", flag));
  if (p.second) {
    p.first->second = new Flag("", true, false, opts_);
  }
  return p.first->second;
}

void FlagParser::Parse(const std::vector<string>& args) {
  std::copy(args.begin(), args.end(), back_inserter(args_));
  parsed_flags_.resize(args_.size());

  // Check longest flag name first.
  std::vector<Flag*> flags;
  for (const auto& iter : flags_) {
    flags.push_back(iter.second);
  }
  FlagLengthComparator comp;
  std::sort(flags.begin(), flags.end(), comp);

  for (size_t i = opts_.has_command_name ? 1 : 0; i < args.size(); i++) {
    const string& arg = args[i];
    VLOG(4) << "FlagParser: arg=" << arg;
    if (arg.empty()) {
      VLOG(3) << "FlagParser: empty flag";
      continue;
    }

    bool parsed = false;
    for (size_t j = 0; j < flags.size(); ++j) {
      size_t last_i;
      if (flags[j]->Parse(args_, i, &last_i)) {
        VLOG(3) << "matched for flag '" << flags[j]->name() << "' for "
                << args_[i];
        for (; i <= last_i; i++)
          parsed_flags_[i] = flags[j];
        i = last_i;
        parsed = true;
        break;
      }
    }

    if (!parsed && arg.front() == opts_.flag_prefix) {
      unknown_flag_args_.push_back(arg);
    }
  }
}

std::vector<string> FlagParser::GetParsedArgs() {
  std::vector<string> args;
  if (opts_.has_command_name)
    args.push_back(args_[0]);
  for (size_t i = (opts_.has_command_name ? 1 : 0);
       i < parsed_flags_.size();
       ++i) {
    if (parsed_flags_[i])
      args.push_back(parsed_flags_[i]->GetParsedArgs(i));
    else
      args.push_back(args_[i]);
  }
  return args;
}
