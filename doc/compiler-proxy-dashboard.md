# compiler_proxy dashboard

This document describes compiler_proxy dashboard (a.k.a. http://localhost:8088).

[TOC]

## The meaning of finished tasks

compiler_proxy's dashboard shows finished tasks with several colors:

* Succeeded
  * The task has run in a Goma server and it succeeded.
* Cache hit
  * The task has sent to a Goma backend, and the result is returned from cache.
* Local fallback
  * The task finished locally. This happens when 1) the local command finishes
    before Goma respond or 2) Goma decided to run the task locally (Goma doesn't
    use a backend if the task is preprocess or link).
* Retry
  * The first request failed but the retried request succeeded. compiler_proxy
    remembers files it has already sent to backends and doesn't send them again.
    This status can happen when Goma's backends don't have some of these files
    actually, due to cache expiration. You should be able to consider this state
    as success unless you see many (like &gt; 30%).
* Mismatch
  * compiler version, compiler hash, or subprogram (e.g. .so files that a
    compiler uses) is mismatched between client and server. It means, the Goma
    server didn't use the same compiler as the compiler you used in your client,
    maybe because the Goma server doesn't have the same compiler.
    (Mismatch will be shown from Goma version 92.)
* Goma Error
  * Something wrong has happened in Goma's backends. Several reasons can cause
    this: 1) this type of request isn't supported by Goma yet, 2) backends are
    unhealthy, 3) Goma's bug, 4) cache expiration, etc. Anyway, compiler_proxy
    will run local fallback whenever this happens and your compilation should
    succeed. You may be able to ignore these failures unless a lot of tasks are
    going to this state. When many tasks fail due to this reason, Goma's
    performance can be significantly worse than usual.
* Failed
  * Local task failed. Usually, this means your source code has some errors.
    If you are sure your source code is OK, this may indicate a serious bug in
    Goma. Please let us know.
* Failed but maybe during ./configure
  * Same as `Failed` but the input filename contains "conftest"
    so this may happened during `./configure`
* Canceled
  * Goma canceled this task. This should happen only when you stopped
    your build (e.g., SIGINT for make).

## The meaning of Goma statistics

http://localhost:8088/statz (or command line `goma_ctl.py stat`) shows
compiler\_proxy statistics like the following:

```
request: total=8779 success=8777 failure=0
 compiler_proxy: fail=0
 compiler_info: stores=0 store_dups=0 miss=0 fail=0
 goma: finished=8511 cache_hit=7147 aborted=78 retry=3 fail=2
 local: run=342 killed=72 finished=190
 local run reason:
  fail in call exec=2
  fast goma, local not started=1
  killed by fast goma=72
  local idle=42
  should fallback=138
  should not run while delaying subproc=36
  slow goma, local run started in CALL_EXEC=15
  slow goma, local run started in FILE_REQ=37
binary_hash_mismatch: local:clang++ 4.2.1[clang version 3.5 ] (6795dc1f4742ba948
d3013bbe30ba2c9ad777591dccd7cc285b87a1f2a7be32c) but remote:clang++ 4.2.1[clang
version 3.5 ] (48ebdf87967cf7e6b9794ec7c8ede41939da40889e26280ccdc0bee9f0416fe8)
 1

files: requested=2590510 uploaded=65511 missed=4
outputs: files=17018 rename=3286 max_sum_size=97206735
memory: consuming=1110646784
time: uptime=690
include_processor: total=6377640 skipped=29243457 total_wait_time=2929048 total_run_time=3579761
includecache:
  entries=31503 cache_size=22604906 hit=14235680 missed=31682 updated=179 evicted=0
  orig_total=225083081 orig_max=3258206 orig_ave=7144 filter_total=22604906 filter_max=305128 filter_ave=717
incdircache: instances=64 memory=140835214 created=6426 reused=27556
http_rpc: query=1702 retry=0 timeout=0 error=0
```

* request: represents the number of request compiler_proxy received from gomacc.
  * total: total number of request from gomacc.
  * success: number of successful request
  * failure: number of compile error notified to command line.
* compiler_proxy
  * fail: the number of compiles that failed in Goma backend but succeeded
    in local fallback. This is considered to be something wrong happened
    in Goma backend.
* goma: represents the number of compile request remote Goma server handled.
  * finished: the number of compile finished in remote Goma server.
  * cache_hit: the number of compile result returned from the Goma remote
    server's cache.
  * aborted: the local compile is faster than remote and aborted remote compile.
  * retry: the number of compile retried because of retry-able server or
    communication error.
  * fail: the number of compile failed in remote Goma server.
    (fallbacks to local if goma_FALLBACK=1 (default))
* local: represents the number of compile request executed locally.
  * run: total number of compile run locally (run = killed + finished)
  * killed: the number of compile started but killed because remote Goma
    compile is faster than local compile.
  * finished: the number of compiles finished.
* time:
  * uptime: elapsed time from compiler_proxy start up.
