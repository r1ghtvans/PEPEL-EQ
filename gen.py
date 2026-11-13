from PIL import Image, ImageDraw, ImageFont
import math
F="/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
def kanji(ch, thr=0.38):
    S=128
    f=ImageFont.truetype(F, 120, index=2) if False else ImageFont.truetype(F,118)
    im=Image.new("L",(S,S),0); d=ImageDraw.Draw(im)
    bb=d.textbbox((0,0),ch,font=f)
    w,h=bb[2]-bb[0],bb[3]-bb[1]
    d.text(((S-w)//2-bb[0],(S-h)//2-bb[1]),ch,font=f,fill=255)
    bb=im.getbbox(); im=im.crop(bb)
    m=max(im.size); sq=Image.new("L",(m,m),0); sq.paste(im,((m-im.size[0])//2,(m-im.size[1])//2))
    sq=sq.resize((16,16),Image.BOX)
    px=sq.load()
    return ["".join('#' if px[x,y]/255>thr else '.' for x in range(16)) for y in range(16)]
def circ(w,h,cx,cy,r):
    return {(x,y) for y in range(h) for x in range(w) if (x-cx)**2+(y-cy)**2<=r*r}
def crescent():
    a=circ(13,13,6,6,6.2); b=circ(13,13,9.3,4.4,5.4)
    s=a-b
    return ["".join('#' if (x,y) in s else '.' for x in range(13)) for y in range(13)]
def cloud(w,h,blobs):
    s=set()
    for cx,cy,rx,ry in blobs:
        for y in range(h):
            for x in range(w):
                if ((x-cx)/rx)**2+((y-cy)/ry)**2<=1: s.add((x,y))
    for x in range(w): s.add((x,h-1)) if any((x,h-2) in s for _ in [0]) else None
    return ["".join('#' if (x,y) in s else '.' for x in range(w)) for y in range(h)]
sprites={
 "kanjiHai":kanji("灰"),"kanjiAi":kanji("愛"),"crescent":crescent(),
 "cloudS":cloud(9,3,[(3,1.6,3,1.4),(6,2,3.2,1.2)]),
 "cloudM":cloud(15,4,[(4,2.2,4.2,1.9),(8.5,1.6,3.6,1.7),(11.5,2.6,3.6,1.5)]),
 "cloudL":cloud(23,5,[(5,3,5.5,2.2),(11,2.2,5.5,2.4),(17,2.8,5.5,2.2),(20,3.6,3.5,1.5)]),
 "heart":[".##.##.","#######","#######",".#####.","..###..","...#..."],
}
out=["#pragma once","#include <array>","// Сгенерировано gen.py: спрайты (', .' = пусто). Кандзи растеризованы Noto Sans CJK Bold (в плагин не вшит).","namespace sprites {"]
for k,v in sprites.items():
    out.append(f"static constexpr const char* {k}[] = {{")
    for r in v: out.append(f'    "{r}",')
    out.append("};")
    out.append(f"static constexpr int {k}W = {len(v[0])}, {k}H = {len(v)};")
out.append("}")
open("Source/Sprites.h","w").write("\n".join(out)+"\n")
for k in ("kanjiHai","kanjiAi","crescent","cloudL","heart"):
    print(k); print("\n".join(sprites[k]))
