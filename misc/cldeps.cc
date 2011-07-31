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


// Wrapper around cl that adds /showIncludes to command line, and uses that to
// generate .d files that match the style from gcc -MD.
//
// /showIncludes is equivalent to -MD, not -MMD, that is, system headers are
// included.


#include <windows.h>
#include <sstream>
#include "subprocess.h"
#include "util.h"

// We don't want any wildcard expansion.
// See http://msdn.microsoft.com/en-us/library/zay8tzh6(v=vs.85).aspx
void _setargv() {}

static void usage(const char* msg) {
  Fatal("%s\n\nusage:\n"
          "  cldeps "
          "<output-path-for-.d-file> "
          "<output-path-for-.obj-file> "
          "<path-to-cl> "
          "<rest-of-command...>\n", msg);
}

static string trimLeadingSpace(const string& cmdline) {
  int i = 0;
  for (; cmdline[i] == ' '; ++i)
    ;
  return cmdline.substr(i);
}

static void doEscape(string& str, const string& search, const string& repl) {
  string::size_type pos = 0;
  while ((pos = str.find(search, pos)) != string::npos) {
    str.replace(pos, search.size(), repl);
    pos += repl.size();
  }
}

// Strips one argument from the cmdline and returns it. "surrounding quotes"
// are removed from the argument if there were any.
static string getArg(string& cmdline) {
  string ret;
  bool in_quoted = false;
  unsigned int i = 0;

  cmdline = trimLeadingSpace(cmdline);

  for (;; ++i) {
    if (i >= cmdline.size())
      usage("Couldn't parse arguments.");
    if (!in_quoted && cmdline[i] == ' ')
      break;
    if (cmdline[i] == '"')
      in_quoted = !in_quoted;
  }

  ret = cmdline.substr(0, i);
  if (ret[0] == '"' && ret[i - 1] == '"')
    ret = ret.substr(1, ret.size() - 2);
  cmdline = cmdline.substr(i);
  return ret;
}

static void parseCommandLine(LPTSTR wincmdline,
        string& dfile, string& objfile, string& clpath, string& rest) {
  string cmdline(wincmdline);
  /* self */ getArg(cmdline);
  dfile = getArg(cmdline);
  objfile = getArg(cmdline);
  clpath = getArg(cmdline);
  rest = trimLeadingSpace(cmdline);
}

static void outputDepFile(const string& dfile, const string& objfile,
        vector<string>& incs) {

  // strip duplicates
  sort(incs.begin(), incs.end());
  incs.erase(unique(incs.begin(), incs.end()), incs.end());

  FILE* out = fopen(dfile.c_str(), "wb");

  // FIXME should this be fatal or not? delete obj? delete d?
  if (!out)
    return;

  fprintf(out, "%s: \\\n", objfile.c_str());
  for (vector<string>::iterator i(incs.begin()); i != incs.end(); ++i) {
    string tmp = *i;
    doEscape(tmp, "\\", "\\\\");
    doEscape(tmp, " ", "\\ ");
    fprintf(out, "%s \\\n", tmp.c_str());
  }

  fprintf(out, "\n");
  fclose(out);
}

int main() {

  // Use the Win32 api instead of argc/argv so we can avoid interpreting the
  // rest of command line after the .d and .obj. Custom parsing seemed
  // preferable to the ugliness you get into in trying to re-escape quotes for
  // subprocesses, so by avoiding argc/argv, the subprocess is called with
  // the same command line verbatim.

  string dfile, objfile, clpath, rest;
  parseCommandLine(GetCommandLine(), dfile, objfile, clpath, rest);

  //fprintf(stderr, "D: %s\n", dfile.c_str());
  //fprintf(stderr, "OBJ: %s\n", objfile.c_str());
  //fprintf(stderr, "CL: %s\n", clpath.c_str());
  //fprintf(stderr, "REST: %s\n", rest.c_str());

  SubprocessSet subprocs;
  Subprocess* subproc = new Subprocess;
  if (!subproc->Start(&subprocs, clpath + string(" /showIncludes ") + rest))
    return 2;

  subprocs.Add(subproc);
  while ((subproc = subprocs.NextFinished()) == NULL) {
    subprocs.DoWork();
  }

  bool success = subproc->Finish();
  string output = subproc->GetOutput();

  delete subproc;

  // process the include directives and output everything else
  stringstream ss(output);
  string line;
  string prefix("Note: including file: "); // FIXME does VS localize this?
  vector<string> includes;
  while (getline(ss, line)) {
    if (line.compare(0, prefix.size(), prefix) == 0) {
      string inc = trimLeadingSpace(line.substr(prefix.size()).c_str());
      if (inc[inc.size() - 1] == '\r') // blech, stupid \r\n
        inc = inc.substr(0, inc.size() - 1);
      includes.push_back(inc);
    } else {
      fprintf(stdout, "%s\n", line.c_str());
    }
  }

  if (!success)
    return 3;

  // don't update .d until/unless we succeed compilation
  outputDepFile(dfile, objfile, includes);

  return 0;
}
