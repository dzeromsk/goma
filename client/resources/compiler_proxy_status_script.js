// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

// Add starts with.
if (!String.prototype.startsWith) {
  /**
   * Returns true if the string starts with searchString.
   * @param {string} searchString The characters to be searched for at the start
   * of this string.
   * @param {number} position Optional. The position in this string at which to
   * begin searching for searchString; defaults to 0.
   * @return {bool} True if the string starts with searchString. Otherwise false.
   */
  String.prototype.startsWith = function(searchString, position) {
    position = position || 0;
    return this.lastIndexOf(searchString, position) === position;
  };
}

/**
 * Human readable bytes.
 * @param{Number} bytes.
 * @return{string} human readable bytes.
 */
function humanReadableBytes(num) {
  var sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
  if (num == 0) {
    return '0';
  }
  var i = parseInt(Math.floor(Math.log(num) / Math.log(1024)));
  if (i >= sizes.length) {
    i = sizes.length - 1;
  }
  return (Math.round(num / Math.pow(1024, i) * 100) / 100) + sizes[i];
};

/* Returns a basename of path. For example:
 * C:\Program Files (x86)\VC\bin\cl.exe --> cl.exe
 * /usr/bin/clang++                     --> clang++
 *
 * @param {string} path path.
 * @return {string} basename of path.
 */
function basename(path) {
  var splitted = path.split(/[\/\\\\]/)
  return splitted[splitted.length - 1];
}

function makeFlagSummary(flag) {
  if (!flag)
    return '';

  var args = flag.split(' ');

  var compiler = basename(args[0]);

  var target = '';
  for (var i = 0; i < args.length; ++i) {
    // Catches '-o <filename>', '/Fe <filename>', or '/Fo <filename>' cases.
    if (args[i] == '-o' || args[i] == '/Fe' || args[i] == '/Fo') {
      if (i + 1 < args.length) {
        target = basename(args[i + 1]);
        break;
      }
    }

    // Catches '-o<filename>' case.
    if (args[i].startsWith('-o')) {
      target = basename(args[i].substring(2));
      break;
    }

    // Catches '/Fe<filename>', or '/Fo<filename>' cases.
    if (args[i].startsWith('/Fe') || args[i].startsWith('/Fo')) {
      target = basename(args[i].substring(3));
      break;
    }
  }

  return compiler + ' ... ' + target;
}

/**
 * Gets "task" status.
 * @param {Object} task task object.
 * @return {string} task status
 */
function taskStatus(task) {
  // There is a case that `canceled` exists but `exit` or `http` does not
  // exists. So handle this earlier.
  if (task['canceled']) {
    return 'cancel';
  }

  // finished task must have 'replied'. Otherwise,running.
  if (!('replied' in task)) {
    return 'running';
  }

  var success = true;
  if ('exit' in task) {
    if (task['exit'] != 0) {
      success = false;
    }
  } else if ('http' in task) {
    if (task['http'] != 200) {
      success = false;
    }
  }

  if (task['compiler_proxy_error']) {
    return 'client-error'
  }
  if (task['goma_error']) {
    if (task['http'] != 200) {
      return 'http-error';
    }
    return 'backend-error'
  }
  if (!success) {
    if (task['command'] && task['command'].match(/ conftest\./)) {
      return 'conftestfailure';
    }
    return 'failure';
  }
  if (task['fail_fallback']) {
    return 'failure';
  }

  if (task['command_version_mismatch']
      || task['command_binary_hash_mismatch']
      || task['command_subprograms_mismatch']) {
    return 'mismatch';
  }

  if (task['state'] != 'FINISHED') {
    return 'local-fallback';
  }
  if (task['retry'] > 0) {
    return 'retry';
  }
  if (task['cache'] == 'local hit') {
    return 'local-cachehit';
  }
  if (task['cache'] == 'hit') {
    return 'cachehit';
  }
  return 'success';
}

// Returns true if task has compiler mismatch.
function taskHasMismatch(task) {
  var keys = ['command_version_mismatch',
              'command_binary_hash_mismatch',
              'command_subprograms_mismatch'];
  for (var i = 0; i < keys.length; ++i) {
    var key = keys[i];
    if ((key in task) && task[key] != "") {
      return true;
    }
  }

  return false;
}

function taskHasGomaccRevisionMismatch(task) {
  if ('gomacc_revision_mismatch' in task &&
      task['gomacc_revision_mismatch'] != "") {
    return true;
  }

  return false;
}
// ----------------------------------------------------------------------

function GomaTaskView() {
  this.currentTaskView = 'active';
  this.taskStartingOffset = 0;
  this.taskMaxSize = 25;
  this.tasks = {
    active: [],
    finished: [],
    failed: [],
    long: []
  };
  // id -> Task
  this.finishedTasks = {},
  this.failedTasks = {},
  // id -> Task
  this.taskDetailCache = {},
  this.currentDetailTaskId = 0;
  this.currentTaskOrderKey = null;
  this.currentTaskOrderAscending = true;

  // For Task stats
  this.maxActives = 0;
}

