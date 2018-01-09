// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "threadpool_http_server.h"

#include <string>
#include <gtest/gtest.h>

using devtools_goma::ThreadpoolHttpServer;
using std::string;

namespace {

TEST(ThreadpoolHttpServerTest, TestParseRequestLineWithoutQuery) {
  static const string kRequest(
      "GET /hoge HTTP/1.1\r\n"
      "Host: hogehoge.com\r\n"
      "\r\n");
  string method, path, query;
  EXPECT_TRUE(
      ThreadpoolHttpServer::ParseRequestLine(
          kRequest, &method, &path, &query));
  EXPECT_EQ("GET", method);
  EXPECT_EQ("/hoge", path);
  EXPECT_EQ("", query);
}

TEST(ThreadpoolHttpServerTest, TestParseRequestLineWithQuery) {
  static const string kRequest(
      "GET /hoge.cgi?hoge=fugafuga HTTP/1.1\r\n"
      "Host: hogehoge.com\r\n"
      "\r\n");
  string method, path, query;
  EXPECT_TRUE(
      ThreadpoolHttpServer::ParseRequestLine(
          kRequest, &method, &path, &query));
  EXPECT_EQ("GET", method);
  EXPECT_EQ("/hoge.cgi", path);
  EXPECT_EQ("hoge=fugafuga", query);
}

TEST(ThreadpoolHttpServerTest, BrokenRequest) {
  static const string kRequest(
      "GET /hoge.cgi?hoge=fugafuga\r\n"
      "Host: hogehoge.com\r\n"
      "\r\n");
  string method, path, query;
  EXPECT_FALSE(
      ThreadpoolHttpServer::ParseRequestLine(
          kRequest, &method, &path, &query));
}

TEST(ThreadpoolHttpServerTest, BrokenRequest2) {
  // Try some request without CRLF
  static const string kRequest(
        "GET /hoge.cgi?hoge=fugafuga\n"
        "Host: hogehoge.com\n"
        "\n");
  string method, path, query;
  EXPECT_FALSE(
      ThreadpoolHttpServer::ParseRequestLine(
          kRequest, &method, &path, &query));
}

}  // namespace
