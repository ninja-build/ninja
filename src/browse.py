#!/usr/bin/python

"""Simple web server for browsing dependency graph data.

This script is inlined into the final executable and spawned by
it when needed.
"""

import BaseHTTPServer
import subprocess
import sys
import webbrowser

def match_strip(prefix, line):
    assert line.startswith(prefix)
    return line[len(prefix):]

def parse(text):
    lines = text.split('\n')
    node = lines.pop(0)
    node = node[:-1]  # strip trailing colon

    input = []
    if lines and lines[0].startswith('  input:'):
        input.append(match_strip('  input: ', lines.pop(0)))
        while lines and lines[0].startswith('    '):
            input.append(lines.pop(0).strip())

    outputs = []
    while lines:
        output = []
        output.append(match_strip('  output: ', lines.pop(0)))
        while lines and lines[0].startswith('    '):
            output.append(lines.pop(0).strip())
        outputs.append(output)

    return (node, input, outputs)

def generate_html(data):
    node, input, outputs = data
    print '''<!DOCTYPE html>
<style>
body {
    font-family: sans;
    font-size: 0.8em;
    margin: 4ex;
}
h1 {
    font-weight: normal;
    text-align: center;
    margin: 0;
}
h2 {
    font-weight: normal;
}
tt {
    font-family: WebKitHack, monospace;
}
</style>'''
    print '<table><tr><td colspan=3>'
    print '<h1>%s</h1>' % node
    print '</td></tr>'

    print '<tr><td valign=top>'
    print '<h2>input</h2>'
    if input:
        print '<p><tt>%s</tt>:</p>' % input[0]
        print '<ul>'
        for i in input[1:]:
            print '<li><tt><a href="?%s">%s</a></tt></li>' % (i, i)
        print '</ul>'
    print '</td>'
    print '<td width=50>&nbsp;</td>'

    print '<td valign=top>'
    print '<h2>outputs</h2>'
    for output in outputs:
        print '<p><tt>%s</tt>:</p>' % output[0]
        print '<ul>'
        for i in output[1:]:
            print '<li><tt><a href="?%s">%s</a></tt></li>' % (i, i)
        print '</ul>'
    print '</td></tr></table>'

def ninja_dump(target):
    proc = subprocess.Popen(['./ninja', '-q', target], stdout=subprocess.PIPE)
    return proc.communicate()[0]

class RequestHandler(BaseHTTPServer.BaseHTTPRequestHandler):
    def do_GET(self):
        assert self.path[0] == '/'
        target = self.path[1:]

        if target == '':
            self.send_response(302)
            self.send_header('Location', '?' + sys.argv[1])
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