GomaTaskView.prototype = {
  qpsChart: null,
  trafficChart: null,

  setPageSize: function(size) {
    this.taskMaxSize = size;
  },

  updateTaskView: function() {
    var currentTasks = this._filteredTasks();
    this._updateTaskViewWith(currentTasks);
  },

  // Update compiler mismatch view.
  // We don't filter tasks here.
  updateMismatchView: function() {
    var compilerMismatchDetected = false;
    var gomaccRevisionMismatchDetected = false;
    var mismatchedTaskIdSet = {};
    for (var i = 0; i < this.tasks.finished.length; ++i) {
      var task = this.tasks.finished[i];
      if (taskHasMismatch(task)) {
        compilerMismatchDetected = true;
        mismatchedTaskIdSet[task.id] = true;
      }
      if (taskHasGomaccRevisionMismatch(task)) {
        gomaccRevisionMismatchDetected = true;
      }
    }
    for (var i = 0; i < this.tasks.failed.length; ++i) {
      var task = this.tasks.failed[i];
      if (taskHasMismatch(task)) {
        compilerMismatchDetected = true;
        mismatchedTaskIdSet[task.id] = true;
      }
      if (taskHasGomaccRevisionMismatch(task)) {
        gomaccRevisionMismatchDetected = true;
      }
    }

    var mismatchArea = $('#mismatch');
    if (!(compilerMismatchDetected || gomaccRevisionMismatchDetected)) {
      mismatchArea.hide();
      return;
    }

    mismatchArea.empty();

    if (gomaccRevisionMismatchDetected) {
      $('<div>revision between gomacc and compiler_proxy ' +
          'is mismatched. Probably goma has been updated. ' +
          'Restart compiler_proxy.</div>').appendTo(mismatchArea);
    }

    if (compilerMismatchDetected) {
      var mismatchedTaskIds = [];
      for (var id in mismatchedTaskIdSet) {
        mismatchedTaskIds.push(id);
      }
      mismatchedTaskIds.sort(function(lhs, rhs) { return lhs - rhs; });

      $('<div>Compiler Mismatch</div>').appendTo(mismatchArea);
      for (var i = 0; i < mismatchedTaskIds.length; ++i) {
        var id = mismatchedTaskIds[i];
        $('<a>').attr('href', '#task' + id).text(id + "")
            .appendTo(mismatchArea);
        mismatchArea.append(' ');
      }
    }

    mismatchArea.show();
  },

  updateTaskStats: function() {
    var resp = this.resp;
    var numActives = gomaTaskView.tasks['active'].length;

    if (numActives > this.maxActives) {
      this.maxActives = numActives;
    }

    function setTextAndMeter(id, value, maxValue) {
      if (maxValue < 1) {
        maxValue = 1;
      }

      $(id + ' .text').text(value);
      $(id + ' meter').attr('value', value)
          .attr('min', 0)
          .attr('max', maxValue)
          .text((100 * value / maxValue) + '%');
    }

    var taskMeterMax = this.maxActives < 100 ? 100 : this.maxActives;
    $('#task-stats-num-limit').text(resp['num_exec']['max_active_tasks']);
    setTextAndMeter('#task-stats-num-max', this.maxActives, taskMeterMax);
    setTextAndMeter('#task-stats-num-actives', numActives, taskMeterMax);
    setTextAndMeter('#task-stats-num-pendings', resp['num_exec']['pending'], taskMeterMax);

    var requestMeterMax = resp['num_exec']['request'] < 1 ? 1 : resp['num_exec']['request'];
    setTextAndMeter('#task-stats-request-total', resp['num_exec']['request'], requestMeterMax);
    setTextAndMeter('#task-stats-request-success', resp['num_exec']['success'], requestMeterMax);
    setTextAndMeter('#task-stats-request-failure', resp['num_exec']['failure'], requestMeterMax);
    setTextAndMeter('#task-stats-request-success-finished', resp['num_exec']['goma_finished'], requestMeterMax);
    setTextAndMeter('#task-stats-request-success-cache-hit', resp['num_exec']['goma_cache_hit'], requestMeterMax);
    setTextAndMeter('#task-stats-request-success-aborted', resp['num_exec']['goma_aborted'], requestMeterMax);
    setTextAndMeter('#task-stats-request-success-retry', resp['num_exec']['goma_retry'], requestMeterMax);
    setTextAndMeter('#task-stats-request-local-run', resp['num_exec']['local_run'], requestMeterMax);
    setTextAndMeter('#task-stats-request-local-finished', resp['num_exec']['local_finished'], requestMeterMax);
    setTextAndMeter('#task-stats-request-local-killed', resp['num_exec']['local_killed'], requestMeterMax);
    setTextAndMeter('#task-stats-request-fail-fallback', resp['num_exec']['fail_fallback'], requestMeterMax);
    setTextAndMeter('#task-stats-request-compiler-proxy-fail', resp['num_exec']['compiler_proxy_fail'], requestMeterMax);

    var gomaMeterMax = resp['num_exec']['goma_finished'] + resp['num_exec']['goma_aborted'];
    setTextAndMeter('#task-stats-goma-killed', resp['num_exec']['local_killed'], gomaMeterMax);
    setTextAndMeter('#task-stats-goma-aborted', resp['num_exec']['goma_aborted'], gomaMeterMax);

    var localMeterMax = resp['num_exec']['local_run'];
    setTextAndMeter('#task-stats-local-killed', resp['num_exec']['local_killed'], localMeterMax);
    setTextAndMeter('#task-stats-local-aborted', resp['num_exec']['goma_aborted'], localMeterMax);

    var raceMeterMax = resp['num_exec']['local_killed'] + resp['num_exec']['goma_aborted'];
    setTextAndMeter('#task-stats-compiler-race-total', raceMeterMax, raceMeterMax);
    setTextAndMeter('#task-stats-compiler-race-goma-win', resp['num_exec']['local_killed'], raceMeterMax);
    setTextAndMeter('#task-stats-compiler-race-local-win', resp['num_exec']['goma_aborted'], raceMeterMax);

    var compilerInfoMeterMax = resp['num_exec']['compiler_info_stores'];
    setTextAndMeter('#task-stats-compiler-info-stores', resp['num_exec']['compiler_info_stores'], compilerInfoMeterMax);
    setTextAndMeter('#task-stats-compiler-info-store-dups', resp['num_exec']['compiler_info_store_dups'], compilerInfoMeterMax);
    setTextAndMeter('#task-stats-compiler-info-miss', resp['num_exec']['compiler_info_miss'], compilerInfoMeterMax);
    setTextAndMeter('#task-stats-compiler-info-fail', resp['num_exec']['compiler_info_fail'], compilerInfoMeterMax);

    var fileInfoMeterMax = resp['num_file']['requested'];
    setTextAndMeter('#task-stats-file-total', resp['num_file']['requested'], fileInfoMeterMax);
    setTextAndMeter('#task-stats-file-uploaded', resp['num_file']['uploaded'], fileInfoMeterMax);
    setTextAndMeter('#task-stats-file-missed', resp['num_file']['missed'], fileInfoMeterMax);
  },

  updateNetworkStats: function() {
    var httprpc = this.resp['http_rpc'];

    this.drawQPSChart(httprpc['qps_data']['qps'], httprpc['qps_data']['http_err']);
    this.drawTrafficChart(httprpc['traffic_data']['read'], httprpc['traffic_data']['write']);

    $('#rpc-health-status').text(httprpc['health_status']);

    $('#rpc-num-active').text(httprpc['num_active']);
    $('#rpc-num-query').text(httprpc['num_query']);
    $('#rpc-num-retry').text(httprpc['num_http_retry']);
    $('#rpc-num-timeout').text(httprpc['num_http_timeout']);
    $('#rpc-num-error').text(httprpc['num_http_error']);

    $('#rpc-socket-pool').text(httprpc['socket_pool']);
    $('#rpc-read-bps').text(humanReadableBytes(httprpc['read_bps']));
    $('#rpc-read-byte').text(humanReadableBytes(httprpc['read_byte']));
    $('#rpc-write-bps').text(humanReadableBytes(httprpc['write_bps']));
    $('#rpc-write-byte').text(humanReadableBytes(httprpc['write_byte']));

    $('#rpc-compression').text(httprpc['compression']);
    $('#rpc-accept-encoding').text(httprpc['accept_encoding']);
    $('#rpc-authorization').text(httprpc['authorization']);
    $('#rpc-cookie').text(httprpc['cookie']);
    $('#rpc-content-type').text(httprpc['content_type']);
    $('#rpc-oauth2').text(httprpc['oauth2']);
    $('#rpc-capture-response-header').text(httprpc['capture_response_header']);
    $('#rpc-ssl').text(httprpc['ssl']);

    $('#rpc-url-path-prefix').text(httprpc['url_path_prefix']);
    $('#rpc-extra-params').text(httprpc['extra_params']);
    $('#rpc-ssl-extra-cert').text(httprpc['ssl_extra_cert']);

    $('#rpc-user-agent').text(httprpc['user_agent']);

    if (httprpc['health_status'] != 'ok') {
      $('#http-rpc-info').addClass('warning');
    } else {
      $('#http-rpc-info').removeClass('warning');
    }
  },

  makeEmptyChart: function(canvasCtx, label1, label2) {
    let labels = [];
    for (let i = 0; i < 120; ++i) {
      labels[i] = i - 119;
    }

    return new Chart(canvasCtx, {
      type: 'line',
      data: {
        labels: labels,
        datasets: [{
          label: label1,
          data: [],
          backgroundColor: "rgba(0,255,0,0.9)",
          borderColor: "rgba(0,255,0,0.9)",
          borderWidth: 1,
          pointRadius: 0,
          fill: false,
        }, {
          label: label2,
          data: [],
          backgroundColor: "rgba(0,0,255,0.9)",
          borderColor: "rgba(0,0,255,0.9)",
          pointRadius: 0,
          borderWidth: 1,
          fill: false,
        }]
      },
      options: {
        elements: {
          line: {
            tension: 0,  // disables bezier curves
          }
        },
        scales: {
          xAxes: [{
            display: true,
            scaleLabel: {
              display: true,
              labelString: 'Time'
            },
          }],
          yAxes: [{
            ticks: {
              beginAtZero: true,
              min: 0,
            },
          }],
        },
        animation:false
      }
    });
  },

  drawQPSChart: function(qps_data, http_err_data) {
    if (qps_data.length != 120 || http_err_data.length != 120) {
      console.error('data length mismatch: ',
                    'qps_data.length=' + qps_data.length,
                    'http_err_data.length=' + http_err_data.length);
      return;
    }

    if (!this.qpsChart) {
      let ctx = $('#http-rpc-qps-chart')[0].getContext('2d');
      this.qpsChart = this.makeEmptyChart(ctx, 'QPS', 'HTTP error');
    }

    this.qpsChart.data.datasets[0].data = qps_data;
    this.qpsChart.data.datasets[1].data = http_err_data;
    this.qpsChart.update();
  },

  drawTrafficChart: function(read_data, write_data) {
    if (read_data.length != 120 || write_data.length != 120) {
      console.error('data length mismatch: ',
                    'read_data.length=' + read_data.length,
                    'write_data.length=' + write_data.length);
      return;
    }

    if (!this.trafficChart) {
      let ctx = $('#http-rpc-traffic-chart')[0].getContext('2d');
      this.trafficChart = this.makeEmptyChart(ctx, 'read', 'write');
    }

    this.trafficChart.data.datasets[0].data = read_data;
    this.trafficChart.data.datasets[1].data = write_data;
    this.trafficChart.update();
  },

  setTaskPositionFirst: function() {
    var currentTasks = this._filteredTasks();
    this.taskStartingOffset = 0;
    this._updateTaskViewWith(currentTasks);
  },

  setTaskPositionPrev: function() {
    var currentTasks = this._filteredTasks();
    this.taskStartingOffset -= this.taskMaxSize;
    if (currentTasks.length < this.taskStartingOffset)
      this.taskStartingOffset = 0;
    if (this.taskStartingOffset < 0)
      this.taskStartingOffset = 0;
    this._updateTaskViewWith(currentTasks);
  },

  setTaskPositionNext: function() {
    var currentTasks = this._filteredTasks();
    this.taskStartingOffset += this.taskMaxSize;
    if (this.taskStartingOffset >= currentTasks.length) {
      this.taskStartingOffset = currentTasks.length - this.taskMaxSize;
    }
    if (this.taskStartingOffset < 0) {
      this.taskStartingOffset = 0;
    }
    this._updateTaskViewWith(currentTasks);
  },

  setTaskPositionLast: function() {
    var currentTasks = this._filteredTasks();
    this.taskStartingOffset = currentTasks.length - this.taskMaxSize;
    if (this.taskStartingOffst < 0)
      this.taskStartingOffset = 0;
    this._updateTaskViewWith(currentTasks);
  },

  // Changes task order.
  changeTaskOrder: function(key) {
    if (this.currentTaskOrderKey != key) {
      // Setting New key
      this.currentTaskOrderKey = key;
      this.currentTaskOrderAscending = true;
    } else if (this.currentTaskOrderAscending) {
      // Setting the same key. Changing the order.
      this.currentTaskOrderAscending = false;
    } else {
      // Setting the same key twice. Remove the key.
      this.currentTaskOrderKey = null;
    }

    this._updateTaskOrderView();
  },

  showTaskDetail: function(taskId) {
    console.log('showTaskDetail: ' + taskId);

    if (taskId < 0)
      return;

    this.currentDetailTaskId = taskId;

    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = (function(that) { return function() {
      if (this.readyState == 4) {
        var responseText = this.responseText;
        this.onreadystatechange = null;
        if (this.status != 200) {
          if (taskId in that.taskDetailCache)
            that._showTaskDetailWith(that.taskDetailCache[taskId]);
          else
            that._showEmptyTaskDetailWith(taskId);
          return;
        }

        var detail = JSON.parse(responseText);
        that._showTaskDetailWith(detail);

        // Cache the detail only when it's finished.
        if (that.finishedTasks[taskId])
          that.taskDetailCache[taskId] = detail;
      }
    };})(this);

    var url = taskUpdater.url + '?id=' + taskId;
    xhr.open('POST', url);
    xhr.send();
  },

  // Returns the list of tasks that are filtered.
  _filteredTasks: function() {
    var currentTasks = this.tasks[this.currentTaskView];
    var result = [];

    var checker = {};
    $('#task-filter input').each(function() {
      if (this.checked)
        checker[this.name] = this.checked;
    });

    for (var i = 0; i < currentTasks.length; ++i) {
      var task = currentTasks[i];
      var className = 'task-status-' + taskStatus(task);
      if (className in checker) {
        result.push(task);
      }
    }

    // Sorts here.
    if (this.currentTaskOrderKey != null) {
      var sortKey = this.currentTaskOrderKey.replace('-', '_');

      var compare = function(v1, v2, ascending) {
        if (v1 == v2) {
          return 0;
        }

        if (v1 && v2) {
          if (v1 < v2) {
            return -ascending;
          }
          return ascending;
        }
        if (!v2) {
          return ascending;
        }
        return -ascending;
      }

      var sortFunc = (function(ascending) {
        return function(a, b) {
          var v1 = a[sortKey];
          var v2 = b[sortKey];
          return compare(v1, v2, ascending);
        };
      })(this.currentTaskOrderAscending ? 1 : -1);

      // When key is 'duration', if 'duration' is missing, we should use
      // 'elapsed' instead.
      if (sortKey == 'duration') {
        sortFunc = (function(ascending) {
          return function(a, b) {
            var v1 = a['duration'] || a['elapsed'] || '0';
            var v2 = b['duration'] || b['elapsed'] || '0';
            return compare(parseInt(v1), parseInt(v2), ascending);
          };
        })(this.currentTaskOrderAscending ? 1 : -1);
      }

      result.sort(sortFunc);
    }

    return result;
  },

  _updateTaskOrderView: function() {
    $('.task-summary-head').removeClass('with-icon-ascending');
    $('.task-summary-head').removeClass('with-icon-descending');
    if (this.currentTaskOrderKey == null)
      return;

    var id = 'task-summary-head-' + this.currentTaskOrderKey;
    var elem = $('#' + id);
    if (this.currentTaskOrderAscending) {
      elem.addClass('with-icon-ascending');
    } else {
      elem.addClass('with-icon-descending');
    }
  },

  _updateTaskViewWith: function(currentTasks) {
    var taskSummaryList = $('#task-summary-list');
    taskSummaryList.empty();

    var startPos = this.taskStartingOffset;
    var endPos = startPos + this.taskMaxSize;
    if (startPos < 0)
      startPos = 0;
    if (endPos >= currentTasks.length)
      endPos = currentTasks.length;
    if (endPos < startPos)
      endPos = startPos;

    for (var i = startPos; i < endPos; ++i) {
      var task = currentTasks[i];

      var tr = $('<tr>');
      var taskStatusName = taskStatus(task);
      tr.addClass('task-status-' + taskStatusName);
      $('<td class="task-summary-id">').text(task.id).appendTo(tr);
      if ('duration' in task) {
        $('<td class="task-summary-duration">').text(task.duration).appendTo(tr);
      } else if ('elapsed' in task) {
        $('<td class="task-summary-duration">').text(task.elapsed).appendTo(tr);
      } else {
        $('<td class="task-summary-duration">').appendTo(tr);
      }
      $('<td class="task-summary-pid">').text(task.pid).appendTo(tr);
      $('<td class="task-summary-state">').text(task.state).appendTo(tr);
      $('<td class="task-summary-status">').text(taskStatusName.toUpperCase().replace('-', ' ')).appendTo(tr);
      $('<td class="task-summary-http-status">').text(task.http_status).appendTo(tr);
      $('<td class="task-summary-subproc-pid">').text(task.subproc_pid).appendTo(tr);
      $('<td class="task-summary-subproc-state">').text(task.subproc_state).appendTo(tr);
      // TODO: Show full command when a cursor is hovered?
      // It will take a big area, though... b/24883527
      $('<td class="task-summary-command">').text(makeFlagSummary(task.command)).appendTo(tr);
      $('<td class="task-summary-major-factor">').text(task.major_factor).appendTo(tr);

      tr.click((function(taskId) {
        return function() {
          location.hash = '#task' + taskId;
        };
      })(task.id));
      tr.css({'cursor': 'pointer'});
      taskSummaryList.append(tr);
    }

    $('#task-summary-total').text(currentTasks.length);
    $('#task-summary-offset-begin').text(startPos + 1);
    $('#task-summary-offset-end').text(endPos);

    if (startPos == 0) {
      $('#task-summary-first').addClass('disabled');
      $('#task-summary-prev').addClass('disabled');
    } else {
      $('#task-summary-first').removeClass('disabled');
      $('#task-summary-prev').removeClass('disabled');
    }

    if (endPos >= currentTasks.length) {
      $('#task-summary-next').addClass('disabled');
      $('#task-summary-last').addClass('disabled');
    } else {
      $('#task-summary-next').removeClass('disabled');
      $('#task-summary-last').removeClass('disabled');
    }
  },

  // Just show task id if task is empty.
  _showEmptyTaskDetailWith: function(taskId) {
    var task = {
      id: taskId,
    };

    this._showTaskDetailWith(task);
  },

  _showTaskDetailWith: function(task) {
    var table = $('#task-detail-table');
    table.empty();

    function addTextItem(key, value) {
      var tr = $('<tr>');
      $('<td>').text(key).appendTo(tr);
      $('<td>').text(value).appendTo(tr);
      table.append(tr);
      return true;
    }

    function addHTMLItem(key, value) {
      var tr = $('<tr>');
      $('<td>').text(key).appendTo(tr);
      $('<td>').html(value).appendTo(tr);
      table.append(tr);

      return true;
    }

    function addLink(key) {
      if (!(key in task))
        return false;

      var value = task[key];
      var tr = $('<tr>');
      $('<td>').text(key).appendTo(tr);
      $('<td>').html($('<a>').attr('href', value).text(value)).appendTo(tr);

      table.append(tr);
      return true;
    }

    function addLineBreak() {
      var tr = $('<tr>');
      tr.addClass('task-linebreak');
      $('<td>').text('');
      $('<td>').text('');
      table.append(tr);
    }

    function add(key, opt_suffix) {
      if (!(key in task))
        return false;

      var value = task[key];
      if (opt_suffix)
        value += opt_suffix;

      return addTextItem(key, value);
    }

    function addLongString(key, opt_usingPre) {
      if (!(key in task))
        return false;

      var value = task[key];

      var details = $('<details>');
      $('<summary>see more ...</summary>').appendTo(details);
      if (opt_usingPre) {
        $('<pre>').text(value).appendTo(details);
      } else {
        $('<div>').text(value).appendTo(details);
      }

      return addHTMLItem(key, details);
    }

    function addArrayItem(key) {
      if (!(key in task))
        return false;

      var values = task[key];
      var value = values.join('<br>');

      var details = $('<details>');
      $('<summary>see more...</summary>').appendTo(details);
      $('<div>').html(value).appendTo(details);

      return addHTMLItem(key, details);
    }

    add('id');
    add('elapsed');
    add('duration');
    add('pid');
    add('state');
    add('subproc_state');
    add('subproc_pid');
    addLongString('command');
    add('major_factor');
    add('command_version_mismatch');
    add('command_binary_hash_mismatch');
    add('command_subprograms_mismatch');
    addLineBreak();

    add('start_time');
    addLineBreak();

    if (addArrayItem('error_message')) {
      addLineBreak();
    }

    add('cache_key');
    add('http_status');
    add('exit');
    add('retry');
    addLineBreak();
    if (addArrayItem('exec_request_retry_reason')) {
      addLineBreak();
    }

    // # of files
    add('total_input');
    add('uploading_input');
    add('missing_input');
    addLineBreak();

    // size
    add('gomacc_req_size');
    add('gomacc_resp_size');
    add('exec_req_size');
    add('exec_resp_size');
    add('output_file_size');
    add('chunk_resp_size');
    addLineBreak();

    /* time */
    add('compiler_info_process_time');
    if (('depscache_used' in task) && task['depscache_used'] == 'true') {
      add('include_preprocess_time', ' (cached)');
    } else {
      add('include_preprocess_time');
    }
    add('include_fileload_time');
    add('include_fileload_pending_time');
    add('include_fileload_run_time');
    add('rpc_call_time');
    add('file_response_time');
    addLineBreak();


    /* file_response_time break down */
    add('output_file_rpc');
    add('output_file_rpc_req_build_time');
    add('output_file_rpc_req_send_time');
    add('output_file_rpc_wait_time');
    add('output_file_rpc_resp_recv_time');
    add('output_file_rpc_resp_parse_time');
    addLineBreak();

    add('local_delay_time');

    /* local run */
    if (('local_pending_time' in task) || 'local_run_time' in task) {
      add('local_pending_time');
      add('local_run_time');
      add('local_mem_kb');
      add('local_output_file_time');
      add('local_output_file_size');
      add('local_run_reason');
      addLineBreak();
    }

    /* command detail */
    add('cwd');
    addLineBreak();
    if (add('orig_flag')) {
      addLineBreak();
    }
    if (addArrayItem('env')) {
      addLineBreak();
    }
    addArrayItem('exec_output_files');
    addLineBreak();
    if (addLongString('stdout', true)) {
      addLineBreak();
    }
    if (addLongString('stderr', true)) {
      addLineBreak();
    }
    addArrayItem('input_files');
    addArrayItem('system_library_paths');
    add('response_header');

    if (('state' in task) && task['state'] === 'FINISHED') {
      var tr = $('<tr>');
      var exec_req_dump_url =
          './api/taskz?id=' + encodeURIComponent(task['id']) + '&dump=req';
      $('<td>').text('dump_exec_req').appendTo(tr);

      var form = $('<form>')
          .attr('method', 'POST')
          .attr('action', exec_req_dump_url)
          .html($('<button>').text('dump exec req'));

      $('<td>').html(form).appendTo(tr);

      table.append(tr);
    }

    var prevTaskId = task.id - 1;
    if (prevTaskId >= 0) {
      $('#task-show-prev').attr('href', '#task' + prevTaskId);
    } else {
      $('#task-show-prev').removeAttr('href');
    }
    var nextTaskId = task.id + 1;
    $('#task-show-next').attr('href', '#task' + nextTaskId);

    showPage('task-detail');
  }
};

