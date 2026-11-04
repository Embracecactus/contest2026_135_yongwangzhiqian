#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""4K warm-paper/red motion edit. Sources stay in their original locations."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import wave

HERE=Path(__file__).resolve().parent
BASE=HERE.parent
REPO=HERE.parents[4]
WORKSPACE=REPO.parent
ASSETS=HERE/'assets'
CACHE=HERE/'render-cache'
LOGS=HERE/'verification'
FONT=HERE.parents[1]/'素材/fonts/MiSans-Medium.ttf'
SPEC=json.loads((HERE/'edit.json').read_text())
W,H,FPS=SPEC['width'],SPEC['height'],SPEC['fps']
ENC=['-c:v','h264_nvenc','-preset','p5','-rc','vbr','-cq','19','-b:v','0','-maxrate','38M','-bufsize','76M','-pix_fmt','yuv420p','-profile:v','high','-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709','-video_track_timescale','30000']

def run(args,log):
    with log.open('wb') as f:
        p=subprocess.run([str(s) for s in args],stdout=f,stderr=subprocess.STDOUT)
    if p.returncode:raise RuntimeError(f'{p.returncode}: {log}')

def probe(p):
    return json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-show_chapters','-of','json',str(p)]))

def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()

def timeline():
    n=0;rows=[]
    for value in SPEC['timeline']:
        r=dict(value);r['frames']=round(r.get('duration',r.get('out',0)-r.get('in',0))*FPS)
        r['duration']=r['frames']/FPS;r['start']=n/FPS;n+=r['frames'];rows.append(r)
    return rows

def source(key,videos):
    if key in ('approval','workbench'):return BASE/'assets'/f'{key}-normalized.mp4'
    s=SPEC['sources'][key]
    return (WORKSPACE if s['root']=='workspace' else videos)/s['path']

def stamp(t):
    n=round(t*1000);return f'{n//3600000:02}:{n//60000%60:02}:{n//1000%60:02},{n%1000:03}'

def captions(videos):
    out=[]
    for row in timeline():
        if 'source' not in row:continue
        s=SPEC['sources'][row['source']];p=(WORKSPACE if s['root']=='workspace' else videos)/s['srt']
        for block in re.split(r'\n\s*\n',p.read_text(encoding='utf-8-sig').strip()):
            ls=block.splitlines()
            if len(ls)<3 or '-->' not in ls[1]:continue
            def sec(v):
                h,m,t=v.strip().replace(',','.').split(':');return int(h)*3600+int(m)*60+float(t)
            a,b=map(sec,ls[1].split('-->'));a=max(a,row['in']);b=min(b,row['out'])
            if b<=a:continue
            body='\n'.join(ls[2:]).replace('眼镜素材包','眼睛素材包')
            out.append(f'{len(out)+1}\n{stamp(row["start"]+a-row["in"])} --> {stamp(row["start"]+b-row["in"])}\n{body}\n')
    (HERE/'中文字幕-4K时间轴.srt').write_text('\n'.join(out),encoding='utf-8')

