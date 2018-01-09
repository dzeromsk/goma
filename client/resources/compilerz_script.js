// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function showCompilers(json) {
  if (!('compilers' in json)) {
    console.error('No compilers found in json', json);
    return;
  }

  var compilers = json['compilers'];
  var $body = $('#compilers-body');
  for (var i = 0; i < compilers.length; ++i) {
    var c = compilers[i];

    var $name_td = $('<td>');
    if ('name' in c) {
      $name_td.text(c['name']);
    }

    var $path_td = $('<td>');
    if ('local_compiler_path' in c) {
      $path_td.text(c['local_compiler_path']);
    }

    var $version_td = $('<td>');
    if ('version' in c) {
      $version_td.text(c['version']);
    }

    var $hash_td = $('<td>');
    if ('real_compiler_hash' in c) {
      $hash_td.text(c['real_compiler_hash']);
    }

    var $tr = $('<tr>');
    $tr.append($name_td)
        .append($path_td)
        .append($version_td)
        .append($hash_td)
        .appendTo($body);
  }
}

function loadCompilers() {
  $.get('/api/compilerz', showCompilers);
}

function init() {
  loadCompilers()
}