var gomaTaskView = new GomaTaskView();

// showPage shows only one page.
function showPage(pageName) {
  $('.page').hide();
  $('#' + pageName + '-page').show();

  gomaTaskView.updateTaskView();
}

// ----------------------------------------------------------------------

/**
 * Current task updater.
 * @type {Loader}
 */
var taskUpdater = null;

/**
 * Constructs loadder that gets from 'url' and calls 'updater'.
 * @param {string} url url to request XHR.
 * @param {Function} updater closure to update by response text.
 * @constructor
 */
function Loader(url, updater) {
  this.url = url;
  this.updater = updater;
  this.suspended = true;
  this.xhr = null;
  this.params = {};
}

/**
 * Starts loading.
 */
Loader.prototype.start = function() {
   this.suspended = false;
   this.load();
};

/**
 * Stops loading.
 */
Loader.prototype.stop = function() {
  this.suspended = true;
};

/**
 * Updates load status shown in id='update-status'.
 * @param {string} msg loader status message.
 */
Loader.prototype.updateLoaderStatus = function(msg) {
  document.getElementById('update-status').innerText = msg;
};

/**
 * Set parameter
 * @param {string} key key for POST parameter
 * @param {string} value value for POST parameter
 */
Loader.prototype.setParameter = function(key, value) {
  this.params[key] = value;
};