def prepare(videos):
    from PIL import Image,ImageDraw,ImageFont,ImageFilter
    import numpy as np
    for p in (ASSETS,CACHE,LOGS):p.mkdir(exist_ok=True)
    font=lambda s:ImageFont.truetype(str(FONT),s)
    for key,(chapter,period,title) in SPEC['sections'].items():
        frame=Image.new('RGBA',(W,H),'#F5F0E8');d=ImageDraw.Draw(frame)
        d.rectangle((160,140,3679,2119),fill=(0,0,0,0))
        d.ellipse((160,50,178,68),fill='#CB4139')
        d.text((205,39),chapter,font=font(43),fill='#CB4139')
        d.text((540,39),title,font=font(43),fill='#252A2D')
        d.text((3680,48),period,font=font(32),fill='#8B8074',anchor='ra')
        d.line((160,2139,3680,2139),fill='#DCD2C6',width=4)
        frame.save(ASSETS/f'frame-{key}.png')
    for row in timeline():
        for index,g in enumerate(row.get('guides',[])):
            im=Image.new('RGBA',(800,166));d=ImageDraw.Draw(im)
            d.rounded_rectangle((4,6,796,162),radius=28,fill='#00000014')
            im=im.filter(ImageFilter.GaussianBlur(7));d=ImageDraw.Draw(im)
            d.rounded_rectangle((6,0,792,147),radius=25,fill='#FFFDFAF5',outline='#E7DCD1',width=2)
            d.rounded_rectangle((27,24,34,123),radius=3,fill='#CB4139')
            d.text((61,22),g['title'],font=font(45),fill='#252A2D')
            d.text((61,83),g['detail'],font=font(32),fill='#8B8074')
            im.save(ASSETS/f'{row["id"]}-guide-{index}.png')
    # Original, quiet UI accents, only under the illustrated chapter scenes.
    cues={'intro':[.3,1.6,3.5],'app':[1,1.7,2.4],'approval':[1,2.6,4.5,5.5],'workbench':[.4,1.7,3.15,3.8],'outro':[.4,1.2]}
    for row in timeline():
        if 'scene' not in row or 'source' in row:continue
        name=row['scene'];rate=48000;y=np.zeros(round(row['duration']*rate),dtype=np.float64)
        for i,start in enumerate(cues[name]):
            t=np.arange(round(.45*rate))/rate;freq=[659.25,783.99,987.77,1174.66][i%4]
            env=(1-np.exp(-t*100))*np.exp(-t*12)
            tone=.045*env*(np.sin(2*np.pi*freq*t)+.24*np.sin(2*np.pi*freq*2*t))
            offset=round(start*rate);n=min(len(tone),len(y)-offset);y[offset:offset+n]+=tone[:n]
        stereo=np.stack([y,y],axis=1)
        with wave.open(str(ASSETS/f'{name}-accent.wav'),'wb') as f:
            f.setnchannels(2);f.setsampwidth(2);f.setframerate(rate);f.writeframes((stereo*32767).astype('<i2').tobytes())
    captions(videos)
    architecture=next(r for r in timeline() if r.get('scene')=='architecture')
    run(['ffmpeg','-v','error','-y','-ss',str(architecture['in']),'-i',source('main',videos),'-t',str(architecture['duration']),'-vn','-c:a','pcm_s16le','-ar','48000','-ac','2',ASSETS/'architecture-original.wav'],LOGS/'extract-architecture-audio.log')
    (HERE/'timeline.json').write_text(json.dumps(timeline(),ensure_ascii=False,indent=2)+'\n')
    inputs={}
    for key,s in SPEC['sources'].items():
        root=WORKSPACE if s['root']=='workspace' else videos
        paths=[s['path'],s['srt']]+([s['audio']] if 'audio' in s else [])
        inputs[key]=[{'path':p,'bytes':(root/p).stat().st_size,'sha256':sha(root/p)} for p in paths]
    (HERE/'sources.json').write_text(json.dumps(inputs,ensure_ascii=False,indent=2)+'\n')
    print('Prepared native 4K frame graphics, guide cards, sound accents and timeline.')

def smooth_expr(x):return f'(pow(clip({x},0,1),2)*(3-2*clip({x},0,1)))'

