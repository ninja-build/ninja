// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "change_journal.h"
#include "disk_interface.h"
#include "interesting_paths.h"
#include "pathdb.h"
#include "stat_cache.h"
#include "stat_daemon_util.h"
#include "util.h"

#include <algorithm>
#include <assert.h>
#include <windows.h>
#include <winioctl.h>

BOOL WINAPI NotifyInterrupted(DWORD dwCtrlType) {
  if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
    gShutdown = true;
    fclose(fopen("shutdown_notify", "w"));
    _unlink("shutdown_notify");
    return TRUE;
  }
  return FALSE;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("usage: %s <build_root>\n", argv[0]);
    return 1;
  }
  // TODO: probably don't actually need this?
  char build_root[_MAX_PATH];
  if (!GetFullPathName(argv[1], sizeof(build_root), build_root, NULL)) {
    fprintf(stderr, "failed to get full path for build root\n");
    return 2;
  }
  gBuildRoot = build_root;

  if (!SetConsoleCtrlHandler(NotifyInterrupted, TRUE))
    Win32Fatal("SetConsoleCtrlHandler");
  gShutdown = false;
  Log("starting");
  InterestingPaths interesting_paths(true);
  RealDiskInterface disk_interface;
  StatCache stat_cache(true, &disk_interface);
  ChangeJournal cj('C', stat_cache, interesting_paths);
  while (!gShutdown) {
    cj.CheckForDirtyPaths();
    if (!cj.ProcessAvailableRecords())
      Fatal("ProcessAvailableRecords");
    cj.WaitForMoreData();
    // Wait a little to get some batch processing if there's a lot happening.
    Sleep(500);
  }
  Log("shutting down");
  return 0;
}
