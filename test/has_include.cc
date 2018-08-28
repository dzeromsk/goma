// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// If this succeeds when building locally, but fails when building with Goma,
// then Goma has probably not included the iostream header in the ExecReq.

#if !__has_include(<iostream>)
#error "__has_include(X) should include X as an input"
#endif
