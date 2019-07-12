#!/usr/bin/env python

from __future__ import print_function

import sys

import frontend

def main():
    if len(sys.argv) >= 2:
        f = open(sys.argv[1], 'rb')
    else:
        f = frontend.default_reader()

    for msg in frontend.Frontend(f):
        print('---------------------------------')
        sys.stdout.write(str(msg))

if __name__ == '__main__':
    main()