/**
 * Loads data from 'url' and call 'updater'. Repeats this with 1 sec interval
 * while 'suspended' is false.
 */
Loader.prototype.load = function() {
  var self = this;

  this.xhr = new XMLHttpRequest();
  this.xhr.onreadystatechange = function() {
    if (this.readyState == 4) {
      var responseText = self.xhr.responseText;
      self.xhr.onreadystatechange = null;
      self.xhr = null;
      if (this.status == 200) {
        self.updateLoaderStatus('parsing');
        setTimeout(function() {
          if (self.suspended) {
            self.updateLoaderStatus('suspended');
          } else {
            self.updater(responseText);
            // TODO: We may want validation here?
            var updateFreq =
                document.getElementById('update-freq').value | 0;
            setTimeout(function() { self.load(); }, updateFreq);
            self.updateLoaderStatus('waiting');
          }
        }, 1);
      } else {
        var errorArea = $('#error');
        errorArea.show();
        errorArea.text(
            'request to ' + self.url + ' got error ' + this.status);
      }
    }
  };

  var url = self.url;
  var param = $.param(self.params);
  if (param != '') {
    url += '?' + param;
  }

  this.xhr.open('POST', url);
  this.xhr.send();
  this.updateLoaderStatus('loading');
};

// ----------------------------------------------------------------------

