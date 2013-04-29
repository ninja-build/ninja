import sys

def platforms():
    return ['linux', 'freebsd', 'solaris', 'sunos5', 'mingw', 'msvc']
        
class platform( object ):
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

    def isLinux(self):
        return self._platform == 'linux'

    def isMingw(self):
        return self._platform == 'mingw'

    def isMSVC(self):
        return self._platform == 'msvc'

    def isWindows(self):
        return self.isMingw() or self.isMSVC()

    def isSolaris(self):
        return self._platform == 'solaris'

    def isFreebsd(self):
        return self._platform == 'freebsd'

    def isSunos5(self):
        return self._platform == 'sunos5'
