// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GOMACC_IPC_H_
#define DEVTOOLS_GOMA_CLIENT_GOMACC_IPC_H_

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

enum GomaCCCommand {
  GOMACC_CMD_COMPILE,
  GOMACC_CMD_WAIT,
  GOMACC_CMD_TERMINATE,
};

bool SendCommand(int fd, GomaCCCommand cmd);
bool ReceiveCommand(int fd, GomaCCCommand* cmd);

bool SendMessage(int sock, const google::protobuf::Message& message);
bool ReceiveMessage(int sock, google::protobuf::Message* message);

#endif  // DEVTOOLS_GOMA_CLIENT_GOMACC_IPC_H_
