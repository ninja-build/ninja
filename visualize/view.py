#!/usr/bin/python2
import json

data = []

for line in open('.ninja_log'):
    if line.startswith('#'):
        continue
    start, end, _, target, _ = line.split('\t', 4)
    data.append({
        'start': int(start),
        'end': int(end),
        'target': target
    })

data.sort(key=lambda row: row['start'])
print 'var data =', json.dumps(data), ';'