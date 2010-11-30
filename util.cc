#include "util.h"

#include <execinfo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void DumpBacktrace(int skip_frames) {
  void* stack[256];
  int size = backtrace(stack, 256);
  ++skip_frames;  // Skip ourselves as well.
  backtrace_symbols_fd(stack + skip_frames, size - skip_frames, 2);
}

void Fatal(const char* msg, ...) {
  va_list ap;
  fprintf(stderr, "FATAL: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  DumpBacktrace(1);
  exit(1);
}
