#!/usr/bin/env python

"""Ninja frontend interface.

This module implements a Ninja frontend interface that delegates handling each
message to a handler object
"""

import os
import sys

import google.protobuf.descriptor_pb2
import google.protobuf.message_factory

def default_reader():
    fd = 3
    return os.fdopen(fd, 'rb', 0)

class Frontend(object):
    """Generator class that parses length-delimited ninja status messages
    through a ninja frontend interface.
    """

    def __init__(self, reader=default_reader()):
        self.reader = reader
        self.status_class = self.get_status_proto()

    def get_status_proto(self):
        fd_set = google.protobuf.descriptor_pb2.FileDescriptorSet()
        descriptor = os.path.join(os.path.dirname(__file__), 'frontend.pb')
        with open(descriptor, 'rb') as f:
            fd_set.ParseFromString(f.read())

        if len(fd_set.file) != 1:
            raise RuntimeError('expected exactly one file descriptor in ' + descriptor)

        messages = google.protobuf.message_factory.GetMessages(fd_set.file)
        return messages['ninja.Status']

    def __iter__(self):
        return self

    def __next__(self):
        return self.next()

    def next(self):
        size = 0
        shift = 0
        while True:
            byte = bytearray(self.reader.read(1))
            if len(byte) == 0:
                raise StopIteration()

            byte = byte[0]
            size += (byte & 0x7f) << (shift * 7)
            if (byte & 0x80) == 0:
                break
            shift += 1
            if shift > 4:
                raise RuntimeError('Expected varint32 length-delimeted message')

        message = self.reader.read(size)
        if len(message) != size:
            raise EOFError('Unexpected EOF reading %d bytes' % size)

        return self.status_class.FromString(message)
