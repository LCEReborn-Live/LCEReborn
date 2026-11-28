"""Append PS4 TU80 raid effects to the existing LCE UI without replacing its controls."""
import argparse
import struct
from pathlib import Path
from import_common_update import read_archive, write_archive
from import_tu80_ui import Movie, cstr, putstr

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--archive',type=Path,required=True)
args=parser.parse_args()
source=read_archive(args.source.read_bytes())
archive=read_archive(args.archive.read_bytes())
donor=Movie(source[b'skinHDGraphicsInGame.fui'])
graphics=Movie(archive[b'skinGraphicsInGame.fui'])
icons=['Potion_Effect_Icon_bad_omen_effect','Potion_Effect_Icon_village_hero_effect']
for name in icons:
    if name in graphics.exports(): continue
    kind,index=donor.exports()[name]; assert kind==3
    bitmap=bytearray(donor.regions['bitmap'][index])
    offset,size=struct.unpack_from('<II',bitmap,16)
    symbol=bytearray(72);putstr(symbol,0,name)
    struct.pack_into('<II',symbol,64,3,len(graphics.regions['bitmap']))
    struct.pack_into('<I',bitmap,0,len(graphics.regions['symbol']))
    struct.pack_into('<I',bitmap,16,len(graphics.images))
    graphics.images+=donor.images[offset:offset+size]
    graphics.regions['bitmap'].append(bitmap);graphics.regions['symbol'].append(symbol)
archive[b'skinGraphicsInGame.fui']=graphics.encode()
movie=Movie(archive[b'skinInGame.fui'])
timeline=movie.regions['timeline'][movie.exports()['skinInGame_fla.MobEffectIcon_40'][1]]
start,count=struct.unpack_from('<HH',timeline,4)
if count==27:
    frames=[bytearray(f) for f in movie.regions['frame'][start:start+count]]
    last=frames[-1];event_start,event_count=struct.unpack_from('<II',last,64)
    previous=bytearray(movie.regions['event'][event_start+event_count-1])
    for name in icons:
        reference=bytearray(72);struct.pack_into('<I',reference,0,0xffffffff);putstr(reference,4,name)
        new=bytearray(previous);new[0]=5;new[2]=4
        struct.pack_into('<H',new,6,len(movie.regions['reference']))
        # TU80 shares these 18x18 icons across display resolutions.
        struct.pack_into('<4f',new,12,1,0,0,1)
        movie.regions['reference'].append(reference)
        remove=bytearray(previous);remove[0]=2
        frame=bytearray(72);struct.pack_into('<II',frame,64,len(movie.regions['event']),2)
        movie.regions['event'].extend([remove,new]);frames.append(frame);previous=new
    struct.pack_into('<HH',timeline,4,len(movie.regions['frame']),len(frames))
    movie.regions['frame'].extend(frames)
else: assert count==29
start,count=struct.unpack_from('<HH',timeline,4)
for frame in movie.regions['frame'][start+27:start+29]:
    first,length=struct.unpack_from('<II',frame,64)
    for event in movie.regions['event'][first:first+length]:
        if event[0]==5: struct.pack_into('<4f',event,12,1,0,0,1)
archive[b'skinInGame.fui']=movie.encode()
changed=[]
for key,data in list(archive.items()):
    if not key.endswith(b'.fui') or b'Effect27\x00' not in data: continue
    movie=Movie(data)
    name27={i for i,n in enumerate(movie.regions['name']) if cstr(n)=='Effect27'}
    name28=next((i for i,n in enumerate(movie.regions['name']) if cstr(n)=='Effect28'),None)
    if name28 is None:
        name28=len(movie.regions['name']); name=bytearray(64);putstr(name,0,'Effect28');movie.regions['name'].append(name)
    for frame in movie.regions['frame']:
        start,count=struct.unpack_from('<II',frame,64)
        events=[bytearray(e) for e in movie.regions['event'][start:start+count]]
        if any(struct.unpack_from('<h',e,10)[0]==name28 for e in events): continue
        copies=[]
        for e in events:
            if struct.unpack_from('<h',e,10)[0] in name27:
                clone=bytearray(e);struct.pack_into('<h',clone,10,name28)
                struct.pack_into('<H',clone,4,max(struct.unpack_from('<H',v,4)[0] for v in events)+1)
                copies.append(clone)
        if copies:
            struct.pack_into('<II',frame,64,len(movie.regions['event']),count+len(copies))
            movie.regions['event'].extend(events+copies)
    archive[key]=movie.encode();changed.append(key.decode())
payload=write_archive(archive)
assert read_archive(payload)==archive
args.archive.write_bytes(payload)
print('Imported PS4 raid icons; expanded effect panels:', ', '.join(changed))
