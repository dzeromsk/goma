// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_CPP_PARSER_H_
#define DEVTOOLS_GOMA_CLIENT_CPP_PARSER_H_

#include <bitset>
#include <list>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/strings/string_view.h"
#include "autolock_timer.h"
#include "basictypes.h"
#include "compiler_info.h"
#include "cpp_input.h"
#include "cpp_macro.h"
#include "cpp_token.h"
#include "file_id.h"
#include "glog/logging.h"
#include "gtest/gtest_prod.h"
#include "platform_thread.h"
#include "predefined_macros.h"

#ifdef _WIN32
# include "config_win.h"
#endif

using std::string;

namespace devtools_goma {

class Content;
class CppInputStream;

class MacroSet {
 public:
  MacroSet() {}
  void Set(int i) {
    macros_.insert(i);
  }
  bool Get(int i) const {
    return macros_.find(i) != macros_.end();
  }
  void Union(const MacroSet& other) {
    macros_.insert(other.macros_.begin(), other.macros_.end());
  }

  bool empty() const { return macros_.empty(); }

 private:
  std::set<int> macros_;
};

// CppParser is thread-unsafe.
// TODO: Add unittest for this class.
class CppParser {
 public:
  class IncludeObserver {
   public:
    virtual ~IncludeObserver() {}

    // Handles include directive that CppParser processes.
    // Returns true if the include file is found (or it was already processed).
    // Returns false if the include file was not found and failed to process
    // the include directive.
    virtual bool HandleInclude(
        const string& path,
        const string& current_directory,
        const string& current_filepath,
        char quote_char,  // '"' or '<'
        int include_dir_index) = 0;

    // Handles __has_include() macro.
    // Returns value of __has_include().
    virtual bool HasInclude(
        const string& path,
        const string& current_directory,
        const string& current_filepath,
        char quote_char,  // '"' or '<'
        int include_dir_index) = 0;
  };
  class ErrorObserver {
   public:
    virtual ~ErrorObserver() {}
    virtual void HandleError(const string& error) = 0;
  };

  using Token = CppToken;
  using Input = CppInput;

  typedef std::list<Token> TokenList;
  typedef std::vector<Token> ArrayTokenList;
  typedef std::vector<TokenList> ArrayArgList;
  typedef std::list<MacroSet> MacroSetList;

  CppParser();
  ~CppParser();

  void set_bracket_include_dir_index(int index) {
    bracket_include_dir_index_ = index;
  }
  void set_include_observer(IncludeObserver* obs) { include_observer_ = obs; }
  void set_error_observer(ErrorObserver* obs) { error_observer_ = obs; }
  void SetCompilerInfo(const CompilerInfo* compiler_info);

  // Support predefined macro. This is expected to be used for tests.
  // For usual cases, SetCompilerInfo() should be used.
  void EnablePredefinedMacro(const string& name);
  bool IsEnabledPredefinedMacro(const string& name) const {
    return enabled_predefined_macros_.count(name) > 0;
  }
  bool IsHiddenPredefinedMacro(const string& name) const;

  void set_is_vc() { is_vc_ = true; }
  void set_is_cplusplus(bool is_cplusplus) { is_cplusplus_ = is_cplusplus; }
  bool is_cplusplus() const { return is_cplusplus_; }

  // Parses and processes directives only.
  // Returns false if it failed to process and is pretty sure it missed some
  // input files.
  bool ProcessDirectives();

  Token NextToken(bool skip_space);
  void UngetToken(const Token& token);
  int NextDirective();

  void AddMacroByString(const string& name, const string& body);
  void DeleteMacro(const string& name);
  bool HasMacro(const string& name);
  bool IsMacroDefined(const string& name);

  void ClearBaseFile() { base_file_.clear(); }

  void AddStringInput(const string& content, const string& pathname);

  // Adds |content| of |path|, which exists in |directory|.
  // |include_dir_index| is an index of a list of include dirs.
  void AddFileInput(std::unique_ptr<Content> content, const FileId& fileid,
                    const string& path, const string& directory,
                    int include_dir_index);

  // Returns true if the parser has already processed the |path|
  // and the set of macros that the file depends on have not changed.
  bool IsProcessedFile(const string& filepath, int include_dir_index) {
    ++total_files_;
    if (!IsProcessedFileInternal(filepath, include_dir_index))
      return false;
    ++skipped_files_;
    return true;
  }

