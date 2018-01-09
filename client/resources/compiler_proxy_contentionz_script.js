// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

function addSortFunction(key) {
  var sorter = function(a, b) {
    var avalue = parseFloat($(a).find(key).text());
    var bvalue = parseFloat($(b).find(key).text());
    return avalue > bvalue ? -1 : 1;
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
