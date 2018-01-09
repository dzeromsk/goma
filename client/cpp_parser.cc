// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "cpp_parser.h"

#include <limits.h>
#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>

#include "compiler_info.h"
#include "compiler_specific.h"
#include "content.h"
#include "counterz.h"
#include "cpp_input.h"
#include "cpp_macro.h"
#include "cpp_tokenizer.h"
#include "file_id.h"
#include "include_guard_detector.h"
#include "ioutil.h"
#include "lockhelper.h"
#include "path.h"
#include "path_resolver.h"
#include "static_darray.h"
#include "string_piece.h"
#include "string_piece_utils.h"
#include "string_util.h"
#include "util.h"

namespace {

static const int kIncludeFileDepthLimit = 1024;

}  // anonymous namespace

namespace devtools_goma {
#include "cpp_parser_darray.h"


// CppParser::PragmaOnceFileSet ----------------------------------------

void CppParser::PragmaOnceFileSet::Insert(const std::string& file) {
  files_.insert(PathResolver::ResolvePath(file));
}

bool CppParser::PragmaOnceFileSet::Has(const std::string& file) const {
  return files_.find(PathResolver::ResolvePath(file)) != files_.end();
}

// CppParser::IntegerConstantEvaluator ---------------------------------

class CppParser::IntegerConstantEvaluator {
 public:
  IntegerConstantEvaluator(
      const ArrayTokenList& tokens,
      CppParser* parser)
      : tokens_(tokens),
        iter_(tokens.begin()),
        parser_(parser) {
    CHECK(parser_);
    VLOG(2) << parser_->DebugStringPrefix() << " Evaluating: "
            << DebugString(TokenList(tokens.begin(), tokens.end()));
  }

  int GetValue() {
    return Conditional();
  }

 private:
  int Conditional() {
    int v1 = Expression(Primary(), 0);
    while (iter_ != tokens_.end()) {
      if (iter_->IsPuncChar('?')) {
        ++iter_;
        int v2 = Conditional();
        if (iter_ == tokens_.end() || !iter_->IsPuncChar(':')) {
          parser_->Error("syntax error: missing ':' in ternary operation");
          return 0;
        }
        ++iter_;
        int v3 = Conditional();
        return v1 ? v2 : v3;
      }
      break;
    }
    return v1;
  }

  int Expression(int v1, int min_precedence) {
    while (iter_ != tokens_.end() &&
           iter_->IsOperator() &&
           iter_->GetPrecedence() >= min_precedence) {
      const Token& op = *iter_++;
      int v2 = Primary();
      while (iter_ != tokens_.end() &&
             iter_->IsOperator() &&
             iter_->GetPrecedence() > op.GetPrecedence()) {
        v2 = Expression(v2, iter_->GetPrecedence());
      }
      v1 = op.ApplyOperator(v1, v2);
    }
    return v1;
  }

  int Primary() {
    int result = 0;
    int sign = 1;
    while (iter_ != tokens_.end()) {
      const Token& token = *iter_++;
      switch (token.type) {
        case Token::IDENTIFIER:
          // If it comes to here without expanded to number, it means
          // identifier is not defined.  Such case should be 0 unless
          // it is the C++ reserved keyword "true".
          if (parser_->is_cplusplus() && token.string_value == "true") {
            // Int value of C++ reserved keyword "true" is 1.
            // See: ISO/IEC 14882:2011 (C++11) 4.5 Integral promotions.
            result = 1;
          }
          break;
        case Token::NUMBER:
          result = token.v.int_value;
          break;
        case Token::SUB:
          sign = 0 - sign;
          continue;
        case Token::ADD:
          continue;
        case Token::PUNCTUATOR:
          switch (token.v.char_value.c) {
            case '(':
              result = GetValue();
              if (iter_ != tokens_.end() && iter_->IsPuncChar(')')) {
                ++iter_;
              }
              break;
            case '!':
              return !Primary();
            case '~':
              return ~Primary();
            default: {
              parser_->Error("unknown unary operator: ", token.DebugString());
              break;
            }
          }
          break;
        default:
          break;
      }
      break;
    }
    return sign * result;
  }

  const ArrayTokenList& tokens_;
  ArrayTokenList::const_iterator iter_;
  CppParser* parser_;

