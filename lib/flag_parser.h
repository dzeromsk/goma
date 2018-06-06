// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_FLAG_PARSER_H_
#define DEVTOOLS_GOMA_LIB_FLAG_PARSER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>


#include <unordered_map>
#include "basictypes.h"
using std::string;

class FlagParser {
 public:
  struct Options {
    Options();

    // '-' for GCC, '/' for VC++, and '\0' for ar.
    char flag_prefix;

    // Alternative flag prefix if any.
    // '-' for VC++.
    // It is weaker than flag_prefix.  If arg starts with alt_flag_prefix, but
    // no Flag matching found, arg may be considered as non flag.
    // TODO: for clang-cl, flag_prefix='-', alt_flag_prefix='/' ?
    // https://code.google.com/p/chromium/issues/detail?id=427942
    char alt_flag_prefix;

    // Support -flag=value style. default false.
    bool allows_equal_arg;

    // Support -flagvalue style. default false.
    bool allows_nonspace_arg;

    // If true, we will skip the first argument. True by default.
    bool has_command_name;
  };
  class Flag;
  class Callback {
   public:
    Callback() {}
    virtual ~Callback() {}
    // Returns parsed flag value of value for flag.
    virtual string ParseFlagValue(const Flag& flag, const string& value) = 0;
   private:
    DISALLOW_COPY_AND_ASSIGN(Callback);
  };
  class Flag {
   public:
    // Uses seen_output to store boolean whether the flag is seen or not.
    // Should be called before calling FlagParser::Parse().
    // *seen_output will be updated in FlagParser::Parse().
    // Doesn't take ownership of seen_output.
    void SetSeenOutput(bool* seen_output);

    // Uses output to store original arguments for the flag.
    // Should be called before calling FlagParser::Parse().
    // *output will be updated in FlagParser::Parse().
    // output may be shared with other flags.
    // Doesn't take ownership of output.
    void SetOutput(std::vector<string>* output);

    // Uses values to store values for the flags.
    // If callback is not NULL, it is used to parse flag value before stroing
    // to values.  If callback is NULL, original flag value will be stored.
    // Should be called before calling FlagParser::Parse().
    // *values will be updated in FlagParser::Parse().
    // Doesn't take ownership of callback and values.
    void SetValueOutputWithCallback(Callback* callback,
                                    std::vector<string>* values);

    // Uses callback to get parsed args.
    // If callback is NULL or SetCallbackForParsedArgs() is not used, original
    // args will be used as parsed args.
    // Should be called before calling FlagParser::Parse().
    // Doesn't take ownership of callback.
    void SetCallbackForParsedArgs(Callback* callback);

    // Name of the flag.  E.g "c" for "-c". "" for non flag args.
    const string& name() const { return name_; }

    // True if the flag requires a value.
    bool require_value() const { return require_value_; }

    // True if the flag is used.  Used after FlagParser::Parse() called.
    bool seen() const { return seen_; }

    // Returns flag values.  Used after FlagParser::Parse() called.
    const std::vector<string>& values() const { return values_; }
    // Gets i'th flag value.  Used after FlagParser::Parse() called.
    const string& value(int i) const;
    // Gets last flag value.  Used after FlagParser::Parse() called.
    string GetLastValue() const;

   private:
    friend class FlagParser;
    friend std::unique_ptr<Flag>::deleter_type;

    using ParsedArgsMap = std::unordered_map<int, string>;

    Flag(const char* name, bool require_value, bool allow_space_arg,
         const Options& options);
    ~Flag();

    // Tries to parse args at i.
    // Returns true if it is the flag and sets last i in *last_i.
    // Returns false if it is not the flag.
    bool Parse(const std::vector<string>& args, size_t i, size_t* last_i);

    // Gets parsed arguments at i, where Parse() had returned true for the i.
    const string& GetParsedArgs(int i) const;

    void Output(int i, const string& arg, const string* value);

    string name_;
    bool require_value_;

    char flag_prefix_;
    char alt_flag_prefix_;
    bool allows_equal_arg_;
    bool allows_nonspace_arg_;
    bool allows_space_arg_;

    bool seen_;
    bool* seen_output_;
    std::vector<string>* output_;
    Callback* value_callback_;
    std::vector<string> values_;
    std::vector<string>* values_output_;
    Callback* parse_callback_;
    ParsedArgsMap parsed_args_;
    DISALLOW_COPY_AND_ASSIGN(Flag);
  };

  FlagParser();
  ~FlagParser();

  FlagParser::Options* mutable_options() {
    return &opts_;
  }

  // Adds flag to be parsed.
  // If name is already added, returns the same flag instance.
  // Must be called before calling Parse().
  //
  // BoolFlag doesn't take any value. "-name".
  Flag* AddBoolFlag(const char* name);

  // PrefixFlag may take value in the same argument. "-name" or "-namevalue".
  Flag* AddPrefixFlag(const char* name);

  // Flag takes value.
  // "-name value".
  // "-namevalue" (if allows_non_space_arg).
  // "-name=value" (if allows_equal_arg).
  Flag* AddFlag(const char* name);

  // Argument that isn't prefixed with flag_prefix.
  Flag* AddNonFlag();

  void Parse(const std::vector<string>& args);

  // Returns parsed args.  Called once Parse() is called.
  std::vector<string> GetParsedArgs();
  // Returns unknown flags. Valid after Parse() is called.
  const std::vector<string>& unknown_flag_args() const {
    return unknown_flag_args_;
  }

 private:
  Options opts_;
  std::map<string, std::unique_ptr<Flag>> flags_;

  // original args given by Parse().
  std::vector<string> args_;

  // Valid after Parse. This contains unknown flags.
  std::vector<string> unknown_flag_args_;

  std::vector<Flag*> parsed_flags_;

  DISALLOW_COPY_AND_ASSIGN(FlagParser);
};

#endif  // DEVTOOLS_GOMA_LIB_FLAG_PARSER_H_
