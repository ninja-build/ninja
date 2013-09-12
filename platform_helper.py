#!/usr/bin/env python
# Copyright 2011 Google Inc.
# Copyright 2013 Patrick von Reth <vonreth@kde.org>
# All Rights Reserved.
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

import sys

def platforms():
    return ['linux', 'darwin', 'freebsd', 'openbsd', 'solaris', 'sunos5',
            'mingw', 'msvc', 'gnukfreebsd8', 'bitrig']

class Platform( object ):
    def __init__( self, platform):
        self._platform = platform
        if not self._platform is None:
            return
        self._platform = sys.platform
        if self._platform.startswith('linux'):
            self._platform = 'linux'
        elif self._platform.startswith('freebsd'):
            self._platform = 'freebsd'
        elif self._platform.startswith('gnukfreebsd8'):
            self._platform = 'freebsd'
        elif self._platform.startswith('openbsd'):
            self._platform = 'openbsd'
        elif self._platform.startswith('solaris'):
            self._platform = 'solaris'
        elif self._platform.startswith('mingw'):
            self._platform = 'mingw'
        elif self._platform.startswith('win'):
            self._platform = 'msvc'
        elif self._platform.startswith('bitrig'):
            self._platform = 'bitrig'

    def platform(self):
        return self._platform

    def is_linux(self):
        return self._platform == 'linux'

    def is_mingw(self):
        return self._platform == 'mingw'

    def is_msvc(self):
        return self._platform == 'msvc'

    def is_windows(self):
        return self.is_mingw() or self.is_msvc()

    def is_solaris(self):
        return self._platform == 'solaris'

    def is_freebsd(self):
        return self._platform == 'freebsd'

    def is_openbsd(self):
        return self._platform == 'openbsd'

    def is_sunos5(self):
        return self._platform == 'sunos5'

    def is_bitrig(self):
        return self._platform == 'bitrig'
