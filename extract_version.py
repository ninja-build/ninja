import subprocess
p = subprocess.Popen(['git', 'describe', '--always'], shell=True,
                     stdout=subprocess.PIPE)
version = p.communicate()[0].strip()
f = open('src/version.h', 'w')
print >>f, 'const char kVersionString[] = "%s";' % version
f.close()