def render_one(row,videos,preview=False):
    length=min(row['duration'],6) if preview else row['duration']
    output=CACHE/(row['id']+('-preview' if preview else '')+'.mp4')
    args=['ffmpeg','-hide_banner','-loglevel','warning','-y','-filter_complex_threads','2']
    if 'scene' in row:
        args+=['-i',ASSETS/f'{row["scene"]}-4k.mp4']
        if 'source' in row:
            args+=['-ss',str(row['in']),'-i',source(row['source'],videos)];gain=SPEC['sources'][row['source']]['gain_db']
        else:
            args+=['-i',ASSETS/f'{row["scene"]}-accent.wav'];gain=0
        args+=['-map','0:v:0','-map','1:a:0','-c:v','copy','-af',f'volume={gain}dB']
    else:
        args+=['-ss',str(row['in']),'-i',source(row['source'],videos),'-loop','1','-framerate','30','-i',ASSETS/f'frame-{row["section"]}.png']
        for i,g in enumerate(row.get('guides',[])):args+=['-loop','1','-framerate','30','-i',ASSETS/f'{row["id"]}-guide-{i}.png']
        if row.get('zoom'):
            if row['zoom']=='approval':
                a=smooth_expr('(on/30-19.0)/0.8');b=smooth_expr('(on/30-28.7)/0.8');q=smooth_expr('(on/30-32.5)/0.8')
                z=f'1+0.16*{a}*(1-{b})+0.15*{q}'
            else:z=f'1+0.12*{smooth_expr("(on/30-18.2)/0.8")}'
            scale=f"zoompan=z='{z}':x='iw-iw/zoom':y='ih-ih/zoom':d=1:s=3520x1980:fps=30"
        else:scale='scale=3520:1980:flags=lanczos'
        graph=[f'[0:v]setpts=PTS-STARTPTS,fps=30,{scale},setsar=1,pad=3840:2160:160:140:color=0xF5F0E8[base]',
               '[base][1:v]overlay=0:0:shortest=1[v0]']
        last='v0'
        for i,g in enumerate(row.get('guides',[])):
            start,end=g['start'],g['end'];guide=f'g{i}';nextv=f'v{i+1}'
            graph.append(f'[{i+2}:v]format=rgba,fade=t=in:st={start}:d=0.25:alpha=1,fade=t=out:st={max(start,end-.25)}:d=0.25:alpha=1[{guide}]')
            x=f'2880+180*pow(1-clip((t-{start})/0.45,0,1),3)'
            graph.append(f"[{last}][{guide}]overlay=x='{x}':y=185:enable='between(t,{start},{end})':shortest=1[{nextv}]")
            last=nextv
        # The outer-frame baseline stays clear of the source captions.
        graph.append(f"[{last}]drawbox=x=160:y=2137:w=3520:h=4:color=0xDCD2C6:t=fill,format=yuv420p[v]")
        args+=['-filter_complex',';'.join(graph),'-map','[v]','-map','0:a:0',*ENC]
        gain=SPEC['sources'][row['source']]['gain_db']
        args+=['-af',f'volume={gain}dB,afade=t=in:d=0.025,afade=t=out:st={max(0,length-.025)}:d=0.025']
    args+=['-t',str(length),'-c:a','aac','-b:a','192k','-ar','48000','-ac','2','-movflags','+faststart',output]
    run(args,LOGS/(output.stem+'.log'));print('Rendered',row['id'],flush=True)
    return output

def assemble(delivery):
    rows=timeline();length=sum(r['duration'] for r in rows)
    for r in rows:assert (CACHE/(r['id']+'.mp4')).is_file(),r['id']
    listing=CACHE/'concat.txt';listing.write_text('ffconcat version 1.0\n'+''.join(f"file '{CACHE/(r['id']+'.mp4')}'\nduration {r['duration']:.6f}\n" for r in rows))
    chapters=[r for r in rows if 'chapter' in r];meta=';FFMETADATA1\ntitle=傻妞 · 决赛演示 · 4K 动画版\nartist=勇往直前\n'
    for i,r in enumerate(chapters):
        end=chapters[i+1]['start'] if i+1<len(chapters) else length
        meta+=f'[CHAPTER]\nTIMEBASE=1/1000\nSTART={round(r["start"]*1000)}\nEND={round(end*1000)}\ntitle={r["chapter"]}\n'
    (CACHE/'chapters.txt').write_text(meta)
    delivery.mkdir(exist_ok=True,parents=True);out=delivery/SPEC['output_name']
    run(['ffmpeg','-hide_banner','-loglevel','warning','-y','-f','concat','-safe','0','-i',listing,'-f','ffmetadata','-i',CACHE/'chapters.txt','-map','0:v:0','-map','0:a:0','-map_metadata','1','-map_chapters','1','-c:v','copy','-c:a','aac','-b:a','192k','-ar','48000','-af','aresample=async=1:first_pts=0,alimiter=limit=0.794328:attack=5:release=60:level=0','-t',str(length),'-movflags','+faststart',out],LOGS/'assemble.log')
    print('MP4:',out)

