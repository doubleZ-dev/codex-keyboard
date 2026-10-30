"""Windows endpoint controls for an explicitly identified Codex board microphone only."""
import argparse
import ctypes as c
import json
import uuid


class Guid(c.Structure):
    _fields_=[('a',c.c_uint32),('b',c.c_uint16),('d',c.c_uint16),('tail',c.c_ubyte*8)]
    @classmethod
    def of(cls,text): return cls.from_buffer_copy(uuid.UUID(text).bytes_le)


def check(result):
    if result < 0: raise OSError(f'CoreAudio HRESULT 0x{result & 0xffffffff:08x}')


def invoke(ptr,index,types,*args):
    table=c.cast(ptr,c.POINTER(c.POINTER(c.c_void_p))).contents
    return c.WINFUNCTYPE(c.c_long,c.c_void_p,*types)(table[index])(ptr,*args)


class BoardLevel:
    def __init__(self,endpoint_id):
        self.pointers=[]
        self.ole=c.OleDLL('ole32')
        check(self.ole.CoInitializeEx(None,0))
        self.initialized=True
        try:
            enumerator=c.c_void_p()
            clsid=Guid.of('bcde0395-e52f-467c-8e3d-c4579291692e')
            iid=Guid.of('a95664d2-9614-4f35-a746-de8db63617e6')
            check(self.ole.CoCreateInstance(c.byref(clsid),None,23,c.byref(iid),c.byref(enumerator)))
            self.pointers.append(enumerator)
            device=c.c_void_p()
            check(invoke(enumerator,5,[c.c_wchar_p,c.POINTER(c.c_void_p)],endpoint_id,c.byref(device)))
            self.pointers.append(device)
            # Read PKEY_Device_FriendlyName before accepting any mutation.
            store=c.c_void_p()
            check(invoke(device,4,[c.c_uint32,c.POINTER(c.c_void_p)],0,c.byref(store)))
            self.pointers.append(store)
            class PropertyKey(c.Structure): _fields_=[('fmtid',Guid),('pid',c.c_uint32)]
            class PropVariant(c.Structure):
                _fields_=[('vt',c.c_uint16),('r1',c.c_uint16),('r2',c.c_uint16),('r3',c.c_uint16),('value',c.c_void_p),('extra',c.c_void_p)]
            key=PropertyKey(Guid.of('a45c254e-df1c-4efd-8020-67d146a850e0'),14)
            value=PropVariant()
            check(invoke(store,5,[c.POINTER(PropertyKey),c.POINTER(PropVariant)],c.byref(key),c.byref(value)))
            try:
                if value.vt!=31: raise ValueError('Endpoint has no string friendly name')
                self.name=c.wstring_at(value.value)
            finally:self.ole.PropVariantClear(c.byref(value))
            if 'codex micro' not in self.name.casefold() and 'inmp441' not in self.name.casefold():
                raise ValueError('Refusing to control another audio endpoint: '+self.name)
            volume=c.c_void_p();vid=Guid.of('5cdf2c82-841e-4546-9722-0cf74078229a')
            check(invoke(device,3,[c.POINTER(Guid),c.c_uint32,c.c_void_p,c.POINTER(c.c_void_p)],
                         c.byref(vid),23,None,c.byref(volume)))
            self.pointers.append(volume);self.volume=volume
        except BaseException:
            self.close();raise
    def status(self):
        level=c.c_float();low=c.c_float();high=c.c_float();step=c.c_float();mute=c.c_int()
        check(invoke(self.volume,8,[c.POINTER(c.c_float)],c.byref(level)))
        check(invoke(self.volume,20,[c.POINTER(c.c_float)]*3,c.byref(low),c.byref(high),c.byref(step)))
        check(invoke(self.volume,15,[c.POINTER(c.c_int)],c.byref(mute)))
        return dict(name=self.name,db=level.value,min_db=low.value,max_db=high.value,step_db=step.value,mute=bool(mute.value))
    def set_db(self,level):
        status=self.status()
        if not status['min_db']<=level<=status['max_db']:raise ValueError('Gain outside endpoint range')
        check(invoke(self.volume,6,[c.c_float,c.c_void_p],level,None))
    def set_mute(self,muted):
        check(invoke(self.volume,14,[c.c_int,c.c_void_p],int(muted),None))
    def close(self):
        for ptr in reversed(self.pointers):invoke(ptr,2,[])
        self.pointers=[]
        if getattr(self,'initialized',False):self.ole.CoUninitialize();self.initialized=False


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--endpoint-id',required=True)
    parser.add_argument('--gain-db',type=float)
    mute=parser.add_mutually_exclusive_group();mute.add_argument('--mute',action='store_true');mute.add_argument('--unmute',action='store_true')
    args=parser.parse_args()
    endpoint=BoardLevel(args.endpoint_id)
    try:
        before=endpoint.status()
        if args.gain_db is not None:endpoint.set_db(args.gain_db)
        if args.mute or args.unmute:endpoint.set_mute(args.mute)
        print(json.dumps(dict(before=before,after=endpoint.status()),ensure_ascii=False))
    finally:endpoint.close()


if __name__=='__main__':main()
