#!/bin/bash
#
# Copyright 2011 The Goma Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to warn not to use this script.

cat <<EOM 1>&2
****************************** ERROR ******************************

DO NOT USE goma_ctl.sh anymore. This is just rotten.

We suggest you to use goma_ctl.py or goma_stubby.sh when using stubby_proxy.

*******************************************************************
EOM

exit 1