  DISALLOW_COPY_AND_ASSIGN(IntegerConstantEvaluator);
};

// CppParser -----------------------------------------------------------

// static
#ifndef _WIN32
pthread_once_t CppParser::key_once_ = PTHREAD_ONCE_INIT;
#else
INIT_ONCE CppParser::key_once_;
#endif

bool CppParser::global_initialized_ = false;
CppParser::PredefinedObjMacroMap* CppParser::predefined_macros_ = nullptr;
CppParser::PredefinedFuncMacroMap* CppParser::predefined_func_macros_ = nullptr;

const CppParser::DirectiveHandler CppParser::kDirectiveTable[] = {
  &CppParser::ProcessInclude,
  &CppParser::ProcessImport,
  &CppParser::ProcessIncludeNext,
  &CppParser::ProcessDefine,
  &CppParser::ProcessUndef,
  &CppParser::ProcessIfdef,
  &CppParser::ProcessIfndef,
  &CppParser::ProcessIf,
  &CppParser::ProcessElse,
  &CppParser::ProcessEndif,
  &CppParser::ProcessElif,
  &CppParser::ProcessPragma,
};

const CppParser::DirectiveHandler CppParser::kFalseConditionDirectiveTable[] = {
  nullptr, nullptr, nullptr,  // include, import, include_next
  nullptr, nullptr,        // define, undef
  &CppParser::ProcessConditionInFalse,
  &CppParser::ProcessConditionInFalse,
  &CppParser::ProcessConditionInFalse,
  &CppParser::ProcessElse,
  &CppParser::ProcessEndif,
  &CppParser::ProcessElif,
  nullptr,              // pragma
};

COMPILE_ASSERT(arraysize(CppParser::kDirectiveTable) ==
               arraysize(kDirectiveKeywords),
               directives_keywords_handler_mismatch);
COMPILE_ASSERT(arraysize(CppParser::kDirectiveTable) ==
               arraysize(CppParser::kFalseConditionDirectiveTable),
               directives_array_size_mismatch);

CppParser::CppParser()
    : condition_in_false_depth_(0),
      counter_(0),
      is_cplusplus_(false),
      next_macro_id_(0),
      bracket_include_dir_index_(kIncludeDirIndexStarting),
      include_observer_(nullptr),
      error_observer_(nullptr),
      compiler_info_(nullptr),
      is_vc_(false),
      disabled_(false),
      skipped_files_(0),
      total_files_(0),
      obj_cache_hit_(0),
      func_cache_hit_(0),
      owner_thread_id_(GetCurrentThreadId()) {
  char buf[26];
  time_t tm;
  time(&tm);
#ifndef _WIN32
  ctime_r(&tm, buf);
#else
  // All Windows CRT functions are thread-safe if use /MT or /MD in compile
  // options.
  ctime_s(buf, 26, &tm);
#endif
  current_time_ = string(&buf[11], 8);
  current_date_ = string(&buf[4], 7) + string(&buf[20], 4);
#ifndef _WIN32
  pthread_once(&key_once_, InitializeStaticOnce);
#else
  InitOnceExecuteOnce(&key_once_, CppParser::InitializeWinOnce,
                      nullptr, nullptr);
#endif
  // Push empty input as a sentinel.
  last_input_.reset(
      new Input(std::unique_ptr<Content>(Content::CreateFromString("")),
                FileId(), "<empty>", "<empty>", -1));
  macros_ = GetMacroEnvFromCache();
}

CppParser::~CppParser() {
  DCHECK(THREAD_ID_IS_SELF(owner_thread_id_));
  while (!inputs_.empty())
    PopInput();
  for (auto* p : used_macros_) {
    p->type = Macro::UNUSED;
  }
  ReleaseMacroEnvToCache(std::move(macros_));
}

void CppParser::SetCompilerInfo(const CompilerInfo* compiler_info) {
  compiler_info_ = compiler_info;
  if (compiler_info_ == nullptr)
    return;

  AddStringInput(compiler_info_->predefined_macros(), "(predefined)");
  ProcessDirectives();

  enabled_predefined_macros_ = compiler_info->supported_predefined_macros();
  set_is_cplusplus(compiler_info_->lang() == "c++");
}

bool CppParser::ProcessDirectives() {
  if (disabled_)
    return false;
  for (;;) {
    int directive = NextDirective();
    if (directive < 0) {
      break;
    }
    DCHECK(directive < static_cast<int>(arraysize(kDirectiveKeywords)));
    VLOG(2) << DebugStringPrefix() << " Directive:"
            << kDirectiveKeywords[directive];
    if (CurrentCondition()) {
      (this->*(kDirectiveTable[directive]))();
    } else {
      (this->*(kFalseConditionDirectiveTable[directive]))();
    }
  }
  return !disabled_;
}

void CppParser::UngetToken(const Token& token) {
  last_token_ = token;
}

int CppParser::NextDirective() {
  while (HasMoreInput()) {
    std::string error_reason;
    if (!CppTokenizer::SkipUntilDirective(input()->stream(), &error_reason)) {
      // When no directive was found, false is returned. It's not an error.
      // In this case, |error_reason| is empty.
      if (!error_reason.empty()) {
        Error(error_reason);
      }
      PopInput();
      if (HasMoreInput()) {
        continue;
      }
      return -1;
    }
    Input* current = input();
    current->stream()->SkipWhiteSpaces();
    const StaticDoubleArray& darray =
        CurrentCondition() ? kDirectiveArray : kConditionalDirectiveArray;
    StaticDoubleArray::LookupHelper helper(&darray);
    int value = -1;
    for (;;) {
      int c = current->stream()->GetCharWithBackslashHandling();
      if (c == EOF) {
        value = helper.GetValue();
        break;
      }
      if (!IsAsciiAlphaDigit(c) && c != '_') {
        current->stream()->UngetChar(c);
        value = helper.GetValue();
        break;
      }
      if (!helper.Lookup(static_cast<char>(c)))
        break;
    }
    if (value >= 0) {
      return value;
    }
    continue;
  }
  return -1;
}

void CppParser::AddMacroByString(const string& name, const string& body) {
  string macro = name + (body.empty() ? "" : " ") + body + '\n';
  AddStringInput(macro, "(macro)");
  ProcessDefine();
}

void CppParser::DeleteMacro(const string& name) {
  if (predefined_macros_->find(name) != predefined_macros_->end() ||
      predefined_func_macros_->find(name) != predefined_func_macros_->end()) {
    Error("predefined macro cannot be deleted:", name);
    return;
  }
  VLOG(2) << "#UNDEF Macro " << name;
  unordered_map<string, Macro>::iterator found = macros_->find(name);

  if (found == macros_->end() || found->second.type == Macro::UNUSED ||
      found->second.type == Macro::UNDEFINED) {
    return;
  }

  found->second.type = Macro::UNDEFINED;
}

bool CppParser::HasMacro(const string& name) {
  return GetMacro(name, false) != nullptr;
}

bool CppParser::IsMacroDefined(const string& name) {
  Macro* m = GetMacro(name, false);
  if (m == nullptr || m->type == Macro::UNUSED || m->type == Macro::UNDEFINED) {
    return false;
  }
  // Hack for GCC 5.
  // e.g. __has_include__ is not defined but callable.
  if (m->type == Macro::CBK_FUNC && IsHiddenPredefinedMacro(name)) {
    return false;
  }
  return true;
}

void CppParser::AddStringInput(const string& content, const string& pathname) {
  if (inputs_.size() >= kIncludeFileDepthLimit) {
    LOG(ERROR) << "Exceed include depth limit: " << kIncludeFileDepthLimit
               << " pathname: " << pathname;
    disabled_ = true;
    return;
  }
  inputs_.emplace_back(
      new Input(std::unique_ptr<Content>(Content::CreateFromString(content)),
                FileId(), pathname, "(string)",
                kCurrentDirIncludeDirIndex));
}

void CppParser::AddFileInput(
    std::unique_ptr<Content> fp, const FileId& fileid, const string& filepath,
    const string& directory, int include_dir_index) {
  if (inputs_.size() >= kIncludeFileDepthLimit) {
    LOG(ERROR) << "Exceeds include depth limit: " << kIncludeFileDepthLimit
               << " filepath: " << filepath;
    disabled_ = true;
    return;
  }

  DCHECK(fp);
  DCHECK_GE(include_dir_index, kCurrentDirIncludeDirIndex);
  if (base_file_.empty())
    base_file_ = filepath;
  inputs_.emplace_back(new Input(std::move(fp), fileid, filepath, directory,
                                 include_dir_index));
  VLOG(2) << "Including file: " << filepath;
}

string CppParser::DumpMacros() {
  std::stringstream ss;
  for (const auto& iter : *macros_) {
    ss << iter.second.DebugString(this, iter.first) << std::endl;
  }
  return ss.str();
}

/* static */
string CppParser::DebugString(const TokenList& tokens) {
  return DebugString(tokens.begin(), tokens.end());
}

/* static */
string CppParser::DebugString(TokenList::const_iterator begin,
                              TokenList::const_iterator end) {
  string str;
  for (auto iter = begin; iter != end; ++iter) {
    str.append(iter->DebugString());
  }
  return str;
}

string CppParser::DebugStringPrefix() {
  string str;
  str.reserve(input()->filepath().size() + 32);
  str.append("(");
  str.append(input()->filepath());
  str.append(":");
  str.append(std::to_string(input()->stream()->line()));
  str.append(")");
  return str;
}

void CppParser::Error(StringPiece error) {
  if (!error_observer_)
    return;
  Error(error, "");
}

void CppParser::Error(StringPiece error, StringPiece arg) {
  if (!error_observer_)
    return;
  string str;
  str.reserve(error.size() + input()->filepath().size() + 100);
  str.append("CppParser");
  str.append(DebugStringPrefix());
  str.append(" ");
  error.AppendToString(&str);
  arg.AppendToString(&str);
  error_observer_->HandleError(str);
}

void CppParser::ProcessInclude() {
  GOMA_COUNTERZ("include");
  input()->include_guard_detector()->OnProcessOther();
  ProcessIncludeInternal(kTypeInclude);
}

void CppParser::ProcessImport() {
  GOMA_COUNTERZ("import");
  input()->include_guard_detector()->OnProcessOther();
  if (!is_vc_) {
    // For gcc, #import means include only-once.
    // http://gcc.gnu.org/onlinedocs/gcc-3.2/cpp/Obsolete-once-only-headers.html

    // For Objective-C, #import means include only-once.
    // https://developer.apple.com/library/mac/documentation/MacOSX/Conceptual/BPFrameworks/Tasks/IncludingFrameworks.html
    // > If you are working in Objective-C, you may use the #import directive
    // instead of the #include directive. The two directives have the same
    // basic results. but the #import directive guarantees that the same
    // header file is never included more than once.
    ProcessIncludeInternal(kTypeImport);
    return;
  }
  // For VC++, #import is used to incorporate information from a type library.
  // http://msdn.microsoft.com/en-us/library/8etzzkb6(v=vs.71).aspx
  LOG(WARNING) << DebugStringPrefix() << " #import used, "
               << "but goma couldn't handle it yet. "
               << "See b/9286087";
  disabled_ = true;
}

void CppParser::ProcessIncludeNext() {
  GOMA_COUNTERZ("include_next");
  input()->include_guard_detector()->OnProcessOther();
  ProcessIncludeInternal(kTypeIncludeNext);
}

void CppParser::ProcessDefine() {
  input()->include_guard_detector()->OnProcessOther();
  Token name = NextToken(true);
  if (name.type != Token::IDENTIFIER) {
    Error("invalid preprocessing macro name token: ", name.DebugString());
    return;
  }
  Token token = NextToken(false);
  if (token.IsPuncChar('(')) {
    ReadFunctionMacro(name.string_value);
  } else {
    if (token.type == Token::NEWLINE || token.type == Token::END) {
      const auto pos = input()->stream()->pos();
      const auto& fileid = input()->fileid();
      Macro* macro = AddMacro(name.string_value, Macro::OBJ, fileid, pos).first;
      VLOG(2) << DebugStringPrefix() << " #DEFINE "
              << macro->DebugString(this, name.string_value);
      return;
    }
    if (token.type != Token::SPACE) {
      Error("missing whitespace after macro name");
      UngetToken(token);
    }
    ReadObjectMacro(name.string_value);
  }
}

void CppParser::ProcessUndef() {
  input()->include_guard_detector()->OnProcessOther();
  Token name = NextToken(true);
  if (name.type != Token::IDENTIFIER) {
    Error("invalid preprocessing macro name token ", name.DebugString());
    return;
  }
  DeleteMacro(name.string_value);
}

void CppParser::ProcessConditionInFalse() {
  input()->include_guard_detector()->OnProcessCondition();
  ++condition_in_false_depth_;
}

void CppParser::ProcessIfdef() {
  input()->include_guard_detector()->OnProcessCondition();
  bool v = IsMacroDefined(ReadDefined());
  VLOG(2) << DebugStringPrefix() << " #IFDEF " << v;
  conditions_.push_back(Condition(v));
}

void CppParser::ProcessIfndef() {
  string ident = ReadDefined();
  input()->include_guard_detector()->OnProcessIfndef(ident);
  bool v = !IsMacroDefined(ident);
  VLOG(2) << DebugStringPrefix() << " #IFNDEF " << v;
  conditions_.push_back(Condition(v));
}

void CppParser::ProcessIf() {
  string ident;
  int v = ReadConditionWithCheckingIncludeGuard(&ident);
  input()->include_guard_detector()->OnProcessIf(ident);
  VLOG(2) << DebugStringPrefix() << " #IF " << v;
  conditions_.push_back(Condition(v != 0));
}

void CppParser::ProcessElse() {
  input()->include_guard_detector()->OnProcessOther();
  if (condition_in_false_depth_ > 0) {
    return;
  }
  if (conditions_.empty()) {
    Error("stray else");
    return;
  }
  conditions_.back().cond = (!conditions_.back().cond &&
                             !conditions_.back().taken);
}

void CppParser::ProcessEndif() {
  input()->include_guard_detector()->OnProcessEndif();
  if (condition_in_false_depth_) {
    --condition_in_false_depth_;
    return;
  }
  if (conditions_.empty()) {
    Error("stray endif");
    return;
  }
  conditions_.pop_back();
}

void CppParser::ProcessElif() {
  input()->include_guard_detector()->OnProcessOther();
  if (condition_in_false_depth_ > 0) {
    return;
  }
  if (conditions_.empty()) {
    Error("stray elif");
    return;
  }
  if (conditions_.back().taken) {
    conditions_.back().cond = false;
    return;
  }
  int v = ReadCondition();
  VLOG(2) << DebugStringPrefix() << " #ELIF " << v;
  conditions_.back().cond = (v != 0);
  conditions_.back().taken |= (v != 0);
}

void CppParser::ProcessPragma() {
  input()->include_guard_detector()->OnProcessOther();
  Token token(NextToken(true));
  if (token.type == Token::IDENTIFIER && token.string_value == "once") {
    pragma_once_fileset_.Insert(input()->filepath());
  }
}

CppParser::Token CppParser::NextToken(bool skip_space) {
  if (last_token_.type != Token::END) {
    Token token = last_token_;
    last_token_ = Token();
    VLOG(3) << token.DebugString();
    return token;
  }
  while (HasMoreInput()) {
    Token token;
    std::string error_reason;
    if (!CppTokenizer::NextTokenFrom(input()->stream(), skip_space,
                                     &token, &error_reason)) {
      if (!error_reason.empty()) {
        Error(error_reason);
      }
    }

    if (token.type != Token::END) {
      VLOG(3) << token.DebugString();
      return token;
    }
    PopInput();
  }
  return Token(Token::END);
}

void CppParser::ProcessIncludeInternal(IncludeType include_type) {
  input()->stream()->SkipWhiteSpaces();
  int c;
  if (!HasMoreInput() || (c = input()->stream()->GetChar()) == EOF) {
    Error("missing include path");
    return;
  }
  const char* directive = "";
  switch (include_type) {
    case kTypeInclude: directive = "include"; break;
    case kTypeImport: directive = "import"; break;
    case kTypeIncludeNext: directive = "include_next"; break;
    default:
      LOG(FATAL) << "unknown include_type=" << include_type;
  }
  // Simple <filepath> case.
  if (c == '<') {
    string path;
    string error_reason;
    if (!CppTokenizer::ReadStringUntilDelimiter(input()->stream(), &path,
                                                '>', &error_reason)) {
      Error(error_reason);
    }
    if (!path.empty() && include_observer_) {
      int next_index = bracket_include_dir_index_;
      if (include_type == kTypeIncludeNext) {
        next_index = input()->include_dir_index() + 1;
      }
      // We should not find the current directory (without specifying by -I).
      DCHECK_GE(next_index, bracket_include_dir_index_)
          << ' ' << input()->include_dir_index();
      if (!include_observer_->HandleInclude(
              path, input()->directory(), input()->filepath(), '<',
              next_index)) {
        LOG(WARNING) << "HandleInclude failed #" << directive
                     << " <" << path << ">"
                     << " from " << input()->filepath()
                     << " [dir:" << input()->directory()
                     << " index:" << input()->include_dir_index() << "]";
        return;
      }
      if (include_type == kTypeImport) {
        DCHECK(!inputs_.empty());
        const string& filepath = inputs_.back()->filepath();
        pragma_once_fileset_.Insert(filepath);
        VLOG(1) << "HandleInclude #import " << filepath;
      }
    }
    return;
  }
  // Simple "filepath" case.
  if (c == '"') {
    string path;
    string error_reason;
    if (!CppTokenizer::ReadStringUntilDelimiter(input()->stream(), &path,
                                                '"', &error_reason)) {
      Error(error_reason);
    }
    if (!path.empty() && include_observer_) {
      int quote_char = c;
      int next_index = input()->include_dir_index();
      if (include_type == kTypeIncludeNext) {
        quote_char = '<';
        ++next_index;
      }
      if (!include_observer_->HandleInclude(
              path, input()->directory(), input()->filepath(), quote_char,
              next_index)) {
        LOG(WARNING) << "HandleInclude failed #" << directive
                     << " \"" << path << "\""
                     << " from " << input()->filepath()
                     << " [dir:" << input()->directory()
                     << " index:" << input()->include_dir_index() << "]";
        return;
      }
      if (include_type == kTypeImport) {
        DCHECK(!inputs_.empty());
        const string& filepath = inputs_.back()->filepath();
        pragma_once_fileset_.Insert(filepath);
        VLOG(1) << "HandleInclude #import " << filepath;
      }
    }
    return;
  }
  input()->stream()->UngetChar(c);

  // Include path is neither <filepath> nor "filepath".
  // Try expanding macros if there are any.
  ArrayTokenList tokens;
  Token token = NextToken(true);
  while (token.type != Token::END && token.type != Token::NEWLINE) {
    tokens.push_back(token);
    token = NextToken(false);
  }

  ArrayTokenList expanded;
  Expand0(tokens, &expanded, false);

  if (expanded.empty()) {
    Error("#include expects \"filename\" or <filename>");
    LOG(WARNING) << "HandleInclude empty arg for #" << directive;
    return;
  }

  // See if the expanded token(s) is <filepath> or "filepath".
  token = expanded.front();
  if (token.type == Token::LT) {
    string path;
    auto iter = expanded.begin();
    ++iter;
    for (; iter != expanded.end() && iter->type != Token::GT; ++iter) {
      path.append(iter->GetCanonicalString());
    }
    int next_index = bracket_include_dir_index_;
    if (include_type == kTypeIncludeNext) {
      next_index = input()->include_dir_index() + 1;
      DCHECK_GE(next_index, bracket_include_dir_index_);
    }
    if (include_observer_) {
      if (!include_observer_->HandleInclude(
              path, input()->directory(), input()->filepath(), '<',
              next_index)) {
        LOG(WARNING) << "HandleInclude failed #" << directive
                     << " <" << path << ">"
                     << " from " << input()->filepath()
                     << " [dir:" << input()->directory()
                     << " index:" << input()->include_dir_index() << "]";
        return;
      }
      if (include_type == kTypeImport) {
        DCHECK(!inputs_.empty());
        const string& filepath = inputs_.back()->filepath();
        pragma_once_fileset_.Insert(filepath);
        VLOG(1) << "HandleInclude #import " << filepath;
      }
    }
    return;
  }
  if (token.type == Token::STRING) {
    if (include_observer_) {
      int quote_char = '"';
      int next_index = input()->include_dir_index();
      if (include_type == kTypeIncludeNext) {
        quote_char = '<';
        ++next_index;
      }
      if (!include_observer_->HandleInclude(
              token.string_value, input()->directory(), input()->filepath(),
              quote_char, next_index)) {
        LOG(WARNING) << "HandleInclude failed #" << directive
                     << " \"" << token.string_value << "\""
                     << " from " << input()->filepath()
                     << " [dir:" << input()->directory()
                     << " index:" << input()->include_dir_index() << "]";
        return;
      }
      if (include_type == kTypeImport) {
        DCHECK(!inputs_.empty());
        const string& filepath = inputs_.back()->filepath();
        pragma_once_fileset_.Insert(filepath);
        VLOG(1) << "HandleInclude #import " << filepath;
      }
    }
    return;
  }
  Error("#include expects \"filename\" or <filename>");
}

void CppParser::ReadObjectMacro(const string& name) {
  const auto pos = input()->stream()->pos();
  const auto& fileid = input()->fileid();

  auto optional_macro = AddMacro(name, Macro::OBJ, fileid, pos);
  if (optional_macro.second) {
    ++obj_cache_hit_;
    GOMA_COUNTERZ("object skip");
    return;
  } else {
    GOMA_COUNTERZ("object no skip");
  }
  Macro* macro = optional_macro.first;

  CHECK(macro);
  Token token = NextToken(true);
  while (token.type != Token::NEWLINE && token.type != Token::END) {
    // Remove contiguous spaces (i.e. '   ' => ' ')
    // Remove preceding spaces for ## (i.e. ' ##' => '##')
    if (token.type == Token::SPACE ||
        token.type == Token::DOUBLESHARP) {
      TrimTokenSpace(macro);
    }

    if (token.type == Token::IDENTIFIER) {
      macro->has_identifier_in_replacement = true;
    }
    macro->replacement.push_back(std::move(token));
    // Remove trailing spaces for ## (i.e. '## ' => '##')
    token = NextToken(token.type == Token::DOUBLESHARP);
  }

  TrimTokenSpace(macro);

  VLOG(2) << DebugStringPrefix() << " #DEFINE "
          << macro->DebugString(this, name);
}

void CppParser::ReadFunctionMacro(const string& name) {
  const auto pos = input()->stream()->pos();
  const auto& fileid = input()->fileid();

  unordered_map<string, size_t> params;
  size_t param_index = 0;
  bool is_vararg = false;
  for (;;) {
    Token token = NextToken(true);
    if (token.type == Token::NEWLINE || token.type == Token::END) {
      Error("missing ')' in the macro parameter list");
      return;
    } else if (token.type == Token::IDENTIFIER) {
      if (!params.insert(
              std::make_pair(token.string_value, param_index)).second) {
        Error("duplicate macro parameter ", token.string_value);
        return;
      }
      param_index++;
      token = NextToken(true);
      if (token.IsPuncChar(',')) {
        continue;
      }
      if (token.IsPuncChar(')')) {
        break;
      }
    } else if (token.type == Token::TRIPLEDOT) {
      is_vararg = true;
      token = NextToken(true);
      if (!token.IsPuncChar(')')) {
        Error("vararg must be the last of the macro parameter list");
        return;
      }
      break;
    } else if (token.IsPuncChar(')')) {
      break;
    }
    Error("invalid preprocessing macro arg token ", token.DebugString());
    return;
  }

  auto optional_macro = AddMacro(name, Macro::FUNC, fileid, pos);
  if (!optional_macro.first) {
    return;
  }
  if (optional_macro.second) {
    ++func_cache_hit_;
    GOMA_COUNTERZ("function skip");
    return;
  } else {
    GOMA_COUNTERZ("function no skip");
  }

  Macro* macro = optional_macro.first;
  DCHECK(params.size() == param_index);
  macro->num_args = params.size();
  macro->is_vararg = is_vararg;

  Token token = NextToken(true);
  while (token.type != Token::NEWLINE && token.type != Token::END) {
    if (token.type == Token::IDENTIFIER) {
      unordered_map<string, size_t>::iterator iter = params.find(
          token.string_value);
      if (iter != params.end()) {
        token.MakeMacroParam(iter->second);
      } else if (token.string_value == "__VA_ARGS__") {
        token.MakeMacroParamVaArgs();
      }
    }

    // Remove contiguous spaces (i.e. '   ' => ' ')
    // Remove preceding spaces for ## (i.e. ' ##' => '##')
    if (token.type == Token::SPACE ||
        token.type == Token::DOUBLESHARP) {
      TrimTokenSpace(macro);
    }
    if (token.type == Token::IDENTIFIER) {
      macro->has_identifier_in_replacement = true;
    }
    macro->replacement.push_back(std::move(token));
    // Remove trailing spaces for ## (i.e. '## ' => '##')
    token = NextToken(token.type == Token::DOUBLESHARP);
  }

  TrimTokenSpace(macro);

  VLOG(2) << DebugStringPrefix() << " #DEFINE "
          << macro->DebugString(this, name);
}

string CppParser::ReadDefined() {
  Token token(NextToken(true));
  bool has_paren = false;
  if (token.IsPuncChar('(')) {
    token = NextToken(true);
    has_paren = true;
  }
  if (token.type != Token::IDENTIFIER) {
    Error("macro names must be identifiers");
    return string();
  }
  if (has_paren) {
    Token paren(NextToken(true));
    if (!paren.IsPuncChar(')')) {
      UngetToken(paren);
      Error("missing terminating ')' character");
    }
  }

  return token.string_value;
}

int CppParser::ReadConditionWithCheckingIncludeGuard(string* ident) {
  // We use this state machine to detect include guard.
  // TODO: If IntegerConstantEvaluator can process 'defined',
  // this code should be simpler.
  enum State {
    START,
    HAS_READ_BANG,
    HAS_READ_COND,
    NOT_INCLUDE_GUARD,
  } state = START;

  // 1. Reads tokens while replacing "defined" expression.
  ArrayTokenList tokens;
  Token token(NextToken(true));

  for (;;) {
    if (token.type == Token::END || token.type == Token::NEWLINE) {
      break;
    }

    string s;
    if (token.type == Token::IDENTIFIER && token.string_value == "defined") {
      s = ReadDefined();
      token = Token(static_cast<int>(IsMacroDefined(s)));
    }

    if (state == START) {
      if (token.type == Token::PUNCTUATOR && token.v.char_value.c == '!') {
        state = HAS_READ_BANG;
      } else {
        state = NOT_INCLUDE_GUARD;
      }
    } else if (state == HAS_READ_BANG) {
      if (!s.empty()) {
        state = HAS_READ_COND;
        *ident = s;
      } else {
        state = NOT_INCLUDE_GUARD;
      }
    } else {
      // When we read something when state is HAS_READ_COND, it's not
      // an include guard, let alone NOT_INCLUDE_GUARD.
      state = NOT_INCLUDE_GUARD;
    }

    tokens.push_back(std::move(token));
    token = NextToken(false);
  }

  // When state is HAS_READ_COND, it means we detected #if !defined(FOO).
  if (state != HAS_READ_COND)
    ident->clear();

  // 2. Expands macros.
  ArrayTokenList expanded;
  Expand0(tokens, &expanded, true);

  // 3. Evaluates the expanded integer constant expression.
  IntegerConstantEvaluator evaluator(expanded, this);
  return evaluator.GetValue();
}

void CppParser::TrimTokenSpace(Macro* macro) {
  while (!macro->replacement.empty() &&
         macro->replacement.back().type == Token::SPACE) {
    macro->replacement.pop_back();
  }
}

int CppParser::ReadCondition() {
  // 1. Reads tokens while replacing "defined" expression.
  ArrayTokenList tokens;
  Token token(NextToken(true));
  for (;;) {
    if (token.type == Token::END || token.type == Token::NEWLINE) {
      break;
    }
    if (token.type == Token::IDENTIFIER && token.string_value == "defined") {
      token = Token(static_cast<int>(IsMacroDefined(ReadDefined())));
    }
    tokens.push_back(std::move(token));
    token = NextToken(false);
  }

  // 2. Expands macros.
  ArrayTokenList expanded;
  Expand0(tokens, &expanded, true);

  // 3. Evaluates the expanded integer constant expression.
  IntegerConstantEvaluator evaluator(expanded, this);
  return evaluator.GetValue();
}

bool CppParser::FastGetMacroArgument(const ArrayTokenList& input_tokens,
                                     bool skip_space,
                                     ArrayTokenList::const_iterator* iter,
                                     ArrayTokenList* arg) {
  // |*iter| is just after '(' or ','.

  while (*iter != input_tokens.end() && (*iter)->type == Token::SPACE) {
    ++(*iter);
  }

  int paren_depth = 0;
  while (*iter != input_tokens.end()) {
    if (paren_depth == 0 &&
        ((*iter)->IsPuncChar(',') || (*iter)->IsPuncChar(')'))) {
      break;
    }
    if ((*iter)->type != Token::SPACE || !skip_space) {
      arg->push_back(**iter);
    }
    if ((*iter)->IsPuncChar('(')) {
      ++paren_depth;
    } else if ((*iter)->IsPuncChar(')')) {
      --paren_depth;
    }
    ++*iter;
  }

  // |*iter| is just ',' or ')'.
  return paren_depth == 0 && *iter != input_tokens.end();
}

bool CppParser::FastGetMacroArguments(const ArrayTokenList& input_tokens,
                                      bool skip_space,
                                      ArrayTokenList::const_iterator* iter,
                                      std::vector<ArrayTokenList>* args) {
  auto iter_backup = *iter;
  // |*iter| is  macro identifier.
  DCHECK((*iter)->type == Token::IDENTIFIER) << (*iter)->DebugString();
  ++*iter;

  // skip space between macro identifier and '('.
  while (*iter != input_tokens.end() && (*iter)->type == Token::SPACE) {
    ++*iter;
  }

  if (*iter == input_tokens.end() || !(*iter)->IsPuncChar('(')) {
    // This case happens in valid below input.
    // #define f(x)
    // f
    *iter = iter_backup;
    return false;
  }

  // consume '('.
  ++*iter;

  while (*iter != input_tokens.end() && !(*iter)->IsPuncChar(')')) {
    if ((*iter)->IsPuncChar(',')) {
      ++*iter;
    }
    ArrayTokenList arg;
    if (!FastGetMacroArgument(input_tokens, skip_space, iter, &arg)) {
      LOG(WARNING) << "Failed to get FastGetMacroArgument: "
                   << DebugString(TokenList(input_tokens.begin(),
                                            input_tokens.end()));
      *iter = iter_backup;
      return false;
    }

    args->push_back(std::move(arg));
  }

  if (*iter == input_tokens.end() ||
      !(*iter)->IsPuncChar(')')) {
    LOG(WARNING) << "Failed to find close paren of function macro call: "
                   << DebugString(TokenList(input_tokens.begin(),
                                            input_tokens.end()));
    *iter = iter_backup;
    return false;
  }
  ++iter;

  // |*iter| is just after function close paren.
  return true;
}

// FastExpand tries one step macro expansion for macros
// which simple replacements are sufficient.
// For example,
// ```
// 1: #define A B  <macro id 1>
// 2: #define B C  <macro id 2>
// 3: #define C 1  <macro id 3>
// 4: #if A
// 5: #endif
// ```
// In line 4, we pass [IDENT("A")] as |input_tokens| to FastExpand,
// and obtain [BEGIN_HIDE(1), IDENT(B), END_HIDE(1)] as |output_tokens|.
// We apply FastExpand for prevous |output_tokens| and obtain
// [BEGIN_HIDE(1), BEGIN_HIDE(2), IDENT(C), END_HIDE(2), END_HIDE(1)]
// finally obtain
// [BEGIN_HIDE(1), BEGIN_HIDE(2), NUMBER(1), END_HIDE(2), END_HIDE(1)].
//
// This function returns true if macro expansion happened.
// This function fails to expand macro in following cases.
// * macro containing '#', '##' or '__VA_ARGS__' tokens.
// * macro containing "defined".
//   top level "defined" in #if direcitve is processed in
//   ReadConditionWithCheckingIncludeGuard function.
// In these cases, we need to fallback to normal expansion.
//
// If we have recursive macros like below
// ```
// 1: #define A B  <macro id 1>
// 2: #define B A  <macro id 2>
// 3: #if A
// 4: #endif
// ```
// Macro `A` is expanded like
// [IDENT("A")] -> [BEGIN_HIDE(1), IDENT("B"), END_HIDE(1)]
//   ->  [BEGIN_HIDE(1), BEGIN_HIDE(2), IDENT("A"), END_HIDE(2), END_HIDE(1)]
// Then we detect recursion by BEGIN_HIDE(1),
// so IDENT("A") is not replaced here.
// TODO; Migrate slowpath to this vector only fastpath.
bool CppParser::FastExpand(const ArrayTokenList& input_tokens, bool skip_space,
                           std::set<int>* hideset,
                           ArrayTokenList* output_tokens,
                           bool* need_fallback) {
  // TODO: handle these fallback case
  for (const auto& token : input_tokens) {
    if (token.type == Token::SHARP ||
        token.type == Token::DOUBLESHARP ||
        token.type == Token::MACRO_PARAM_VA_ARGS) {
      *need_fallback = true;
      return false;
    }
  }

  bool replaced = false;

  for (auto iter = input_tokens.begin();
       iter != input_tokens.end(); ++iter) {

    const auto& token = *iter;

    if (token.type == Token::BEGIN_HIDE) {
      hideset->insert(token.v.int_value);
      output_tokens->push_back(token);
      continue;
    } else if (token.type == Token::END_HIDE) {
      hideset->erase(token.v.int_value);
      output_tokens->push_back(token);
      continue;
    }

    if (skip_space && token.type == Token::SPACE) {
      continue;
    }

    if (token.type != Token::IDENTIFIER) {
      output_tokens->push_back(token);
      continue;
    }

    if (token.string_value == "defined") {
      // TODO: handle defined
      *need_fallback = true;
      return replaced;
    }

    const auto* macro = GetMacro(token.string_value, false);
    if (macro == nullptr || hideset->find(macro->id) != hideset->end()) {
      output_tokens->push_back(token);
      continue;
    }

    if (macro->type != Macro::FUNC &&
        macro->type != Macro::OBJ &&
        macro->type != Macro::CBK_FUNC &&
        macro->type != Macro::UNDEFINED &&
        macro->type != Macro::UNUSED) {
      // TODO: handle other macros if necessary.
      *need_fallback = true;
      return replaced;
    }

    if (macro->type == Macro::OBJ) {
      replaced = true;
      if (macro->has_identifier_in_replacement) {
        output_tokens->push_back(Token(Token::BEGIN_HIDE, macro->id));
      }
      for (const auto& token : macro->replacement) {
        if (skip_space && token.type == Token::SPACE) {
          continue;
        }
        output_tokens->push_back(token);
      }
      if (macro->has_identifier_in_replacement) {
        output_tokens->push_back(Token(Token::END_HIDE, macro->id));
      }
      continue;
    }

    std::vector<ArrayTokenList> args;
    if (!FastGetMacroArguments(input_tokens, skip_space, &iter, &args)) {
      if (macro->type == Macro::CBK_FUNC) {
        // CBK_FUNC should not be illegal form.
        *need_fallback = true;
        return replaced;
      }
      output_tokens->push_back(token);
      continue;
    }

    if (macro->type == Macro::CBK_FUNC) {
      if (args.size() != 1) {
        // number of arguments of CBK_FUNC should be 1.
        *need_fallback = true;
        return replaced;
      }
      replaced = true;
      output_tokens->push_back((this->*(macro->callback_func))(args[0]));
      continue;
    }

    DCHECK(macro->type == Macro::FUNC)
        << macro->DebugString(this, token.string_value);

    if (!macro->is_vararg && args.size() != macro->num_args) {
      *need_fallback = true;
      return replaced;
    }

    // #define x(a, b, ...) is treated as macro->num_args = 3
    // and '...' can be empty.
    if (macro->is_vararg && args.size() + 1 < macro->num_args) {
      *need_fallback = true;
      return replaced;
    }

    replaced = true;
    if (macro->has_identifier_in_replacement) {
      output_tokens->push_back(Token(Token::BEGIN_HIDE, macro->id));
    }

    for (const auto& token : macro->replacement) {

      if (skip_space && token.type == Token::SPACE) {
        continue;
      }

      if (token.type == Token::MACRO_PARAM_VA_ARGS) {
        for (size_t i = macro->num_args; i < args.size(); ++i) {
          if (i > macro->num_args) {
            output_tokens->push_back(Token(Token::PUNCTUATOR, ','));
          }
          ArrayTokenList expanded_arg;
          replaced |= FastExpand(args[i], skip_space, hideset,
                                 &expanded_arg, need_fallback);
          if (*need_fallback) {
            return replaced;
          }
          output_tokens->insert(
              output_tokens->end(), expanded_arg.begin(), expanded_arg.end());
        }
        continue;
      }

      if (token.type != Token::MACRO_PARAM) {
        output_tokens->push_back(token);
        continue;
      }

      // need to expand arg before inserting.
      ArrayTokenList expanded_arg;
      replaced |= FastExpand(args[token.v.param_index], skip_space, hideset,
                             &expanded_arg, need_fallback);
      if (*need_fallback) {
        return replaced;
      }
      output_tokens->insert(
          output_tokens->end(), expanded_arg.begin(), expanded_arg.end());
    }

    if (macro->has_identifier_in_replacement) {
      output_tokens->push_back(Token(Token::END_HIDE, macro->id));
    }
  }
  return replaced;
}

bool CppParser::Expand0Fastpath(const ArrayTokenList& input_tokens,
                                bool skip_space,
                                ArrayTokenList* output_tokens) {
  bool need_fallback = false;
  ArrayTokenList cur_tokens(input_tokens);
  for (;;) {
    std::set<int> hide_set;
    ArrayTokenList replaced_tokens;
    bool replace_happened = FastExpand(
        cur_tokens, skip_space, &hide_set, &replaced_tokens, &need_fallback);
    if (need_fallback) {
      break;
    }

    cur_tokens.swap(replaced_tokens);

    if (!replace_happened) {
      break;
    }
  }

  if (need_fallback) {
    GOMA_COUNTERZ("fallback");
    return false;
  }

  GOMA_COUNTERZ("simple replace");
  cur_tokens.erase(
      std::remove_if(
          cur_tokens.begin(), cur_tokens.end(),
          [](const Token& t) {
            return (t.type == Token::BEGIN_HIDE ||
                    t.type == Token::END_HIDE);
          }),
      cur_tokens.end());

  output_tokens->swap(cur_tokens);
  return true;
}

// Macro expansion code.
// Most of the code below is a naive implementation of the published algorithm
// described in http://www.spinellis.gr/blog/20060626/.
void CppParser::Expand0(const ArrayTokenList& input_tokens,
                        ArrayTokenList* output_tokens,
                        bool skip_space) {
  // If simple replacement is sufficient for expansion,
  // does not call heavy Expand function.
  if (Expand0Fastpath(input_tokens, skip_space, output_tokens)) {
    return;
  }
  TokenList input_list(input_tokens.begin(), input_tokens.end());
  MacroSetList hs_input, hs_output;
  hs_input.assign(input_list.size(), MacroSet());

  TokenList output_list;
  VLOG(2) << DebugStringPrefix() << " Expand: " << DebugString(input_list);
  MacroExpandContext input(&input_list, &hs_input);
  MacroExpandContext output(&output_list, &hs_output);
  Expand(&input, input.Begin(), &output, output.Begin(), skip_space, true);
  VLOG(2) << DebugStringPrefix() << " Expanded: "
          << DebugString(output_list);

  output_tokens->assign(output_list.begin(), output_list.end());
}

void CppParser::Expand(
    MacroExpandContext* input, MacroExpandIterator input_iter,
    MacroExpandContext* output, const MacroExpandIterator output_iter,
    bool skip_space,
    bool use_hideset) {
  DCHECK(output);

  while (input_iter != input->End()) {
    MacroExpandIterator cur_input_iter = input_iter;
    const Token& token = input_iter.token();
    const MacroSet& hide_set = use_hideset ? input_iter.hide_set() : MacroSet();
    ++input_iter;

    DCHECK_NE(token.type, Token::BEGIN_HIDE);
    DCHECK_NE(token.type, Token::END_HIDE);

    if (token.type == Token::END) {
      return;
    }
    if (token.type != Token::IDENTIFIER) {
      if (token.type != Token::SPACE || !skip_space)
        output->Insert(output_iter, token, hide_set);
      continue;
    }

    VLOG(3) << " Expanding:" << DebugString(cur_input_iter.iter(),
                                            input->End().iter())
            << " token:" << token.DebugString();

    // Handle "defined" before expanding macros.
    if (token.string_value == "defined" &&
        (!is_vc_ || input_iter.token().type == Token::SPACE)) {
      bool has_paren = false;
      if (input_iter != input->End() &&
          (input_iter.token().IsPuncChar('(') ||
           input_iter.token().type == Token::SPACE)) {
        has_paren = input_iter.token().IsPuncChar('(');
        // For now, we only output this warning for "defined(foo)".
        // This is because 1. for "defined foo", the bahavior of gcc
        // and vc++ is same and 2. WebKit is using "defined foo" in
        // its core library, so this can be a bit too noisy.
        if (has_paren) {
          if (compiler_info_ == nullptr ||
              !compiler_info_->IsSystemInclude(this->input()->filepath())) {
            LOG(WARNING)
                << DebugStringPrefix()
                << " Using \"defined\" in macro causes undefined behavior. "
                << "See b/6533195";
          }
        }
        ++input_iter;
      }
      if (input_iter.token().type != Token::IDENTIFIER) {
        Error("macro names must be identifiers");
        return;
      }
      int defined = (GetMacro(input_iter.token().string_value, true)
                     != nullptr);
      ++input_iter;
      if (has_paren && input_iter != input->End() &&
          input_iter.token().IsPuncChar(')')) {
        ++input_iter;
      }
      output->Insert(output_iter, Token(defined), hide_set);
      continue;
    }

    // Case 1. input[0] is not a macro or in input[0]'s hide_set.
    const string& name = token.string_value;
    Macro* macro = GetMacro(name, false);
    if (macro == nullptr || hide_set.Get(macro->id)) {
      VLOG(4) << "expanding 1:" << token.DebugString();
      output->Insert(output_iter, token, hide_set);
      continue;
    }

    // Case 2. input[0] is an object-like macro ("()-less macro").
    if (macro->type == Macro::OBJ) {
      VLOG(4) << "expanding 2:" << macro->DebugString(this, name);
      MacroSet hs = hide_set;
      if (use_hideset) {
        hs.Set(macro->id);
      }
      input_iter = Substitute(macro->replacement, macro->num_args,
                              ArrayArgList(), hs,
                              input, input_iter, skip_space, use_hideset);
      continue;
    }

    // Case 2'. input[0] is a callback macro.
    if (macro->type == Macro::CBK) {
      VLOG(4) << "expanding 2':" << macro->DebugString(this, name);
      CHECK(macro->callback);
      Token result = (this->*(macro->callback))();
      output->Insert(output_iter, result, hide_set);
      continue;
    }

    // Case 3. input[0] is a function-like macro ("()'d macro").
    if (macro->type == Macro::FUNC) {
      VLOG(4) << "expanding 3:" << macro->DebugString(this, name);
      ArrayArgList args;
      MacroSet rparen_hs;
      if (GetMacroArguments(name, macro, &args, *input, &input_iter,
                            &rparen_hs)) {
        MacroSet hs = hide_set;
        if (macro->is_vararg) {
          use_hideset = false;
        }
        if (use_hideset) {
          hs.Union(rparen_hs);
          hs.Set(macro->id);
        }
        input_iter = Substitute(macro->replacement, macro->num_args,
                                args, hs, input, input_iter,
                                skip_space, use_hideset);
        continue;
      } else {
        VLOG(3) << "failed to get macro argument:" << token.DebugString();
      }
    }

    // Case 3'. input[0] is a function-like callback macro.
    if (macro->type == Macro::CBK_FUNC) {
      VLOG(4) << "expanding 3':" << macro->DebugString(this, name);
      // Get callback macro arguments.
      if (!SkipUntilBeginMacroArguments(name, *input, &input_iter)) {
        continue;
      }
      ArrayTokenList args;
      int nest = 0;
      while (input_iter != input->End()) {
        const Token& t = input_iter.token();
        ++input_iter;
        if (t.IsPuncChar(')')) {
          if (nest-- == 0) {
            break;
          }
        } else if (t.IsPuncChar('(')) {
          nest++;
        }
        args.push_back(t);
      }
      Token result = (this->*(macro->callback_func))(args);
      output->Insert(output_iter, result, hide_set);
      continue;
    }

    // Case 4. Other cases.
    VLOG(4) << "expanding 4:" << macro->DebugString(this, name);
    output->Insert(output_iter, token, hide_set);
  }
}

// Substitute macro args, handle stringize and paste.
// subst() in http://www.spinellis.gr/blog/20060626/
CppParser::MacroExpandIterator CppParser::Substitute(
    const ArrayTokenList& replacement,
    size_t num_args,
    const ArrayArgList& args, const MacroSet& hide_set,
    MacroExpandContext* output,
    const MacroExpandIterator output_iter,
    bool skip_space,
    bool use_hideset) {
  MacroExpandIterator saved_iter = output_iter;
  --saved_iter;
  for (ArrayTokenList::const_iterator iter = replacement.begin();
       iter != replacement.end(); ) {
    const Token& token = *iter++;
    Token next;
    if (iter != replacement.end())
      next = *iter;

    // Case 1. # param
    if (token.type == Token::SHARP &&
        next.type == Token::MACRO_PARAM) {
      DCHECK(next.v.param_index < args.size());
      if (!args[next.v.param_index].empty()) {
        output->Insert(output_iter,
                       Stringize(args[next.v.param_index]),
                       hide_set);
      }
      iter++;
      continue;
    }

    // Case 2. ## param
    if (token.type == Token::DOUBLESHARP &&
        next.type == Token::MACRO_PARAM) {
      const TokenList& arg = args[next.v.param_index];
      if (!arg.empty()) {
        TokenList::const_iterator arg_iter = arg.begin();
        Glue(output_iter.iter(), *arg_iter++);
        output->Insert(output_iter, arg_iter, arg.end(), hide_set);
      }
      iter++;
      continue;
    }

    // Case 3. ## token <remainder>
    if (token.type == Token::DOUBLESHARP &&
        next.type == Token::IDENTIFIER) {
      Glue(output_iter.iter(), next);
      iter++;
      continue;
    }

    // Case 4. param ## <remainder>
    if (token.type == Token::MACRO_PARAM &&
        next.type == Token::DOUBLESHARP) {
      const TokenList& arg = args[token.v.param_index];
      if (arg.empty()) {
        iter++;
        if (iter != replacement.end() &&
            iter->type == Token::MACRO_PARAM) {
          const TokenList& arg2 = args[iter->v.param_index];
          output->Insert(output_iter, arg2.begin(), arg2.end(), hide_set);
          iter++;
        }
      } else {
        // ## is processed in the next iteration.
        output->Insert(output_iter, arg.begin(), arg.end(), hide_set);
      }
      continue;
    }

    // Case 5. param <remainder>
    if (token.type == Token::MACRO_PARAM) {
      TokenList arg = args[token.v.param_index];
      MacroSetList hs_input;
      hs_input.assign(arg.size(), MacroSet());
      MacroExpandContext input(&arg, &hs_input);
      MacroSetList::iterator saved_hs_iter = output_iter.hs_iter();
      --saved_hs_iter;
      Expand(&input, input.Begin(), output, output_iter,
             skip_space, use_hideset);
      // Add hide set to the tokens added by the Expand.
      for (MacroSetList::iterator hs_iter = ++saved_hs_iter;
           hs_iter != output_iter.hs_iter(); ++hs_iter) {
        hs_iter->Union(hide_set);
      }
      continue;
    }

    // Case 6. __VA_ARGS__ <remainder>
    if (token.type == Token::MACRO_PARAM_VA_ARGS) {
      TokenList arg = args[num_args];
      MacroSetList hs_input;
      hs_input.assign(arg.size(), MacroSet());
      MacroExpandContext input(&arg, &hs_input);
      MacroSetList::iterator saved_hs_iter = output_iter.hs_iter();
      --saved_hs_iter;
      Expand(&input, input.Begin(), output, output_iter, skip_space, false);
      // Add hide set to the tokens added by the Expand.
      for (MacroSetList::iterator hs_iter = ++saved_hs_iter;
           hs_iter != output_iter.hs_iter(); ++hs_iter) {
        hs_iter->Union(hide_set);
      }
      continue;
    }

    // Case 7. Other cases.
    output->Insert(output_iter, token, hide_set);
  }
  ++saved_iter;
  VLOG(3) << "substitute:=>"
          << DebugString(saved_iter.iter(), output->End().iter());
  return saved_iter;
}

// Paste the last of |list| with the |token|.
// glue() in http://www.spinellis.gr/blog/20060626/
void CppParser::Glue(TokenList::iterator left_pos, const Token& right) {
  // TODO: Misc chars can also generate a new token (e.g. '|', '|'
  // -> "||").
  Token& left = *(--left_pos);
  left.Append(right.GetCanonicalString());
}

// Sringize the given token list.
// stringize() in http://www.spinellis.gr/blog/20060626/
CppParser::Token CppParser::Stringize(const TokenList& list) {
  Token output(Token::STRING);
  for (const auto& token : list) {
    if (token.type == Token::STRING) {
      string temp;
      temp += "\"";
      for (size_t i = 0; i < token.string_value.length(); ++i) {
        char c = token.string_value[i];
        if (c == '\\' || c == '"') {
          temp += '\\';
        }
        temp += c;
      }
      temp += "\"";
      output.Append(temp);
    } else {
      output.Append(token.GetCanonicalString());
    }
  }
  return output;
}

bool CppParser::SkipUntilBeginMacroArguments(
    const string& macro_name,
    const MacroExpandContext& input,
    MacroExpandIterator* iter) {
  bool ok = true;
  if (*iter != input.End() && iter->token().type == Token::SPACE) {
    ++(*iter);
  }
  if (*iter == input.End() || !iter->token().IsPuncChar('(')) {
    // Macro invoked without arguments.
    Error("macro is referred without any arguments:", macro_name);
    ok = false;
  } else {
    ++(*iter);
  }
  return ok;
}

// Get macro arguments using the comma tokens as delimiters.
// Arguments in nested parenthesis pairs are parsed in nested token lists.
// Returns a vector of token lists.
// e.g. macro(a1, a2(b1, b2), a3, a4(c1(d)))
//  --> [[a1], [a2, '(', b1, b2, ')'], [a3], [a4, '(', c1, '(', d, ')', ')']]
bool CppParser::GetMacroArguments(
    const std::string& macro_name, Macro* macro, ArrayArgList* args,
    const MacroExpandContext& input, MacroExpandIterator* iter,
    MacroSet* rparen_hs) {
  DCHECK(macro);
  if (!SkipUntilBeginMacroArguments(macro_name, input, iter)) {
    return false;
  }

  int nest = 0;
  bool ok = true;
  TokenList list;
  while (*iter != input.End()) {
    const Token& token = iter->token();
    const MacroSet& hide_set = iter->hide_set();
    ++(*iter);
    if (token.IsPuncChar(',')) {
      if (nest == 0) {
        args->push_back(list);
        VLOG(3) << "macro:" << macro_name << " found ,"
                << " nest=0" << " args=" << args->size();
        list = TokenList();
      } else {
        list.push_back(token);
      }
      if (*iter != input.End() && iter->token().type == Token::SPACE) {
        ++(*iter);
      }
      continue;
    }
    if (token.IsPuncChar(')')) {
      if (nest-- == 0) {
        args->push_back(list);
        VLOG(3) << "macro:" << macro_name << " found )"
                << " nest=0" << " args=" << args->size();
        *rparen_hs = hide_set;
        break;
      }
    } else if (token.IsPuncChar('(')) {
      nest++;
    }
    list.push_back(token);
  }
  // FOO() case.
  if (macro->num_args == 0U && args->size() == 1U && args->front().empty()) {
    args->clear();
  }
  // FOO() is valid for macro FOO(x).
  if (macro->num_args == 1U && args->size() == 0U) {
    // Push empty string token.
    list.clear();
    list.push_back(Token(Token::STRING));
    args->push_back(list);
  }
  if (!macro->is_vararg && macro->num_args != args->size()) {
    Error("macro argument number mismatching with the parameter list");
    VLOG(3) << "macro:" << macro_name
            << " want args:" << macro->num_args
            << " got args:" << args->size();
    ok = false;
  }
  if (macro->is_vararg) {
    list.clear();
    for (size_t i = macro->num_args; i < args->size(); ++i) {
      list.insert(list.end(), args->at(i).begin(), args->at(i).end());
      if (i != args->size() - 1) {
        list.push_back(Token(Token::PUNCTUATOR, ','));
      }
    }
    args->resize(macro->num_args);
    args->push_back(list);
  }
  return ok;
}

std::pair<Macro*, bool> CppParser::AddMacro(
    const string& name, Macro::Type type,
    const FileId& fileid, size_t macro_pos) {
  if (predefined_macros_->find(name) != predefined_macros_->end() ||
      predefined_func_macros_->find(name) != predefined_func_macros_->end()) {
    Error("redefining predefined macro ", name);
  }
  return AddMacroInternal(name, type, fileid, macro_pos);
}

std::pair<Macro*, bool> CppParser::AddMacroInternal(
    const string& name, Macro::Type type,
    const FileId& fileid, size_t macro_pos) {
  DCHECK(!name.empty()) << "Adding a macro that does not have a name.";

  {
    auto it = macros_->find(name);
    if (it != macros_->end()) {
      if (it->second.IsMatch(fileid, macro_pos)) {
        it->second.type = type;
        it->second.id = next_macro_id_++;
        used_macros_.push_back(&it->second);
        return std::make_pair(&it->second, true);
      }

      if (it->second.type != Macro::UNDEFINED &&
          it->second.type != Macro::UNUSED) {
        Error("macro is already defined:", name);
      }

      it->second = Macro(next_macro_id_++, type);
      it->second.fileid = fileid;
      it->second.macro_pos = macro_pos;
      used_macros_.push_back(&it->second);
      return std::make_pair(&it->second, false);
    }
  }

  std::pair<unordered_map<string, Macro>::iterator, bool> result =
      macros_->emplace(name, Macro(next_macro_id_++, type));
  result.first->second.fileid = fileid;
  result.first->second.macro_pos = macro_pos;
  used_macros_.push_back(&result.first->second);
  return std::make_pair(&result.first->second, false);
}


Macro* CppParser::GetMacro(const string& name, bool add_undefined) {
  unordered_map<string, Macro>::iterator found = macros_->find(name);
  if (found == macros_->end() || found->second.type == Macro::UNUSED) {
    // Check predefined macros.
    {
      PredefinedObjMacroMap::const_iterator found_predefined =
        predefined_macros_->find(name);
      if (found_predefined != predefined_macros_->end() &&
          IsEnabledPredefinedMacro(found_predefined->first)) {
        Macro* macro = AddMacroInternal(name, Macro::CBK, FileId(), 0).first;
        macro->callback = found_predefined->second;
        return macro;
      }
    }
    // Check predefined macros.
    {
      PredefinedFuncMacroMap::const_iterator found_predefined_func =
        predefined_func_macros_->find(name);
      if (found_predefined_func != predefined_func_macros_->end() &&
          IsEnabledPredefinedMacro(found_predefined_func->first)) {
        Macro* macro = AddMacroInternal(
            name, Macro::CBK_FUNC, FileId(), 0).first;
        macro->callback_func = found_predefined_func->second;
        return macro;
      }
    }
    // "true" and "false" are C++ reserved keyword.
    // No need to treat them as undefined if C++.
    if (is_cplusplus_ &&
        (name == "true" || name == "false")) {
      return nullptr;
    }
    // No macros found for the given name.
    if (add_undefined)
      AddMacro(name, Macro::UNDEFINED, FileId(), 0);
    return nullptr;
  }

  if (found->second.type == Macro::UNDEFINED) {
    return nullptr;
  }
  return &found->second;
}

void CppParser::PopInput() {
  DCHECK(HasMoreInput());

  std::unique_ptr<Input> current = std::move(inputs_.back());
  inputs_.pop_back();

  current->include_guard_detector()->OnPop();
  if (!current->filepath().empty() &&
      current->include_guard_detector()->IsGuardDetected() &&
      IsMacroDefined(current->include_guard_detector()->detected_ident())) {
    include_guard_ident_[current->filepath()] =
        current->include_guard_detector()->detected_ident();
  }

  last_input_ = std::move(current);
}

bool CppParser::IsProcessedFileInternal(const string& path,
                                        int include_dir_index) {
  VLOG(2) << "IsProcessedFileInternal:"
          << " path=" << path
          << " include_dir_index=" << include_dir_index;
  // Check if this file is in the pragma_once history.
  if (pragma_once_fileset_.Has(path)) {
    VLOG(1) << "Skipping " << path << " for pragma once";
    return true;
  }

  const auto& iter = include_guard_ident_.find(path);
  if (iter == include_guard_ident_.end()) {
    return false;
  }
  if (IsMacroDefined(iter->second)) {
    VLOG(1) << "Skipping " << path << " for include guarded by "
            << iter->second;
    return true;
  }
  return false;
}

CppParser::Token CppParser::GetFileName() {
  Token token(Token::STRING);
  token.Append(input()->filepath());
  return token;
}

CppParser::Token CppParser::GetLineNumber() {
  Token token(Token::NUMBER);
  // We always evaluate macros after reading the line until the end,
  // so the line number needs to be subtracted by 1.
  token.v.int_value = input()->stream()->line() - 1;
  token.Append(std::to_string(token.v.int_value));
  return token;
}

CppParser::Token CppParser::GetDate() {
  Token token(Token::STRING);
  token.Append(current_date_);
  return token;
}

CppParser::Token CppParser::GetTime() {
  Token token(Token::STRING);
  token.Append(current_time_);
  return token;
}

CppParser::Token CppParser::GetCounter() {
  return Token(counter_++);
}

CppParser::Token CppParser::GetBaseFile() {
  Token token(Token::STRING);
  token.Append(base_file_);
  return token;
}

CppParser::Token CppParser::ProcessHasInclude(const ArrayTokenList& tokens) {
  return Token(static_cast<int>(ProcessHasIncludeInternal(tokens, false)));
}

CppParser::Token CppParser::ProcessHasIncludeNext(
    const ArrayTokenList& tokens) {
  return Token(static_cast<int>(ProcessHasIncludeInternal(tokens, true)));
}

bool CppParser::ProcessHasIncludeInternal(const ArrayTokenList& tokens,
                                          bool is_include_next) {
  if (tokens.empty()) {
    Error("__has_include expects \"filename\" or <filename>");
    return false;
  }

  ArrayTokenList tokenlist(tokens.begin(), tokens.end());
  ArrayTokenList expanded;
  Expand0(tokenlist, &expanded, false);
  if (expanded.empty()) {
    Error("__has_include expects \"filename\" or <filename>");
    return false;
  }

  Token token = expanded.front();
  if (token.type == Token::LT) {
    string path;
    auto iter = expanded.begin();
    ++iter;
    for (; iter != expanded.end() && iter->type != Token::GT; ++iter) {
      path.append(iter->GetCanonicalString());
    }
    VLOG(1) << DebugStringPrefix() << "HAS_INCLUDE(<" << path << ">)";
    if (include_observer_) {
      return include_observer_->HasInclude(
          path, input()->directory(), input()->filepath(),
          '<',
          is_include_next ? (input()->include_dir_index() + 1) :
          bracket_include_dir_index_);
    }
    return false;
  }
  if (token.type == Token::STRING) {
    VLOG(1) << DebugStringPrefix() << "HAS_INCLUDE(" << token.string_value
            << ")";
    if (include_observer_) {
      return include_observer_->HasInclude(
          token.string_value, input()->directory(), input()->filepath(),
          is_include_next ? '<' : '"',
          is_include_next ? (input()->include_dir_index() + 1) :
          input()->include_dir_index());
    }
    return false;
  }
  Error("__has_include expects \"filename\" or <filename>");
  return false;
}

CppParser::Token CppParser::ProcessHasCheckMacro(
    const string& name,
    const ArrayTokenList& tokens,
    const unordered_map<string, int>& has_check_macro) {
  if (tokens.empty()) {
    Error(name + " expects an identifier");
    return Token(0);
  }

  ArrayTokenList token_list(tokens.begin(), tokens.end());
  ArrayTokenList expanded;
  Expand0(token_list, &expanded, true);
  if (expanded.empty()) {
    Error(name + " expects an identifier");
    return Token(0);
  }

  // Let's consider "__has_cpp_attribute(clang::fallthrough)".
  // Here, token list is like "clang" ":" ":" "fallthrough".
  //
  // TODO: what happens
  //   1. if space is inserted between tokens?
  //   2. if clang or fallthrough is defined somwhere?
  //
  // b/71611716

  string ident;
  if (expanded.size() > 1) {
    // Concat the expanded tokens. Allow only ident or ':'.
    for (const auto& t : expanded) {
      if (t.type == Token::IDENTIFIER) {
        ident += t.string_value;
      } else if (t.IsPuncChar(':')) {
        ident += ':';
      } else {
        Error(name + " expects an identifier");
        return Token(0);
      }
    }
  } else {
    Token token = expanded.front();
    if (token.type != Token::IDENTIFIER) {
      Error(name + " expects an identifier");
      return Token(0);
    }
    ident = token.string_value;
  }

  // Normalize the extension identifier.
  // '__feature__' is normalized to 'feature' in clang.
  if (ident.size() >= 4 && strings::StartsWith(ident, "__")
      && strings::EndsWith(ident, "__")) {
    ident.resize(ident.size() - 2);
    ident = ident.substr(2);
  }

  const auto& iter = has_check_macro.find(ident);
  if (iter == has_check_macro.end())
    return Token(0);
  return Token(iter->second);
}

void CppParser::EnablePredefinedMacro(const string& name) {
  enabled_predefined_macros_.insert(std::make_pair(name, false));
}

bool CppParser::IsHiddenPredefinedMacro(const string& name) const {
  const auto& found = enabled_predefined_macros_.find(name);
  if (found == enabled_predefined_macros_.end()) {
    return false;
  }
  return found->second;
}

#ifdef _WIN32
BOOL WINAPI CppParser::InitializeWinOnce(PINIT_ONCE, PVOID, PVOID*) {
  CppParser::InitializeStaticOnce();
  return TRUE;
}
#endif

void CppParser::InitializeStaticOnce() {
  DCHECK(!global_initialized_);

  CppTokenizer::InitializeStaticOnce();

  // One-time assertion checks to see the static values auto-generated by
  // generate_static_darray.py are initialized as expected.
  const DirectiveHandler* table = kDirectiveTable;
  DCHECK(table[kDirectiveInclude] == &CppParser::ProcessInclude);
  DCHECK(table[kDirectiveImport] == &CppParser::ProcessImport);
  DCHECK(table[kDirectiveIncludeNext] == &CppParser::ProcessIncludeNext);
  DCHECK(table[kDirectiveDefine] == &CppParser::ProcessDefine);
  DCHECK(table[kDirectiveUndef] == &CppParser::ProcessUndef);
  DCHECK(table[kDirectiveIfdef] == &CppParser::ProcessIfdef);
  DCHECK(table[kDirectiveIfndef] == &CppParser::ProcessIfndef);
  DCHECK(table[kDirectiveIf] == &CppParser::ProcessIf);
  DCHECK(table[kDirectiveElse] == &CppParser::ProcessElse);
  DCHECK(table[kDirectiveEndif] == &CppParser::ProcessEndif);
  DCHECK(table[kDirectiveElif] == &CppParser::ProcessElif);
  DCHECK(table[kDirectivePragma] == &CppParser::ProcessPragma);

  table = kFalseConditionDirectiveTable;
  DCHECK(table[kDirectiveIfdef] == &CppParser::ProcessConditionInFalse);
  DCHECK(table[kDirectiveIfndef] == &CppParser::ProcessConditionInFalse);
  DCHECK(table[kDirectiveIf] == &CppParser::ProcessConditionInFalse);
  DCHECK(table[kDirectiveElse] == &CppParser::ProcessElse);
  DCHECK(table[kDirectiveEndif] == &CppParser::ProcessEndif);
  DCHECK(table[kDirectiveElif] == &CppParser::ProcessElif);

  typedef CppParser self;
  static const struct {
    const char* name;
    Macro::CallbackObj callback;
  } kPredefinedCallbackMacros[] = {
    { "__FILE__", &self::GetFileName },
    { "__LINE__", &self::GetLineNumber },
    { "__DATE__", &self::GetDate },
    { "__TIME__", &self::GetTime },
    { "__COUNTER__",   &self::GetCounter },
    { "__BASE_FILE__", &self::GetBaseFile },
  };
  predefined_macros_ = new PredefinedObjMacroMap;
  for (const auto& iter : kPredefinedCallbackMacros) {
    predefined_macros_->insert(std::make_pair(iter.name, iter.callback));
  }

  static const struct {
    const char* name;
    Macro::CallbackFunc callback;
  } kPredefinedCallbackFuncMacros[] = {
    { "__has_include", &self::ProcessHasInclude },
    { "__has_include__", &self::ProcessHasInclude },
    { "__has_include_next", &self::ProcessHasIncludeNext },
    { "__has_include_next__", &self::ProcessHasIncludeNext },
    { "__has_feature", &self::ProcessHasFeature },
    { "__has_extension", &self::ProcessHasExtension },
    { "__has_attribute", &self::ProcessHasAttribute },
    { "__has_cpp_attribute", &self::ProcessHasCppAttribute },
    { "__has_declspec_attribute", &self::ProcessHasDeclspecAttribute },
    { "__has_builtin", &self::ProcessHasBuiltin },
  };
  predefined_func_macros_ = new PredefinedFuncMacroMap;
  for (const auto& iter : kPredefinedCallbackFuncMacros) {
    predefined_func_macros_->insert(std::make_pair(iter.name, iter.callback));
  }

  global_initialized_ = true;
}

}  // namespace devtools_goma

