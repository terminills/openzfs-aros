import json, socket, sys, time
from pathlib import Path
from PIL import Image
run = Path('/run/media/terminills/Amiwest/openzfs-source-replay-20261028/build/native-gui-run.vYw278Jq')
s = socket.socket(socket.AF_UNIX)
s.connect(str(run/'qmp.sock'))
f = s.makefile('rwb',buffering=0)
def call(c,a=None):
    f.write((json.dumps({'execute':c,'arguments':a or {}})+'\n').encode())
    while True:
        r=json.loads(f.readline())
        if 'return' in r:return r
        if 'error' in r:raise RuntimeError(r)
f.readline();call('qmp_capabilities')
if sys.argv[1]=='type':
    for ch in sys.argv[2]:
        key={'_':'shift-minus',':':'shift-semicolon','.':'dot','/':'slash'}.get(ch,ch)
        if ch.isupper():key='shift-'+ch.lower()
        call('human-monitor-command',{'command-line':'sendkey '+key})
        time.sleep(.12)
    name=sys.argv[3]
elif sys.argv[1]=='capture':
    name=sys.argv[2]
else:
    x,y=map(int,sys.argv[1:3]);name=sys.argv[3]
    rx=x*32767/1279;ry=y*32767/799
    for i in range(12):
        call('input-send-event',{'events':[{'type':'abs','data':{'axis':'x','value':max(0,min(32767,int(rx)))}},{'type':'abs','data':{'axis':'y','value':max(0,min(32767,int(ry)))}}]})
        time.sleep(.25)
        call('screendump',{'filename':str(run/'pointer.ppm')})
        im=Image.open(run/'pointer.ppm').convert('RGB')
        points=[(n%im.width,n//im.width) for n,(r,g,b) in enumerate(im.getdata()) if r>150 and g<70 and b<70 and not (595<=n//im.width<=620 and (440<=n%im.width<=475 or 740<=n%im.width<=770))]
        if not points:raise RuntimeError('No red fallback pointer observed')
        px=min(p[0] for p in points);py=min(p[1] for p in points)
        print(i,px,py,flush=True)
        if abs(x-px)<=2 and abs(y-py)<=2:break
        rx+=(x-px)*32767/1279*.8;ry+=(y-py)*32767/799*.8
    else:raise RuntimeError('Pointer did not converge; no click sent')
    for down in (True,False):
        call('input-send-event',{'events':[{'type':'btn','data':{'down':down,'button':'left'}}]});time.sleep(.2)
    time.sleep(.5)
call('screendump',{'filename':str(run/(name+'.ppm'))})
Image.open(run/(name+'.ppm')).save(run/(name+'.png'))
print(run/(name+'.png'))