  int total_files() const { return total_files_; }
  int skipped_files() const { return skipped_files_; }

  int obj_cache_hit() const { return obj_cache_hit_; }
  int func_cache_hit() const { return func_cache_hit_; }

  // For debug.
  string DumpMacros();
  static string DebugString(const TokenList& tokens);
  static string DebugString(TokenList::const_iterator begin,
                            TokenList::const_iterator end);

  void Error(absl::string_view error);
  void Error(absl::string_view error, absl::string_view arg);
  string DebugStringPrefix();

  typedef void (CppParser::*DirectiveHandler)();
  static const DirectiveHandler kDirectiveTable[];
  static const DirectiveHandler kFalseConditionDirectiveTable[];

  // include_dir_index for the current directory, which is not specified by -I.
  // This is mainly used for the source file, or header files included by
  // #include "somewhere.h"
  static const int kCurrentDirIncludeDirIndex = 0;
  // include_dir_index will start from this value
  // for include directories specified by -iquote, -I, -isystem etc.
  // -iquote range [kIncludeDirIndexStarting, bracket_include_dir_index_).
  // others [bracket_include_dir_index_, ...).
  // in other words,
  //  #include "..." search starts from kIncludeDirIndexStarting.
  //    kCurrentDirIncludeDirIndex is special for current dir.
  //    directories specified by option are from kIncludeDirIndexStarting.
  //  #include <...> search starts from bracket_include_dir_index_.
  static const int kIncludeDirIndexStarting = 1;
 private:

  // Manage files having #pragma once.
  class PragmaOnceFileSet {
   public:
    void Insert(const std::string& file);
    bool Has(const std::string& file) const;

   private:
    std::unordered_set<std::string> files_;
  };

  struct Condition {
    explicit Condition(bool cond) : cond(cond), taken(cond) {}
    bool cond;
    bool taken;
  };

  // Helper class for macro expansion.
  // In macro expansion we associate each token with corresponding 'hide_set';
  // This helper class and its Iterator are intended to help us manage
  // two distinct lists (of tokens and hide_set) always together.
  class MacroExpandContext {
   public:
    class Iterator {
     public:
      Iterator(TokenList::iterator iter, MacroSetList::iterator hs_iter)
          : iter_(iter), hs_iter_(hs_iter) {}

      Iterator& operator++() {
        ++iter_;
        ++hs_iter_;
        return *this;
      }
      Iterator& operator--() {
        --iter_;
        --hs_iter_;
        return *this;
      }
      TokenList::iterator iter() const { return iter_; }
      MacroSetList::iterator hs_iter() const { return hs_iter_; }
      const Token& token() const { return (*iter_); }
      const MacroSet& hide_set() const { return (*hs_iter_); }
      bool operator==(const Iterator& rhs) const { return iter_ == rhs.iter_; }
      bool operator!=(const Iterator& rhs) const { return iter_ != rhs.iter_; }

     private:
      TokenList::iterator iter_;
      MacroSetList::iterator hs_iter_;
    };

    MacroExpandContext(TokenList* tokens, MacroSetList* hide_sets)
        : tokens_(tokens), hs_(hide_sets) {}

    void Insert(const Iterator& pos,
                const Token& token,
                const MacroSet& hide_set) {
      tokens_->insert(pos.iter(), token);
      hs_->insert(pos.hs_iter(), hide_set);
    }

    void Insert(const Iterator& pos,
                TokenList::const_iterator begin,
                TokenList::const_iterator end,
                const MacroSet& hide_set) {
      tokens_->insert(pos.iter(), begin, end);
      hs_->insert(pos.hs_iter(), distance(begin, end), hide_set);
    }

    const TokenList& tokens() const { return *tokens_; }
    const MacroSetList& hide_sets() const { return *hs_; }
    Iterator Begin() const { return Iterator(tokens_->begin(), hs_->begin()); }
    Iterator End() const { return Iterator(tokens_->end(), hs_->end()); }

   private:
    TokenList* tokens_;
    MacroSetList* hs_;
  };
  typedef MacroExpandContext::Iterator MacroExpandIterator;
  enum IncludeType {
    kTypeInclude,
    kTypeImport,  // include once.
    kTypeIncludeNext,
  };