#ifdef TEST

using devtools_goma::Content;
using devtools_goma::CppParser;
using devtools_goma::GetBaseDir;
using devtools_goma::GetCurrentDirNameOrDie;
using devtools_goma::PathResolver;

class TestIncludeObserver : public CppParser::IncludeObserver {
 public:
  bool HandleInclude(
      const string& path,
      const string& current_directory ALLOW_UNUSED,
      const string& current_filepath ALLOW_UNUSED,
      char quote_char ALLOW_UNUSED,  // '"' or '<'
      int include_dir_index ALLOW_UNUSED) override {
    if (quote_char == '<' &&
        include_dir_index > CppParser::kIncludeDirIndexStarting) {
      std::cout << "#INCLUDE_NEXT ";
    } else {
      std::cout << "#INCLUDE ";
    }
    char close_quote_char = (quote_char == '<') ? '>' : quote_char;
    std::cout << quote_char << path << close_quote_char << std::endl;
#ifdef _WIN32
    UNREFERENCED_PARAMETER(current_directory);
    UNREFERENCED_PARAMETER(current_filepath);
#endif
    return true;
  }

  bool HasInclude(
      const string& path ALLOW_UNUSED,
      const string& current_directory ALLOW_UNUSED,
      const string& current_filepath ALLOW_UNUSED,
      char quote_char ALLOW_UNUSED,  // '"' or '<'
      int include_dir_index ALLOW_UNUSED) override {
#ifdef _WIN32
    UNREFERENCED_PARAMETER(path);
    UNREFERENCED_PARAMETER(current_directory);
    UNREFERENCED_PARAMETER(current_filepath);
    UNREFERENCED_PARAMETER(quote_char);
    UNREFERENCED_PARAMETER(include_dir_index);
#endif
    return true;
  }
};

