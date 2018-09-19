// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

function addSortFunction(key) {
  var sorter = function(a, b) {
    const aelem = $(a).find(key);
    const belem = $(b).find(key);

    const av = aelem.data("to-compare");
    const bv = belem.data("to-compare");

    return av > bv ? -1 : 1;
  }

  $('th' + key).click(
      function() {
        var tbody = $('body > table > tbody > tr')
        var sorted_table = tbody.sort(sorter);
        $('tbody').html(sorted_table);
      });
}

function init() {
  addSortFunction('.count');
  addSortFunction('.total-wait');
  addSortFunction('.max-wait');
  addSortFunction('.ave-wait');
  addSortFunction('.total-hold');
  addSortFunction('.max-hold');
  addSortFunction('.ave-hold');

  $('th.total-wait').trigger("click");
}