def make_draft(videos):
    skill=Path(os.environ.get('JY_SKILL_ROOT',Path.home()/'.codex/skills/jianying-editor-skill'))
    sys.path.insert(0,str(skill/'scripts'))
    from jy_wrapper import JyProject
    import pyJianYingDraft as draft
    from pyJianYingDraft import KeyframeProperty as KP
    root=videos.parent/'AppData/Local/JianyingPro/User Data/Projects/com.lveditor.draft'
    name=SPEC['draft_name'];owned=HERE/'jianying-project.json'
    if (root/name).exists() and not owned.exists():raise RuntimeError('Preserving an unrelated existing draft')
    project=JyProject(name,width=W,height=H,drafts_root=str(root),overwrite=True);project._explicit_res=True;project.script.fps=FPS
    for r in timeline():
        start=r['start'];duration=r['duration']
        if 'scene' in r:
            p=ASSETS/f'{r["scene"]}-4k.mp4';source_start=0
        else:p=source(r['source'],videos);source_start=r['in']
        seg=project.add_clip(str(p),f'{source_start}s',f'{duration}s',target_start=f'{start}s',track_name='原片与动态章节')
        if seg is None:raise RuntimeError(p)
        if 'scene' in r:
            if 'source' in r:
                audio=project.add_media_safe(str(ASSETS/'architecture-original.wav'),f'{start}s',f'{duration}s',track_name='架构原声')
                if audio is None:raise RuntimeError('Architecture narration import failed')
                audio.volume=10**(SPEC['sources'][r['source']]['gain_db']/20)
            else:
                audio=project.add_media_safe(str(ASSETS/f'{r["scene"]}-accent.wav'),f'{start}s',f'{duration}s',track_name='原创轻音效')
                if audio is None:raise RuntimeError('Accent import failed')
        else:
            seg.volume=10**(SPEC['sources'][r['source']]['gain_db']/20)
            seg.clip_settings=draft.ClipSettings(scale_x=3520/3840,scale_y=3520/3840,transform_y=-50/1080)
            if r.get('zoom'):
                def sm(x):
                    x=max(0,min(1,x));return x*x*(3-2*x)
                points=[0,19,19.2,19.4,19.6,19.8,28.7,28.9,29.1,29.3,29.5,32.5,32.7,32.9,33.1,33.3,duration] if r['zoom']=='approval' else [0,18.2,18.4,18.6,18.8,19,duration]
                for at in points:
                    z=1+.16*sm((at-19)/.8)*(1-sm((at-28.7)/.8))+.15*sm((at-32.5)/.8) if r['zoom']=='approval' else 1+.12*sm((at-18.2)/.8)
                    us=round(at*1e6)
                    seg.add_keyframe(KP.scale_x,us,3520/3840*z)
                    seg.add_keyframe(KP.scale_y,us,3520/3840*z)
                    seg.add_keyframe(KP.position_x,us,-1760*(z-1)/1920)
                    seg.add_keyframe(KP.position_y,us,(990*z-1040)/1080)
            frame=project.add_clip(str(ASSETS/f'frame-{r["section"]}.png'),'0s',f'{duration}s',target_start=f'{start}s',track_name='暖白画框与章节')
            if frame is None:raise RuntimeError('Frame import failed')
            for i,g in enumerate(r.get('guides',[])):
                t=start+g['start'];dur=g['end']-g['start']
                card=project.add_clip(str(ASSETS/f'{r["id"]}-guide-{i}.png'),'0s',f'{dur}s',target_start=f'{t}s',track_name='动态操作导读')
                if card is None:raise RuntimeError('Guide import failed')
                # Image importing fits to canvas; restore the desired 800 px width.
                card.clip_settings=draft.ClipSettings(scale_x=800/3840,scale_y=800/3840,transform_x=1360/1920,transform_y=812/1080)
                card.add_keyframe(KP.position_x,0,1450/1920)
                card.add_keyframe(KP.position_x,450000,1360/1920)
                card.add_keyframe(KP.alpha,0,0)
                card.add_keyframe(KP.alpha,250000,1)
                card.add_keyframe(KP.alpha,round((dur-.25)*1e6),1)
                card.add_keyframe(KP.alpha,round(dur*1e6),0)
    result=project.save();folder=root/name
    def paths(v):
        if isinstance(v,dict):return {k:paths(x) for k,x in v.items()}
        if isinstance(v,list):return [paths(x) for x in v]
        if isinstance(v,str) and v.startswith('/') and Path(v).exists():return subprocess.check_output(['wslpath','-w',v],text=True).strip()
        return v
    snap=HERE/'jianying-draft';snap.mkdir(exist_ok=True)
    for p in folder.glob('*.json'):
        p.write_text(json.dumps(paths(json.loads(p.read_text())),ensure_ascii=False,separators=(',',':')));shutil.copy2(p,snap/p.name)
    owned.write_text(json.dumps({'draft_name':name,'draft_root':str(root),'saved':result['status']},ensure_ascii=False,indent=2)+'\n')
    print('Saved editable 4K draft:',folder)