class TestErrorObserver : public CppParser::ErrorObserver {
 public:
  void HandleError(const string& error) override {
    LOG(WARNING) << error;
  }
};

static bool TryAddFileInput(CppParser* parser, const string& filepath,
                            int include_dir_index) {
  std::unique_ptr<Content> fp(Content::CreateFromFile(filepath));
  if (!fp) {
    return false;
  }
  devtools_goma::FileId fileid(filepath);
  string directory;
  GetBaseDir(filepath, &directory);
  parser->AddFileInput(std::move(fp), fileid, filepath, directory,
                       include_dir_index);
  return true;
}

static std::pair<string, string> GetMacroArg(const char* arg) {
  string macro(arg);
  size_t found = macro.find('=');
  if (found == string::npos) {
    return std::make_pair(macro, "");
  }
  const string& key = macro.substr(0, found);
  const string& value = macro.substr(found + 1, macro.size() - (found + 1));
  return std::make_pair(key, value);
}

int main(int argc, char *argv[]) {
  int ac = 1;
  std::vector<std::pair<string, string>> arg_macros;
  for (; ac < argc; ++ac) {
    if (strncmp(argv[ac], "-D", 2) == 0) {
      if (strlen(argv[ac]) > 2) {
        arg_macros.push_back(GetMacroArg(&argv[ac][2]));
      } else if (ac + 1 < argc) {
        arg_macros.push_back(GetMacroArg(argv[++ac]));
      }
      continue;
    }
    break;
  }

  if (ac >= argc) {
    std::cerr << argv[0] << " [-D<macro> ...] path" << std::endl;
    std::cerr << "e.g.: " << argv[0] << " -D'S(x)=<lib##x.h>' tmp.c"
              << std::endl;
    exit(1);
  }

  const string cwd = GetCurrentDirNameOrDie();

  PathResolver path_resolver;

  string path = file::JoinPathRespectAbsolute(cwd, argv[ac]);
  path = path_resolver.ResolvePath(path);

  std::cout << std::endl << "===== Tokens =====" << std::endl;
  {
    CppParser parser;
    TryAddFileInput(&parser, path, CppParser::kCurrentDirIncludeDirIndex);
    for (;;) {
      CppParser::Token token = parser.NextToken(false);
      if (token.type == CppParser::Token::END) {
        break;
      }
      std::cout << token.DebugString();
    }
  }

  {
    CppParser parser;
    TestIncludeObserver include_observer;
    TestErrorObserver error_observer;
    TryAddFileInput(&parser, path, CppParser::kCurrentDirIncludeDirIndex);
    parser.set_include_observer(&include_observer);
    parser.set_error_observer(&error_observer);

    for (const auto& arg_macro : arg_macros) {
      parser.AddMacroByString(arg_macro.first, arg_macro.second);
    }

    std::cout << std::endl << "===== Includes =====" << std::endl;
    parser.ProcessDirectives();

    std::cout << std::endl << "===== Macros =====" << std::endl;
    std::cout << parser.DumpMacros();
  }
}

#endif  // TEST
