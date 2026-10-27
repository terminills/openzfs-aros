#!/usr/bin/env python3
"""Generate a synthetic, path-bounded cache; never open a pool or device."""
import ctypes as c, pathlib, sys
lib=c.CDLL(sys.argv[1])
ptr=c.c_void_p
lib.nvlist_alloc.argtypes=[c.POINTER(ptr),c.c_uint,c.c_int]
lib.nvlist_add_string.argtypes=[ptr,c.c_char_p,c.c_char_p]
lib.nvlist_add_uint64.argtypes=[ptr,c.c_char_p,c.c_uint64]
lib.nvlist_add_nvlist.argtypes=[ptr,c.c_char_p,ptr]
lib.nvlist_add_nvlist_array.argtypes=[ptr,c.c_char_p,c.POINTER(ptr),c.c_uint]
lib.nvlist_pack.argtypes=[ptr,c.POINTER(ptr),c.POINTER(c.c_size_t),c.c_int,c.c_int]
lib.nvlist_free.argtypes=[ptr]
nodes=[]
def node():
    p=ptr();assert lib.nvlist_alloc(c.byref(p),1,0)==0;nodes.append(p);return p
def string(p,key,value):assert lib.nvlist_add_string(p,key.encode(),value.encode())==0
def number(p,key,value):assert lib.nvlist_add_uint64(p,key.encode(),value)==0
try:
    top,config,root,leaf=[node() for _ in range(4)]
    string(config,'name','unadmitted_probe')
    for key,value in [('version',5000),('pool_guid',123456789),('state',0),('txg',1)]:number(config,key,value)
    string(root,'type','root');number(root,'id',0);number(root,'guid',123456789)
    string(leaf,'type','file');string(leaf,'path','/RAM/unadmitted-probe-never-created')
    number(leaf,'id',0);number(leaf,'guid',987654321)
    assert lib.nvlist_add_nvlist_array(root,b'children',(ptr*1)(leaf),1)==0
    assert lib.nvlist_add_nvlist(config,b'vdev_tree',root)==0
    assert lib.nvlist_add_nvlist(top,b'unadmitted_probe',config)==0
    data=ptr();size=c.c_size_t()
    assert lib.nvlist_pack(top,c.byref(data),c.byref(size),1,0)==0
    with pathlib.Path(sys.argv[2]).open('xb') as f:f.write(c.string_at(data,size.value))
    libc=c.CDLL(None);libc.free.argtypes=[ptr];libc.free(data)
    print('Synthetic XDR cache bytes=',size.value,'pool=unadmitted_probe backing=RAM-only-nonexistent')
finally:
    for p in nodes:lib.nvlist_free(p)