/**
 * Updates goma version.
 * @param {Array} resp json response data [my_version, pulled_version].
 */
function updateGomaVersion(resp) {
  var my_version = resp[0];
  var pulled_version = resp[1];
  var d = document.getElementById('goma_version');
  if (my_version >= pulled_version) {
    d.innerText = 'goma_version: ' + my_version;
  } else {
    d.innerText = 'goma_version: ' + my_version +
                  ' [' + pulled_version + ' available]';
    d.setAttribute('class', 'warning');
  }
}

/**
 * Update taskz response in 'domNode'.
 * @param {Element} domNode dom node.
 * @param {string} response JSON text.
 */
function updateTaskView(domNode, response) {
  var resp = JSON.parse(response);

  if (resp['goma_version']) {
    updateGomaVersion(resp['goma_version']);
  }

  if (resp['active']) {
    gomaTaskView.tasks['active'] = resp['active'];
    $('#count-active-tasks').text('' + gomaTaskView.tasks['active'].length);
  }
  if (resp['finished']) {
    var finished = resp['finished'];
    for (var i = finished.length - 1; i >= 0; --i) {
      var task = finished[i];
      if (!(task.id in gomaTaskView.finishedTasks)) {
        gomaTaskView.tasks['finished'].unshift(task);
        gomaTaskView.finishedTasks[task.id] = task;
      }
    }
    $('#count-finished-tasks').text('' + gomaTaskView.tasks['finished'].length);
  }
  if (resp['failed']) {
    var failed = resp['failed'];
    for (var i = failed.length - 1; i >= 0; --i) {
      var task = failed[i];
      if (!(task.id in gomaTaskView.failedTasks)) {
        gomaTaskView.tasks['failed'].unshift(task)
        gomaTaskView.failedTasks[task.id] = task;
      }
    }
    $('#count-failed-tasks').text('' + gomaTaskView.tasks['failed'].length);
  }
  if (resp['long']) {
    gomaTaskView.tasks['long'] = resp['long'];
    $('#count-long-tasks').text('' + gomaTaskView.tasks['long'].length);
  }

  if (resp['last_update_ms']) {
    taskUpdater.setParameter('after', resp['last_update_ms']);
  }

  gomaTaskView.resp = resp;

  gomaTaskView.updateTaskView();
  gomaTaskView.updateMismatchView();
  gomaTaskView.updateTaskStats();
  gomaTaskView.updateNetworkStats();
}

