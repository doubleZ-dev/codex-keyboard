"""Read-only backup of both committed configuration files and device diagnostics."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
from upgrade import Connection


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--serial')
    args=parser.parse_args()
    connection=Connection(args.serial)
    try:
        report={method:connection.call(method) for method in
                ('sys.version','device.status','sys.selftest','diag.inputs','diag.ble','upgrade.status')}
        listing=connection.call('fs.list')
        files={}
        for name,limit in (('keymap.json',131072),('smart_actions.json',32768)):
            entry=next(item for item in listing if item['name']==name)
            expected=int(entry['size'])
            if not 0<expected<=limit:
                raise RuntimeError('Invalid file size: '+name)
            data=bytearray()
            while len(data)<expected:
                reply=connection.call('fs.readbin',dict(file=name,offset=len(data),len=384))
                chunk=base64.b64decode(reply['data'],validate=True)
                if reply['total_size']!=expected or not chunk or len(chunk)>384 or len(data)+len(chunk)>expected:
                    raise RuntimeError('Inconsistent readback: '+name)
                data.extend(chunk)
            if hashlib.sha1(data).hexdigest()!=entry['checksum']:
                raise RuntimeError('Readback checksum differs from committed file: '+name)
            json.loads(data)
            files[name]=bytes(data)
        if listing!=connection.call('fs.list'):
            raise RuntimeError('Configuration changed during backup; no verified backup saved')
        args.out.mkdir(parents=True,exist_ok=True)
        for name,data in files.items():
            (args.out / name).write_bytes(data)
        report.update(verified=True,read_only=True,files={name:dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest()) for name,data in files.items()})
        (args.out / 'backup.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(version=report['sys.version'],files=report['files'],ble=report['diag.ble'],inputs=report['diag.inputs']),indent=2))
    finally:
        connection.close()


if __name__=='__main__':
    main()
