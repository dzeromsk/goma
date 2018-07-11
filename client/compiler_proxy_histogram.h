// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_PROXY_HISTOGRAM_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_PROXY_HISTOGRAM_H_

#include <sstream>
#include <vector>

#include "basictypes.h"
#include "histogram.h"
#include "lockhelper.h"
#include "threadpool_http_server.h"

namespace devtools_goma {

class CompileStats;
class GomaHistograms;

class CompilerProxyHistogram {
 public:
  enum HistogramItems {
    // Stats from compiler_proxy Task
    PendingTime,
    CompilerInfoProcessTime,
    IncludePreprocessTime,
    IncludeProcessorWaitTime,
    IncludeProcessorRunTime,
    IncludeFileloadTime,
    UploadingInputFile,
    MissingInputFile,
    // Time taken for HTTP RPC compiler_proxy is sending to stubby_proxy / GFE.
    RPCCallTime,
    FileResponseTime,
    // Time taken for compiler_proxy to handle request.
    CompilerProxyHandlerTime,

    // Stats from protocol buffer reponse
    GomaccReqSize,
    GomaccRespSize,

    ExecReqSize,
    ExecReqRawSize,
    ExecReqCompressionRatio,
    ExecReqBuildTime,
    ExecReqTime,
    ExecReqKbps,
    ExecWaitTime,
    ExecRespSize,
    ExecRespRawSize,
    ExecRespCompressionRatio,
    ExecRespTime,
    ExecRespKbps,
    ExecRespParseTime,

    // Stats for FileService
    InputFileTime,
    InputFileSize,
    InputFileKbps,
    InputFileReqRawSize,
    InputFileReqCompressionRatio,
    OutputFileTime,
    OutputFileSize,
    ChunkRespSize,
    OutputFileKbps,
    OutputFileRespRawSize,
    OutputFileRespCompressionRatio,

    // Stats for subprocess
    LocalDelayTime,
    LocalPendingTime,
    LocalRunTime,
    LocalMemSize,
    LocalOutputFileTime,
    LocalOutputFileSize,

    // Stats for ThreadpoolHttpServer
    THSReqSize,
    THSRespSize,
    THSWaitingTime,
    THSReadReqTime,
    THSHandlerTime,
    THSWriteRespTime,
    NumCols
  };

  CompilerProxyHistogram();
  ~CompilerProxyHistogram();

  void UpdateThreadpoolHttpServerStat(
      const ThreadpoolHttpServer::Stat& stat);
  void UpdateCompileStat(const CompileStats& task);

  int64_t GetStatMean(HistogramItems item) const;
  double GetStatStandardDeviation(HistogramItems item) const;
  void DumpString(std::ostringstream* ss);
  void DumpToProto(GomaHistograms* hist);

  void Reset();

 private:
  mutable Lock mu_;
  std::vector<Histogram> histogram_;

  DISALLOW_COPY_AND_ASSIGN(CompilerProxyHistogram);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_PROXY_HISTOGRAM_H_
