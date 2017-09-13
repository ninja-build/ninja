#!/usr/bin/env python
#
# Copyright 2001 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Simple web server for browsing dependency graph data.

This script is inlined into the final executable and spawned by
it when needed.
"""

from __future__ import print_function

try:
    import http.server as httpserver
except ImportError:
    import BaseHTTPServer as httpserver
import argparse
import cgi
import os
import socket
import subprocess
import sys
import webbrowser
try:
    from urllib.request import unquote
except ImportError:
    from urllib2 import unquote
from collections import namedtuple

Node = namedtuple('Node', ['inputs', 'rule', 'target', 'outputs'])

# Ideally we'd allow you to navigate to a build edge or a build node,
# with appropriate views for each.  But there's no way to *name* a build
# edge so we can only display nodes.
#
# For a given node, it has at most one input edge, which has n
# different inputs.  This becomes node.inputs.  (We leave out the
# outputs of the input edge due to what follows.)  The node can have
# multiple dependent output edges.  Rather than attempting to display
# those, they are summarized by taking the union of all their outputs.
#
# This means there's no single view that shows you all inputs and outputs
# of an edge.  But I think it's less confusing than alternatives.

def match_strip(line, prefix):
    if not line.startswith(prefix):
        return (False, line)
    return (True, line[len(prefix):])

def html_escape(text):
    return cgi.escape(text, quote=True)

def parse(text):
    lines = iter(text.split('\n'))

    target = None
    rule = None
    inputs = []
    outputs = []

    try:
        target = next(lines)[:-1]  # strip trailing colon

        line = next(lines)
        (match, rule) = match_strip(line, '  input: ')
        if match:
            (match, line) = match_strip(next(lines), '    ')
            while match:
                type = None
                (match, line) = match_strip(line, '| ')
                if match:
                    type = 'implicit'
                (match, line) = match_strip(line, '|| ')
                if match:
                    type = 'order-only'
                inputs.append((line, type))
                (match, line) = match_strip(next(lines), '    ')

        match, _ = match_strip(line, '  outputs:')
        if match:
            (match, line) = match_strip(next(lines), '    ')
            while match:
                outputs.append(line)
                (match, line) = match_strip(next(lines), '    ')
    except StopIteration:
        pass

    return Node(inputs, rule, target, outputs)

def create_page(body):
    return '''<!DOCTYPE html>
<style>
body {
    font-family: sans;
    font-size: 0.8em;
    margin: 4ex;
}
h1 {
    font-weight: normal;
    font-size: 140%;
    text-align: center;
    margin: 0;
}
h2 {
    font-weight: normal;
    font-size: 120%;
}
tt {
    font-family: WebKitHack, monospace;
    white-space: nowrap;
}
.filelist {
  -webkit-columns: auto 2;
}
</style>
''' + body

def generate_html(node):
    document = ['<h1><tt>%s</tt></h1>' % html_escape(node.target)]

    if node.inputs:
        document.append('<h2>target is built using rule <tt>%s</tt> of</h2>' %
                        html_escape(node.rule))
        if len(node.inputs) > 0:
            document.append('<div class=filelist>')
            for input, type in sorted(node.inputs):
                extra = ''
                if type:
                    extra = ' (%s)' % html_escape(type)
                document.append('<tt><a href="?%s">%s</a>%s</tt><br>' %
                                (html_escape(input), html_escape(input), extra))
            document.append('</div>')

    if node.outputs:
        document.append('<h2>dependent edges build:</h2>')
        document.append('<div class=filelist>')
        for output in sorted(node.outputs):
            document.append('<tt><a href="?%s">%s</a></tt><br>' %
                            (html_escape(output), html_escape(output)))
        document.append('</div>')

    return '\n'.join(document)

def ninja_dump(target):
    cmd = [args.ninja_command, '-f', args.f, '-t', 'query', target]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            universal_newlines=True)
    return proc.communicate() + (proc.returncode,)

class RequestHandler(httpserver.BaseHTTPRequestHandler):
    def do_GET(self):
        assert self.path[0] == '/'
        target = unquote(self.path[1:])

        if target == '':
            self.send_response(302)
            self.send_header('Location', '?' + args.initial_target)
            self.end_headers()
            return

        if not target.startswith('?'):
            self.send_response(404)
            self.end_headers()
            return
        target = target[1:]

        ninja_output, ninja_error, exit_code = ninja_dump(target)
        if exit_code == 0:
            page_body = generate_html(parse(ninja_output.strip()))
        else:
            # Relay ninja's error message.
            page_body = '<h1><tt>%s</tt></h1>' % html_escape(ninja_error)

        self.send_response(200)
        self.end_headers()
        self.wfile.write(create_page(page_body).encode('utf-8'))

    def log_message(self, format, *args):
        pass  # Swallow console spam.

parser = argparse.ArgumentParser(prog='ninja -t browse')
parser.add_argument('--port', '-p', default=8000, type=int,
    help='Port number to use (default %(default)d)')
parser.add_argument('--hostname', '-a', default='localhost', type=str,
    help='Hostname to bind to (default %(default)s)')
parser.add_argument('--no-browser', action='store_true',
    help='Do not open a webbrowser on startup.')

parser.add_argument('--ninja-command', default='ninja',
    help='Path to ninja binary (default %(default)s)')
parser.add_argument('-f', default='build.ninja',
    help='Path to build.ninja file (default %(default)s)')
parser.add_argument('initial_target', default='all', nargs='?',
    help='Initial target to show (default %(default)s)')

args = parser.parse_args()
port = args.port
hostname = args.hostname
httpd = httpserver.HTTPServer((hostname,port), RequestHandler)
try:
    if hostname == "":
        hostname = socket.gethostname()
    print('Web server running on %s:%d, ctl-C to abort...' % (hostname,port) )
    print('Web server pid %d' % os.getpid(), file=sys.stderr )
    if not args.no_browser:
        webbrowser.open_new('http://%s:%s' % (hostname, port) )
    httpd.serve_forever()
except KeyboardInterrupt:
    print()
    pass  # Swallow console spam.


