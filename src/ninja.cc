// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "getopt.h"
#include <direct.h>
#include <windows.h>
#elif defined(_AIX)
#include "getopt.h"
#include <unistd.h>
#else
#include <getopt.h>
#include <unistd.h>
#endif

#include <iostream>

#include "public/build_config.h"
#include "public/logger.h"
#include "public/tools.h"
#include "public/ui.h"
#include "public/version.h"

#include "build.h"
#include "build_log.h"
#include "deps_log.h"
#include "debug_flags.h"
#include "disk_interface.h"
#include "graph.h"
#include "manifest_parser.h"
#include "metrics.h"
#include "state.h"
#include "status.h"
#include "util.h"

#ifdef _MSC_VER
// Defined in msvc_helper_main-win32.cc.
int MSVCHelperMain(int argc, char** argv);

// Defined in minidump-win32.cc.
void CreateWin32MiniDump(_EXCEPTION_POINTERS* pep);
#endif

// Use namespace ninja for now until we can carve out all
// of the logic that should be internal to the ninja library
// from the logic that should demonstrate how to use the library.
using namespace ninja;
namespace {

#ifdef _MSC_VER

/// This handler processes fatal crashes that you can't catch
/// Test example: C++ exception in a stack-unwind-block
/// Real-world example: ninja launched a compiler to process a tricky
/// C++ input file. The compiler got itself into a state where it
/// generated 3 GB of output and caused ninja to crash.
void TerminateHandler() {
  CreateWin32MiniDump(NULL);
  std::cerr << "terminate handler called" << std::endl;
  ui::ExitNow();
}

/// On Windows, we want to prevent error dialogs in case of exceptions.
/// This function handles the exception, and writes a minidump.
int ExceptionFilter(unsigned int code, struct _EXCEPTION_POINTERS *ep) {
  std::cerr << ui::Error() << "exception: " << code << std::endl;  // e.g. EXCEPTION_ACCESS_VIOLATION
  fflush(stderr);
  CreateWin32MiniDump(ep);
  return EXCEPTION_EXECUTE_HANDLER;
}

#endif  // _MSC_VER

NORETURN void real_main(int argc, char** argv) {
  // Use exit() instead of return in this function to avoid potentially
  // expensive cleanup when destructing Ninja's state.
  BuildConfig config;
  Options options = {};
  options.input_file = "build.ninja";
  options.dupe_edges_should_err = true;

  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
  const char* ninja_command = argv[0];

  int exit_code = ui::ReadFlags(&argc, &argv, &options, &config);
  if (exit_code >= 0)
    exit(exit_code);

  if (options.depfile_distinct_target_lines_should_err) {
    config.depfile_parser_options.depfile_distinct_target_lines_action_ =
        kDepfileDistinctTargetLinesActionError;
  }

  State state(ninja_command, config, new LoggerBasic());
  if (options.working_dir) {
    // The formatting of this string, complete with funny quotes, is
    // so Emacs can properly identify that the cwd has changed for
    // subsequent commands.
    // Don't print this if a tool is being used, so that tool output
    // can be piped into a file without this string showing up.
    if (!options.tool)
      std::cerr << ui::Info() << "Entering directory `" << options.working_dir << "'" << std::endl;
    if (chdir(options.working_dir) < 0) {
      std::cerr << ui::Error() << "chdir to '" << options.working_dir << "' - " << strerror(errno) << std::endl;
      exit(1);
    }
  }

  if (options.tool && options.tool->when == Tool::RUN_AFTER_FLAGS) {
    // None of the RUN_AFTER_FLAGS actually use a ninja state, but it's needed
    // by other tools.
    exit((options.tool->func)(&state, &options, argc, argv));
  }

  Status* status = new StatusPrinter(config);

  // Limit number of rebuilds, to prevent infinite loops.
  const int kCycleLimit = 100;
  for (int cycle = 1; cycle <= kCycleLimit; ++cycle) {

    ManifestParserOptions parser_opts;
    if (options.dupe_edges_should_err) {
      parser_opts.dupe_edge_action_ = kDupeEdgeActionError;
    }
    if (options.phony_cycle_should_err) {
      parser_opts.phony_cycle_action_ = kPhonyCycleActionError;
    }
    ManifestParser parser(&state, state.disk_interface_, parser_opts);
    string err;
    if (!parser.Load(options.input_file, &err)) {
      status->Error("%s", err.c_str());
      exit(1);
    }

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOAD)
      exit((options.tool->func)(&state, &options, argc, argv));

    if (!EnsureBuildDirExists(&state, state.disk_interface_, state.config_, &err))
      exit(1);

    if (!OpenBuildLog(&state, state.config_, false, &err) || !OpenDepsLog(&state, state.config_, false, &err)) {
      std::cerr << ui::Error() << err << std::endl;
      exit(1);
    }

    // Hack: OpenBuildLog()/OpenDepsLog() can return a warning via err
    if(!err.empty()) {
      std::cerr << ui::Warning() << err << std::endl;
      err.clear();
    }

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOGS)
      exit((options.tool->func)(&state, &options, argc, argv));

    // Attempt to rebuild the manifest before building anything else
    if (RebuildManifest(&state, options.input_file, &err, status)) {
      // In dry_run mode the regeneration will succeed without changing the
      // manifest forever. Better to return immediately.
      if (config.dry_run)
        exit(0);
      // Start the build over with the new manifest.
      continue;
    } else if (!err.empty()) {
      status->Error("rebuilding '%s': %s", options.input_file, err.c_str());
      exit(1);
    }

    int result = RunBuild(&state, argc, argv, status);
    if (g_metrics)
      state.DumpMetrics();
    exit(result);
  }

  status->Error("manifest '%s' still dirty after %d tries",
      options.input_file, kCycleLimit);
  exit(1);
}

}  // anonymous namespace

int main(int argc, char** argv) {
#if defined(_MSC_VER)
  // Set a handler to catch crashes not caught by the __try..__except
  // block (e.g. an exception in a stack-unwind-block).
  std::set_terminate(TerminateHandler);
  __try {
    // Running inside __try ... __except suppresses any Windows error
    // dialogs for errors such as bad_alloc.
    real_main(argc, argv);
  }
  __except(ExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
    // Common error situations return exitCode=1. 2 was chosen to
    // indicate a more serious problem.
    return 2;
  }
#else
  real_main(argc, argv);
#endif
}