def verify(delivery):
    p=delivery/SPEC['output_name'];m=probe(p);streams=m['streams']
    v=next(s for s in streams if s['codec_type']=='video');a=next(s for s in streams if s['codec_type']=='audio')
    expected=sum(r['duration'] for r in timeline())
    assert (v['width'],v['height'],v['codec_name'],v['r_frame_rate'])==(3840,2160,'h264','30/1')
    assert a['codec_name']=='aac' and a['sample_rate']=='48000'
    assert abs(float(m['format']['duration'])-expected)<.2
    run(['ffmpeg','-v','error','-i',p,'-f','null','-'],LOGS/'full-decode.log')
    report={'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p),'duration':m['format']['duration'],'width':3840,'height':2160,'fps':30,'full_decode':'passed','native_4k_graphics':True,'live_sources':'1080p / 720p, resized','animation_seconds':sum(r['duration'] for r in timeline() if 'scene' in r),'new_voiceover':False,'original_ui_sound_accents':True,'normal_speed_manual_playback':False}
    (HERE/'delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');print(json.dumps(report,ensure_ascii=False))

def main():
    p=argparse.ArgumentParser();p.add_argument('stage',choices=['prepare','preview','render','assemble','draft','verify']);p.add_argument('--videos',type=Path,required=True);p.add_argument('--only');args=p.parse_args()
    delivery=args.videos/'傻妞产品交付-20261104'
    if args.stage=='prepare':prepare(args.videos)
    elif args.stage=='preview':render_one(next(r for r in timeline() if r['id']==(args.only or '09-approval')),args.videos,True)
    elif args.stage=='render':
        for row in timeline():
            if args.only and row['id']!=args.only:continue
            if (CACHE/(row['id']+'.mp4')).exists() and not args.only:print('Reuse',row['id'],flush=True);continue
            render_one(row,args.videos)
    elif args.stage=='assemble':assemble(delivery)
    elif args.stage=='draft':make_draft(args.videos)
    else:verify(delivery)

if __name__=='__main__':main()
