import hashlib,json,pathlib,re,subprocess,tempfile

root=pathlib.Path('/mnt/short-term/openzfs-bootenv-candidate-20261025')
source=root/'build-audit/qemu-run.recent-recovery.gBEd5SD9'
labels=(source/'labels.log').read_text()
records=re.findall(r'\ttxg = (\d+)\n.*?\tbp = ([^\n]+)',labels,re.S)
txg,bp=max((int(t),b) for t,b in records)
assert txg==632 and 'birth=632L/632P' in bp
dvas=re.findall(r'DVA\[\d\]=<0:([0-9a-f]+):1000>',bp)
assert len(dvas)==3
dest=pathlib.Path(tempfile.mkdtemp(prefix='qemu-run.recent-corrupt.',dir=root/'build-audit'))
with (dest/'fixture.jsonl').open('x') as log:
    log.write(json.dumps({'source':str(source),'txg':txg,'bp':bp})+'\n')
    for unit in range(2):
        member=source/f'member{unit}.raw'
        for index,dva in enumerate(dvas):
            offset=int(dva,16)+4*1024**2
            with member.open('rb') as f:
                f.seek(offset);digest=hashlib.sha256(f.read(4096)).hexdigest()
            output=dest/f'member{unit}-dva{index}.raw'
            result=subprocess.run(['python3',str(root/'aros/engine/corrupt-test-member.py'),str(member),str(output),str(offset),digest],check=True,capture_output=True,text=True)
            log.write(result.stdout);log.flush()
            member=output
        disk=dest/f'disk{unit}.raw'
        subprocess.run(['cp','--reflink=auto','--sparse=always',str(source/f'disk{unit}.raw'),str(disk)],check=True)
        subprocess.run(['dd',f'if={member}',f'of={disk}','bs=512','seek=4097','conv=notrunc','status=none'],check=True)
print(dest)
