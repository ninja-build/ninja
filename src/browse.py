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

import BaseHTTPServer
import subprocess
import sys
import webbrowser
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

def parse(text):
    lines = iter(text.split('\n'))

    target = None
    rule = None
    inputs = []
    outputs = []

    try:
        target = lines.next()[:-1]  # strip trailing colon

        line = lines.next()
        (match, rule) = match_strip(line, '  input: ')
        if match:
            (match, line) = match_strip(lines.next(), '    ')
            while match:
                type = None
                (match, line) = match_strip(line, '| ')
                if match:
                    type = 'implicit'
                (match, line) = match_strip(line, '|| ')
                if match:
                    type = 'order-only'
                inputs.append((line, type))
                (match, line) = match_strip(lines.next(), '    ')

        match, _ = match_strip(line, '  outputs:')
        if match:
            (match, line) = match_strip(lines.next(), '    ')
            while match:
                outputs.append(line)
                (match, line) = match_strip(lines.next(), '    ')
    except StopIteration:
        pass

    return Node(inputs, rule, target, outputs)

def generate_html(node):
    print '''<!DOCTYPE html>
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
</style>'''

    print '<h1><tt>%s</tt></h1>' % node.target

    if node.inputs:
        print '<h2>target is built using rule <tt>%s</tt> of</h2>' % node.rule
        if len(node.inputs) > 0:
            print '<div class=filelist>'
            for input, type in sorted(node.inputs):
                extra = ''
                if type:
                    extra = ' (%s)' % type
                print '<tt><a href="?%s">%s</a>%s</tt><br>' % (input, input, extra)
            print '</div>'

    if node.outputs:
        print '<h2>dependent edges build:</h2>'
        print '<div class=filelist>'
        for output in sorted(node.outputs):
            print '<tt><a href="?%s">%s</a></tt><br>' % (output, output)
        print '</div>'

def ninja_dump(target):
    proc = subprocess.Popen([sys.argv[1], '-t', 'query', target],
                            stdout=subprocess.PIPE)
    return proc.communicate()[0]

class RequestHandler(BaseHTTPServer.BaseHTTPRequestHandler):
    def do_GET(self):
        assert self.path[0] == '/'
        target = self.path[1:]

        if target == '':
            self.send_response(302)
            self.send_header('Location', '?' + sys.argv[2])
            self.end_headers()
            return

        if not target.startswith('?'):
            self.send_response(404)
            self.end_headers()
            return
        target = target[1:]

        input = ninja_dump(target)

        self.send_response(200)
        self.end_headers()
        stdout = sys.stdout
        sys.stdout = self.wfile
        try:
            generate_html(parse(input.strip()))
        finally:
            sys.stdout = stdout

    def log_message(self, format, *args):
        pass  # Swallow console spam.

port = 8000
httpd = BaseHTTPServer.HTTPServer(('',port), RequestHandler)
try:
    print 'Web server running on port %d, ctl-C to abort...' % port
    webbrowser.open_new('http://localhost:%s' % port)
    httpd.serve_forever()
except KeyboardInterrupt:
    print
    pass  # Swallow console spam.


