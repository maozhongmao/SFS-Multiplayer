import json,sys
d=json.load(open('/mnt/d/SFS-Multiplayer/android/dump/script.json',encoding='utf-8'))
lo=int(sys.argv[1],0); hi=int(sys.argv[2],0)
ms=sorted((int(e['Address']),e['Name']) for e in d['ScriptMethod'])
for a,n in ms:
    if lo<=a<=hi: print(hex(a),n)
