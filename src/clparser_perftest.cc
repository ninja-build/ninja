// Copyright 2017 Google Inc. All Rights Reserved.
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

#include <stdio.h>
#include <stdlib.h>

#include "clparser.h"
#include "metrics.h"

int main(int argc, char* argv[]) {
  // Output of /showIncludes from #include <iostream>
  string perf_testdata =
      "Note: including file: C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\iostream\r\n"
      "Note: including file:  C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\istream\r\n"
      "Note: including file:   C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\ostream\r\n"
      "Note: including file:    C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\ios\r\n"
      "Note: including file:     C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xlocnum\r\n"
      "Note: including file:      C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\climits\r\n"
      "Note: including file:       C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\yvals.h\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xkeycheck.h\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\crtdefs.h\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\sal.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\ConcurrencySal.h\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vadefs.h\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt.h\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\use_ansi.h\r\n"
      "Note: including file:       C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\limits.h\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:      C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cmath\r\n"
      "Note: including file:       C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\math.h\r\n"
      "Note: including file:       C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xtgmath.h\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xtr1common\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cstdlib\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\stdlib.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_malloc.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_search.h\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\stddef.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wstdlib.h\r\n"
      "Note: including file:      C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cstdio\r\n"
      "Note: including file:       C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\stdio.h\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wstdio.h\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_stdio_config.h\r\n"
      "Note: including file:      C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\streambuf\r\n"
      "Note: including file:       C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xiosbase\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xlocale\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cstring\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\string.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_memory.h\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_memcpy_s.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\errno.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime_string.h\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wstring.h\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\stdexcept\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\exception\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\type_traits\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xstddef\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cstddef\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\initializer_list\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\malloc.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime_exception.h\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\eh.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_terminate.h\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xstring\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xmemory0\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cstdint\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\stdint.h\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\limits\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\ymath.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cfloat\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\float.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cwchar\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\wchar.h\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wconio.h\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wctype.h\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wdirect.h\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wio.h\r\n"
      "Note: including file:                C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_share.h\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wprocess.h\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\corecrt_wtime.h\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\sys/stat.h\r\n"
      "Note: including file:                C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\sys/types.h\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\new\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime_new.h\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xutility\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\utility\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\iosfwd\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\crtdbg.h\r\n"
      "Note: including file:                C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime_new_debug.h\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xatomic0.h\r\n"
      "Note: including file:            C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\intrin.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\setjmp.h\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\immintrin.h\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\wmmintrin.h\r\n"
      "Note: including file:               C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\nmmintrin.h\r\n"
      "Note: including file:                C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\smmintrin.h\r\n"
      "Note: including file:                 C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\tmmintrin.h\r\n"
      "Note: including file:                  C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\pmmintrin.h\r\n"
      "Note: including file:                   C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\emmintrin.h\r\n"
      "Note: including file:                    C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xmmintrin.h\r\n"
      "Note: including file:                     C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\mmintrin.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\ammintrin.h\r\n"
      "Note: including file:             C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\mm3dnow.h\r\n"
      "Note: including file:              C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\typeinfo\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime_typeinfo.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\vcruntime.h\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xlocinfo\r\n"
      "Note: including file:          C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xlocinfo.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\ctype.h\r\n"
      "Note: including file:           C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\locale.h\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\xfacet\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\system_error\r\n"
      "Note: including file:         C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\INCLUDE\\cerrno\r\n"
      "Note: including file:        C:\\Program Files (x86)\\Windows Kits\\10\\include\\10.0.10240.0\\ucrt\\share.h\r\n";

  for (int limit = 1 << 10; limit < (1<<20); limit *= 2) {
    int64_t start = GetTimeMillis();
    for (int rep = 0; rep < limit; ++rep) {
      string output;
      string err;

      CLParser parser;
      if (!parser.Parse(perf_testdata, "", &output, &err)) {
        printf("%s\n", err.c_str());
        return 1;
      }
    }
    int64_t end = GetTimeMillis();

    if (end - start > 2000) {
      int delta_ms = (int)(end - start);
      printf("Parse %d times in %dms avg %.1fus\n",
             limit, delta_ms, float(delta_ms * 1000) / limit);
      break;
    }
  }

  return 0;
}
