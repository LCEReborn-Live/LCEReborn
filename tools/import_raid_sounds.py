"""Preserve PS4 bank events and add missing 1.14.4 raid celebrations/resonance as PCM."""
import argparse
import hashlib
import io
import json
import struct
import subprocess
import urllib.request
import wave
from pathlib import Path
import imageio_ffmpeg
from import_tu80_sounds import ps4_bank, make_addon, WORKSTATIONS, EVENT_ROOT

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--reference',type=Path,default=Path('build/raid-reference'))
args=parser.parse_args()
index=json.loads((args.reference/'asset-index.json').read_text())['objects']
definitions=json.loads((args.reference/'sounds.json').read_text())
events,read_sound=ps4_bank(args.source.read_bytes())
selected={name:value for name,value in events.items() if name.startswith(tuple(EVENT_ROOT+x for x in WORKSTATIONS))}
selected['Minecraft/mob/villager/yes'] = events['Minecraft/mob/villager/yes']
template=events['Minecraft/block/bell/hit'].split(';')
samples={}
mapping={
 'block.bell.resonate':'block/bell/resonate',
 'entity.evoker.celebrate':'mob/evocation_illager/celebrate',
 'entity.vindicator.celebrate':'mob/vindication_illager/celebrate',
 'entity.pillager.celebrate':'mob/pillager/celebrate',
 'entity.ravager.celebrate':'mob/ravager/celebrate',
 'entity.witch.celebrate':'mob/witch/celebrate',
}
cache=args.reference/'sounds';cache.mkdir(exist_ok=True)
for event,target in mapping.items():
    choices=[]
    for i,entry in enumerate(definitions[event]['sounds']):
        entry={'name':entry} if isinstance(entry,str) else entry
        asset='minecraft/sounds/'+entry['name']+'.ogg';digest=index[asset]['hash']
        ogg=cache/(digest+'.ogg');wav=cache/(digest+'.wav')
        if not ogg.exists():ogg.write_bytes(urllib.request.urlopen('https://resources.download.minecraft.net/'+digest[:2]+'/'+digest).read())
        assert hashlib.sha1(ogg.read_bytes()).hexdigest()==digest
        if not wav.exists():subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-i',str(ogg),'-c:a','pcm_s16le','-ac','1',str(wav)],check=True)
        payload=bytearray(wav.read_bytes())
        rate_scale=entry.get('pitch',1)
        if rate_scale!=1:
            offset=12
            while offset+8<len(payload):
                size=struct.unpack_from('<I',payload,offset+4)[0]
                if payload[offset:offset+4]==b'fmt ':
                    rate=round(struct.unpack_from('<I',payload,offset+12)[0]*rate_scale)
                    struct.pack_into('<II',payload,offset+12,rate,rate*2);break
                offset+=8+size+(size&1)
        with wave.open(io.BytesIO(payload)) as audio:
            channels=audio.getnchannels();rate=audio.getframerate();length=round(audio.getnframes()/rate*1000)
        name='Minecraft/java_raid/'+target+'/'+str(i)
        fields=[0,0,0,channels,0xffffffff,rate,len(payload),0,0,length,0,0,0,0x3f800000,0]
        samples[name]=(fields,bytes(payload));choices.extend([name,str(entry.get('weight',1))])
    fields=list(template);fields[3]=':'.join(choices)+':'
    selected['Minecraft/'+target]=';'.join(fields)
args.output.write_bytes(make_addon(selected,lambda name:samples[name] if name in samples else read_sound(name)))
print(f'{len(selected)} events; {len(samples)} verified Java PCM samples added alongside PS4 sounds')
