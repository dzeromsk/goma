// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "type.h"

namespace {

// Pretty printing helpers.
struct PrettyString {
  explicit PrettyString(const string& s) : s(s) {}

  friend std::ostream& operator<<(std::ostream& os, const PrettyString& p) {
    // TODO: escape if p.s contains '"'.
    return os << '\"' << p.s << '\"';
  }

  const string& s;
};

struct PrettyAttr {
  explicit PrettyAttr(const string& s) : s(s) {}

  friend std::ostream& operator<<(std::ostream& os, const PrettyAttr& p) {
    return os << '[' << p.s << ']';
  }

  const string& s;
};

struct PrettyIndent {
  explicit PrettyIndent(int level) : level(level) {}

  friend std::ostream& operator<<(std::ostream& os, const PrettyIndent& p) {
    for (int i = 0; i < p.level * 2; ++i) {
      os << ' ';
    }
    return os;
  }

  const int level;
};

struct PrettyVector {
  PrettyVector(const std::vector<string>& vs, const string& sep)
      : vs(vs), sep(sep) {}

  friend std::ostream& operator<<(std::ostream& os, const PrettyVector& p) {
    for (size_t i = 0; i < p.vs.size(); ++i) {
      if (i > 0) {
        os << p.sep;
      }
      os << p.vs[i];
    }
    return os;
  }

  const std::vector<string>& vs;
  const string& sep;
};

}  // anonymous namespace

namespace devtools_goma {
namespace modulemap {

void Module::PrettyPrint(std::ostream* os, int indent_level) const {
  if (is_extern_) {
    *os << PrettyIndent(indent_level) << "extern module " << module_id_ << " "
        << PrettyString(extern_filename_);
    return;
  }

  *os << PrettyIndent(indent_level);
  if (is_explicit_) {
    *os << "explicit ";
  }
  if (is_framework_) {
    *os << "framework ";
  }
  *os << "module " << module_id_ << " ";
  for (const auto& attr : attributes_) {
    *os << PrettyAttr(attr) << ' ';
  }
  *os << "{\n";

  // requires-declaration
  if (!requires_.empty()) {
    *os << PrettyIndent(indent_level + 1) << "requires";
    for (size_t i = 0; i < requires_.size(); ++i) {
      if (i != 0) {
        *os << ',';
      }
      *os << ' ';
      if (!requires_[i].is_positive()) {
        *os << '!';
      }
      *os << requires_[i].name();
    }
    *os << '\n';
  }

  // header-declaration
  for (const auto& header : headers_) {
    *os << PrettyIndent(indent_level + 1);
    if (header.is_umbrella()) {
      *os << "umbrella ";
    }
    if (header.is_exclude()) {
      *os << "exclude ";
    }
    if (header.is_private()) {
      *os << "private ";
    }
    if (header.is_textual()) {
      *os << "textual ";
    }
    *os << "header ";
    *os << PrettyString(header.name());
    if (!header.size().empty() || !header.mtime().empty()) {
      *os << " {\n";
      if (!header.size().empty()) {
        *os << PrettyIndent(indent_level + 2) << "size " << header.size()
            << '\n';
      }
      if (!header.mtime().empty()) {
        *os << PrettyIndent(indent_level + 2) << "mtime " << header.mtime()
            << '\n';
      }
      *os << PrettyIndent(indent_level + 1) << "}";
    }
    *os << '\n';
  }

  // umbrella-dir-declaration
  for (const auto& umbrella_dir : umbrella_dirs_) {
    *os << PrettyIndent(indent_level + 1) << "umbrella "
        << PrettyString(umbrella_dir) << '\n';
  }

  // submodule-declaration
  for (const auto& submodule : submodules_) {
    submodule.PrettyPrint(os, indent_level + 1);
  }

  // export-declaration
  for (const auto& e : exports_) {
    *os << PrettyIndent(indent_level + 1) << "export " << e << "\n";
  }

  // export-as-declaration
  for (const auto& e : export_as_) {
    *os << PrettyIndent(indent_level + 1) << "export_as " << e << "\n";
  }

  // use-declaration
  for (const auto& use : uses_) {
    *os << PrettyIndent(indent_level + 1) << "use " << use << "\n";
  }

  // link-declaration
  for (const auto& link : links_) {
    *os << PrettyIndent(indent_level + 1) << "link ";
    if (link.is_framework()) {
      *os << "framework ";
    }
    *os << PrettyString(link.name());
    *os << '\n';
  }

  // config-macros-declaration
  for (const auto& config_macro : config_macros_) {
    *os << PrettyIndent(indent_level + 1) << "config_macros ";
    for (const auto& attr : config_macro.attributes_) {
      *os << PrettyAttr(attr) << ' ';
    }
    *os << PrettyVector(config_macro.macros_, ", ");
  }

  // conflict-declaration
  for (const auto& conflict : conflicts_) {
    *os << PrettyIndent(indent_level + 1) << "conflict " << conflict.module_id()
        << ", " << PrettyString(conflict.reason()) << '\n';
  }

  *os << PrettyIndent(indent_level) << "}\n";
}

}  // namespace modulemap
}  // namespace devtools_goma
