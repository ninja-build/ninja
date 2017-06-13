#!/usr/bin/env python

from __future__ import print_function

import collections
import ctypes
import os
import re
import struct
import sys

import frontend

class SlidingRateInfo(object):
    def __init__(self, n=32):
        self.rate = -1
        self.last_update = -1
        self.times = collections.deque(maxlen=n)

    def update_rate(self, update_hint, time_millis):
        if update_hint == self.last_update:
            return

        self.last_update = update_hint

        if len(self.times) == self.times.maxlen:
            self.times.popleft()
        self.times.append(time_millis)
        if self.times[-1] != self.times[0]:
            self.rate = len(self.times) / ((self.times[-1] - self.times[0]) / 1e3)

strip_ansi_re = re.compile(r'\x1B\[[^a-zA-Z]*[a-zA-Z]')
def strip_ansi_escape_codes(output):
    return strip_ansi_re.sub('', output)

class NinjaNativeFrontend:
    def __init__(self):
        self.total_edges = 0
        self.running_edges = 0
        self.started_edges = 0
        self.finished_edges = 0
        self.running = {}

        self.time_millis = 0

        self.progress_status_format = os.getenv('NINJA_STATUS', '[%f/%t] ')
        self.current_rate = SlidingRateInfo()
        self.console_locked = False

        self.printer = LinePrinter()
        self.verbose = False

    def handle(self, msg):
        handled = False
        if msg.HasField('total_edges'):
            handled = True
            self.total_edges = msg.total_edges.total_edges

        if msg.HasField('build_started'):
            handled = True
            self.verbose = msg.build_started.verbose
            self.current_rate = SlidingRateInfo(msg.build_started.parallelism)
            self.running_edges = 0
            self.started_edges = 0
            self.finished_edges = 0
            self.running = {}

        if msg.HasField('build_finished'):
            handled = True
            self.printer.set_console_locked(False)
            self.printer.print_on_new_line('')

        if msg.HasField('edge_started'):
            handled = True
            self.started_edges += 1
            self.running_edges += 1
            self.running[msg.edge_started.id] = msg.edge_started
            self.time_millis = msg.edge_started.start_time
            if msg.edge_started.console or self.printer.smart_terminal:
                self.print_status(msg.edge_started)
            if msg.edge_started.console:
                self.printer.set_console_locked(True)

        if msg.HasField('edge_finished'):
            handled = True
            self.finished_edges += 1
            self.time_millis = msg.edge_finished.end_time

            edge_started = self.running[msg.edge_finished.id]

            if edge_started.console:
                self.printer.set_console_locked(False)

            if not edge_started.console:
                self.print_status(edge_started)

            self.running_edges -= 1
            del self.running[msg.edge_finished.id]

            # Print the command that is spewing before printing its output.
            if msg.edge_finished.status != 0:
                self.printer.print_on_new_line('FAILED: ' + ' '.join(edge_started.outputs))
                self.printer.print_on_new_line(edge_started.command)

            # ninja sets stdout and stderr of subprocesses to a pipe, to be able to
            # check if the output is empty. Some compilers, e.g. clang, check
            # isatty(stderr) to decide if they should print colored output.
            # To make it possible to use colored output with ninja, subprocesses should
            # be run with a flag that forces them to always print color escape codes.
            # To make sure these escape codes don't show up in a file if ninja's output
            # is piped to a file, ninja strips ansi escape codes again if it's not
            # writing to a |smart_terminal_|.
            # (Launching subprocesses in pseudo ttys doesn't work because there are
            # only a few hundred available on some systems, and ninja can launch
            # thousands of parallel compile commands.)
            # TODO: There should be a flag to disable escape code stripping.
            if msg.edge_finished.output != '':
                if not self.printer.smart_terminal:
                    msg.edge_finished.output = strip_ansi_escape_codes(msg.edge_finished.output)
                self.printer.print_on_new_line(msg.edge_finished.output)

        if msg.HasField('message'):
            handled = True
            # TODO(colincross): get the enum values from proto
            if msg.message.level == 0:
                prefix = 'ninja: '
            elif msg.message.level == 1:
                prefix = 'ninja: warning: '
            elif msg.message.level == 2:
                prefix = 'ninja: error: '
            self.printer.print_line(prefix + msg.message.message, LinePrinter.LINE_FULL)

        if not handled:
            pass


    def format_progress_status(self, fmt):
        out = ''
        fmt_iter = iter(fmt)
        for c in fmt_iter:
            if c == '%':
                c = next(fmt_iter)
                if c == '%':
                    out += c
                elif c == 's':
                    out += str(self.started_edges)
                elif c == 't':
                    out += str(self.total_edges)
                elif c == 'r':
                    out += str(self.running_edges)
                elif c == 'u':
                    out += str(self.total_edges - self.started_edges)
                elif c == 'f':
                    out += str(self.finished_edges)
                elif c == 'o':
                    if self.time_millis > 0:
                        rate = self.finished_edges / (self.time_millis / 1e3)
                        out += '{:.1f}'.format(rate)
                    else:
                        out += '?'
                elif c == 'c':
                    self.current_rate.update_rate(self.finished_edges, self.time_millis)
                    if self.current_rate.rate == -1:
                        out += '?'
                    else:
                        out += '{:.1f}'.format(self.current_rate.rate)
                elif c == 'p':
                    out += '{:3d}%'.format((100 * self.finished_edges) // self.total_edges)
                elif c == 'e':
                    out += '{:.3f}'.format(self.time_millis / 1e3)
                else:
                    raise RuntimeError('unknown placeholder '' + c +'' in $NINJA_STATUS')
            else:
                out += c
        return out

    def print_status(self, edge_started):
        to_print = edge_started.desc
        if self.verbose or to_print == '':
            to_print = edge_started.command

        to_print = self.format_progress_status(self.progress_status_format) + to_print

        self.printer.print_line(to_print, LinePrinter.LINE_FULL if self.verbose else LinePrinter.LINE_ELIDE)


def elide_middle(status, width):
    margin = 3 # Space for '...'.
    if len(status) + margin > width:
        elide_size = (width - margin) // 2
        status = status[0:elide_size] + '...' + status[-elide_size:]
    return status

class LinePrinter(object):
    LINE_FULL = 1
    LINE_ELIDE = 2

    def __init__(self):
        # Whether we can do fancy terminal control codes.
        self.smart_terminal = False

        # Whether the caret is at the beginning of a blank line.
        self.have_blank_line = True

        # Whether console is locked.
        self.console_locked = False

        # Buffered current line while console is locked.
        self.line_buffer = ''

        # Buffered line type while console is locked.
        self.line_type = self.LINE_FULL

        # Buffered console output while console is locked.
        self.output_buffer = ''

        if os.name == 'windows':
            STD_OUTPUT_HANDLE = -11
            self.console = ctypes.windll.kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
            csbi = ctypes.create_string_buffer(22)
            self.smart_terminal = ctypes.windll.kernel32.GetConsoleScreenBufferInfo(self.console, csbi)
        else:
            term = os.getenv('TERM')
            self.smart_terminal = os.isatty(1) and term != '' and term != 'dumb'

    def print_line(self, to_print, line_type):
        if self.console_locked:
            self.line_buffer = to_print
            self.line_type = line_type

        if self.smart_terminal:
            sys.stdout.write('\r') # Print over previous line, if any.

        if self.smart_terminal and line_type == self.LINE_ELIDE:
            if os.name == 'windows':
                csbi = ctypes.create_string_buffer(22)
                ctypes.windll.kernel32.GetConsoleScreenBufferInfo(self.console, csbi)
                (cols, rows) = struct.unpack('hh', csbi.raw)
                to_print = elide_middle(to_print, cols)
                # TODO: windows support
                # We don't want to have the cursor spamming back and forth, so instead of
                # printf use WriteConsoleOutput which updates the contents of the buffer,
                # but doesn't move the cursor position.
                sys.stdout.write(to_print)
                sys.stdout.flush()
            else:
                # Limit output to width of the terminal if provided so we don't cause
                # line-wrapping.
                import fcntl, termios
                winsize = fcntl.ioctl(0, termios.TIOCGWINSZ, '\0'*4)
                (rows, cols) = struct.unpack('hh', winsize)
                to_print = elide_middle(to_print, cols)
                sys.stdout.write(to_print)
                sys.stdout.write('\x1B[K')  # Clear to end of line.
                sys.stdout.flush()

            self.have_blank_line = False
        else:
            sys.stdout.write(to_print + '\n')
            sys.stdout.flush()

    def print_or_buffer(self, to_print):
        if self.console_locked:
            self.output_buffer += to_print
        else:
            sys.stdout.write(to_print)
            sys.stdout.flush()

    def print_on_new_line(self, to_print):
        if self.console_locked or self.line_buffer != '':
            self.output_buffer += self.line_buffer + '\n'
            self.line_buffer = ''
        if not self.have_blank_line:
            self.print_or_buffer('\n')
        if to_print != '':
            self.print_or_buffer(to_print)
        self.have_blank_line = to_print == '' or to_print[0] == '\n'

    def set_console_locked(self, locked):
        if locked == self.console_locked:
            return

        if locked:
            self.print_on_new_line('\n')

        self.console_locked = locked

        if not locked:
            self.print_on_new_line(self.output_buffer)
            if self.line_buffer != '':
                self.print_line(self.line_buffer, self.line_type)
            self.output_buffer = ''
            self.line_buffer = ''

def main():
    native = NinjaNativeFrontend()
    for msg in frontend.Frontend():
        native.handle(msg)

if __name__ == '__main__':
    main()
