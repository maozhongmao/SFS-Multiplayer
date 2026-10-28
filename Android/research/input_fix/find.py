import json,sys
d=json.load(open('/mnt/d/SFS-Multiplayer/android/dump/script.json',encoding='utf-8'))
q=sys.argv[1]
for e in d['ScriptMethod']:
    if q in e['Name']: print(hex(int(e['Address'])), e['Name'])