/**
 * Request to update 'taskview' by /api/taskz.
 */
function startTaskUpdater() {
  taskUpdater = new Loader('/api/taskz',
    function(response) {
      updateTaskView(document.getElementById('taskview'), response);
    });
  taskUpdater.start();
}

function onHashChange() {
  if (location.hash == '#active') {
    gomaTaskView.currentTaskView = 'active';
    gomaTaskView.setTaskPositionFirst();
    showPage('task-summary');
  } else if (location.hash == '#finished') {
    gomaTaskView.currentTaskView = 'finished';
    gomaTaskView.setTaskPositionFirst();
    showPage('task-summary');
  } else if (location.hash == '#failed') {
    gomaTaskView.currentTaskView = 'failed';
    gomaTaskView.setTaskPositionFirst();
    showPage('task-summary');
  } else if (location.hash == '#long') {
    gomaTaskView.currentTaskView = 'long';
    gomaTaskView.setTaskPositionFirst();
    showPage('task-summary');
  } else if (location.hash == '#task-stats') {
    showPage('task-stats');
  } else if (location.hash == '#network-stats') {
    showPage('network-stats');
  } else if (location.hash == '#settings') {
    showPage('settings');
  } else if (location.hash.startsWith('#task')) {
    var taskId = parseInt(location.hash.substr(5));
    if (!isNaN(taskId))
      gomaTaskView.showTaskDetail(taskId);
  }
}

