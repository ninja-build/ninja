
platforms = ['linux', 'freebsd', 'solaris', 'mingw', 'msvc']
_platform = 'linux'

def setPlatform( platform ):
    global _platform
    _platform = platform
    
def isLinux():
    return _platform == 'linux'
    
def isWindows():
    return _platform == 'msvc' or _platform == 'mingw'
    
def isMingw():
    return _platform == 'mingw'
    
def isMSVC():
    return _platform == 'msvc'