

// Dump a backtrace to stderr.
// |skip_frames| is how many frames to skip;
// DumpBacktrace implicitly skips itself already.
void DumpBacktrace(int skip_frames);

// Log a fatal message, dump a backtrace, and exit.
void Fatal(const char* msg, ...);