  class IntegerConstantEvaluator;

  bool IsProcessedFileInternal(const string& filepath, int include_dir_index);

  void ProcessInclude();
  void ProcessImport();
  void ProcessIncludeNext();
  void ProcessDefine();
  void ProcessUndef();
  void ProcessConditionInFalse();
  void ProcessIfdef();
  void ProcessIfndef();
  void ProcessIf();
  void ProcessElse();
  void ProcessEndif();
  void ProcessElif();
  void ProcessPragma();

  void ProcessIncludeInternal(IncludeType include_type);

  // Parser helpers.
  void ReadObjectMacro(const string& name);
  void ReadFunctionMacro(const string& name);
  // Reads the identifier name to check #ifdef/#ifndef/defined(x)
  // When the syntax is invalid, empty string will be returned.
  string ReadDefined();
  int ReadCondition();
  // Same as ReadCondition except checking include guard form like
  // #if !defined(FOO). When such condition is detected, |ident| is
  // set to FOO.
  int ReadConditionWithCheckingIncludeGuard(string* ident);

  void TrimTokenSpace(Macro* macro);

  bool FastGetMacroArgument(const ArrayTokenList& input_tokens,
                            bool skip_space,
                            ArrayTokenList::const_iterator* iter,
                            ArrayTokenList* arg);
  bool FastGetMacroArguments(const ArrayTokenList& input_tokens,
                             bool skip_space,
                             ArrayTokenList::const_iterator* iter,
                             std::vector<ArrayTokenList>* args);
  bool FastExpand(const ArrayTokenList& input_tokens, bool skip_space,
                  std::set<int>* hideset, ArrayTokenList* output_tokens,
                  bool* need_fallback);

  bool Expand0Fastpath(const ArrayTokenList& input, bool skip_space,
                       ArrayTokenList* output);

  // Macro expansion routines.
  // (skip_space parameter is passed around mainly for optimization;
  // for integer expression evaluation in most cases we don't need to
  // preserve spaces.)
  void Expand0(const ArrayTokenList& input, ArrayTokenList* output,
               bool skip_space);
  void Expand(MacroExpandContext* input, MacroExpandIterator input_iter,
              MacroExpandContext* output,
              const MacroExpandIterator output_iter,
              bool skip_space, bool use_hideset);
  MacroExpandIterator Substitute(const ArrayTokenList& replacement,
                                 size_t num_args,
                                 const ArrayArgList& args,
                                 const MacroSet& hide_set,
                                 MacroExpandContext* output,
                                 const MacroExpandIterator output_iter,
                                 bool skip_space, bool use_hideset);
  void Glue(TokenList::iterator left_pos, const Token& right);
  Token Stringize(const TokenList& list);
  bool SkipUntilBeginMacroArguments(const string& macro_name,
                                    const MacroExpandContext& input,
                                    MacroExpandIterator* iter);
  bool GetMacroArguments(const std::string& macro_name, Macro* macro,
                         ArrayArgList* args,
                         const MacroExpandContext& input,
                         MacroExpandIterator* iter,
                         MacroSet* rparen_hs);

  // Macro dictionary helpers.
  // second element of returned value represents
  // whether macro is taken from cache or not.
  std::pair<Macro*, bool> AddMacro(const string& name, Macro::Type type,
                                   const FileId& fileid, size_t macro_pos);
  std::pair<Macro*, bool> AddMacroInternal(const string& name, Macro::Type type,
                          const FileId& fileid, size_t macro_pos);
  Macro* GetMacro(const string& name, bool add_undefined);

  Input* input() const {
    if (HasMoreInput()) {
      return inputs_.back().get();
    }
    return last_input_.get();
  }

  bool HasMoreInput() const {
    return !inputs_.empty();
  }
  void PopInput();

  bool CurrentCondition() const {
    return conditions_.empty() || conditions_.back().cond;
  }

  // Predefined macro callbacks.
  Token GetFileName();
  Token GetLineNumber();
  Token GetDate();
  Token GetTime();
  Token GetCounter();
  Token GetBaseFile();

  Token ProcessHasInclude(const ArrayTokenList& tokens);
  Token ProcessHasIncludeNext(const ArrayTokenList& tokens);
  bool ProcessHasIncludeInternal(const ArrayTokenList& tokens,
                                 bool is_include_next);