function accountUpdate() {
  $.get('/api/accountz',
    function(data) {
      $('#http_status').text(data.status);
      $('#email').text(data.account);
      var login = $('#login');
      login.text(data.text);
      login.attr("href", data.href);
    })
   .always(function() {
     setTimeout(accountUpdate, 10*60*1000);
   });
}

function init() {
  startTaskUpdater();
  accountUpdate();

  // Prevents text selection when clicking icons many times.
  $('.menu-icon').mousedown(function(e) { e.preventDefault(); });

  $('#task-summary-first').click(function() {
    if ($(this).hasClass('disabled'))
      return;
    gomaTaskView.setTaskPositionFirst();
  });
  $('#task-summary-prev').click(function() {
    if ($(this).hasClass('disabled'))
      return;
    gomaTaskView.setTaskPositionPrev();
  });
  $('#task-summary-next').click(function() {
    if ($(this).hasClass('disabled'))
      return;
    gomaTaskView.setTaskPositionNext();
  });
  $('#task-summary-last').click(function() {
    if ($(this).hasClass('disabled'))
      return;
    gomaTaskView.setTaskPositionLast();
  });

  $('.task-summary-head').click(function() {
    var id = $(this).attr('id');
    // remove 'task-summary-head-'
    if (!id.startsWith('task-summary-head-')) {
      console.error('task-summary-head class element should'
          + ' have id starting with task-summary-head');
      return;
    }

    var key = id.slice('task-summary-head-'.length);
    gomaTaskView.changeTaskOrder(key);
  });

  $('input[name="update-task"]:radio').change(function() {
    var value = $(this).val();
    if (taskUpdater == null)
      return;

    if (value == 'on')
      taskUpdater.start();
    else
      taskUpdater.stop();
  });

  $('input[name="pagesize"]:radio').change(function() {
    var pagesize = parseInt($(this).val(), 10);
    if (isNaN(pagesize))
      pagesize = 25;
    gomaTaskView.setPageSize(pagesize);
  });

  $('#btn-task-check-all').click(function(event) {
    $('#task-filter input').each(function() {
      this.checked = true;
    });
    event.preventDefault();
  });

  $('#btn-task-uncheck-all').click(function(event) {
    $('#task-filter input').each(function() {
      this.checked = false;
    });
    event.preventDefault();
  });

  window.onhashchange = onHashChange;

  // The default view is 'active'. But we can check the hash value
  // to determine what page should be shown first.
  gomaTaskView.currentTaskView = 'active';
  showPage('task-summary');
  onHashChange();
}
