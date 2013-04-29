import sys

def platforms():
    return ['linux', 'freebsd', 'solaris', 'sunos5', 'mingw', 'msvc']
        
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
        elif self._platform.startswith('solaris'):
            self._platform = 'solaris'
        elif self._platform.startswith('mingw'):
            self._platform = 'mingw'
        elif self._platform.startswith('win'):
            self._platform = 'msvc'


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

    def is_sunos5(self):
        return self._platform == 'sunos5'