  Token ProcessHasFeature(const ArrayTokenList& tokens) {
    if (!compiler_info_) {
      VLOG(1) << DebugStringPrefix() << " CompilerInfo is not set.";
      return Token(0);
    }
    return ProcessHasCheckMacro("__has_feature", tokens,
                                compiler_info_->has_feature());
  }
  Token ProcessHasExtension(const ArrayTokenList& tokens) {
    if (!compiler_info_) {
      VLOG(1) << DebugStringPrefix() << " CompilerInfo is not set.";
      return Token(0);
    }
    return ProcessHasCheckMacro("__has_extension", tokens,
                                compiler_info_->has_extension());
  }
  Token ProcessHasAttribute(const ArrayTokenList& tokens) {
    if (!compiler_info_) {
      VLOG(1) << DebugStringPrefix() << " CompilerInfo is not set.";
      return Token(0);
    }
    return ProcessHasCheckMacro("__has_attribute", tokens,
                                compiler_info_->has_attribute());
  }
  Token ProcessHasCppAttribute(const ArrayTokenList& tokens) {
    if (!compiler_info_) {
      VLOG(1) << DebugStringPrefix() << " CompilerInfo is not set.";
      return Token(0);
    }
    return ProcessHasCheckMacro("__has_cpp_attribute", tokens,
                                compiler_info_->has_cpp_attribute());
  }
  Token ProcessHasDeclspecAttribute(const ArrayTokenList& tokens) {
    if (!compiler_info_) {
      VLOG(1) << DebugStringPrefix() << " CompilerInfo is not set.";
      return Token(0);
    }
    return ProcessHasCheckMacro("__has_declspec_attribute", tokens,
                                compiler_info_->has_declspec_attribute());
  }
  Token ProcessHasBuiltin(const ArrayTokenList& tokens) {
    if (!compiler_info_) {
      VLOG(1) << DebugStringPrefix() << " CompilerInfo is not set.";
      return Token(0);
    }
    return ProcessHasCheckMacro("__has_builtin", tokens,
                                compiler_info_->has_builtin());
  }

  Token ProcessHasCheckMacro(
      const string& name,
      const ArrayTokenList& tokens,
      const std::unordered_map<string, int>& has_check_macro);

#ifdef _WIN32
  static BOOL WINAPI InitializeWinOnce(PINIT_ONCE, PVOID, PVOID*);
#endif
  static void InitializeStaticOnce();

  std::vector<std::unique_ptr<Input>> inputs_;
  std::unique_ptr<Input> last_input_;

  Token last_token_;
  std::unique_ptr<MacroEnv> macros_;

  std::vector<Condition> conditions_;
  int condition_in_false_depth_;

  PragmaOnceFileSet pragma_once_fileset_;

  string current_date_;
  string current_time_;
  string base_file_;
  int counter_;

  std::unordered_map<string, bool> enabled_predefined_macros_;

  bool is_cplusplus_;

  int next_macro_id_;

  int bracket_include_dir_index_;
  IncludeObserver* include_observer_;
  ErrorObserver* error_observer_;

  // When include guard macro is detected, the token is preserved here.
  std::unordered_map<string, string> include_guard_ident_;

  const CompilerInfo* compiler_info_;
  bool is_vc_;

  // disabled_ becomes true if it detects unsupported features and is
  // pretty sure it couldn't pass necessary files to IncludeObserver.
  // b/9286087
  bool disabled_;

  // For statistics.
  int skipped_files_;
  int total_files_;

  // list of pointers to Macro cached in |macros_| a include processing.
  std::vector<Macro*> used_macros_;

  int obj_cache_hit_;
  int func_cache_hit_;

  PlatformThreadId owner_thread_id_;

  typedef std::unordered_map<string, Macro::CallbackObj> PredefinedObjMacroMap;
  typedef std::unordered_map<string, Macro::CallbackFunc>
      PredefinedFuncMacroMap;

  static PredefinedObjMacroMap* predefined_macros_;
  static PredefinedFuncMacroMap* predefined_func_macros_;
  static bool global_initialized_;
#ifndef _WIN32
  static pthread_once_t key_once_;
#else
  static INIT_ONCE key_once_;
#endif

  friend class CppParserTest;
  DISALLOW_COPY_AND_ASSIGN(CppParser);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CPP_PARSER_H_
