#!/usr/bin/env python3
"""Regenerate the original TaskbarOrder icon. Requires Pillow; not needed to build the app."""
from pathlib import Path
from PIL import Image, ImageDraw

out = Path(__file__).resolve().parents[1] / 'src/controller/assets'
out.mkdir(parents=True, exist_ok=True)
# Bold, flat shapes stay readable in a 16-pixel notification-area icon.
svg = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
<rect x="4" y="4" width="248" height="248" rx="52" fill="#152C4B"/>
<rect x="35" y="80" width="80" height="105" rx="12" fill="#58DED2"/>
<rect x="141" y="80" width="80" height="105" rx="12" fill="#FFCD70"/>
<path d="M49 105h52M155 105h52" stroke="#152C4B" stroke-width="10" stroke-linecap="round"/>
<path d="M50 52h139m-16-16 18 16-18 16M206 211H67m16-16-18 16 18 16" fill="none" stroke="#FFFFFF" stroke-width="14" stroke-linecap="round" stroke-linejoin="round"/>
</svg>'''
(out / 'TaskbarOrder.svg').write_text(svg + '\n')
s = 4
image = Image.new('RGBA', (256*s, 256*s))
d = ImageDraw.Draw(image)
def box(coords, radius, color):
    d.rounded_rectangle(tuple(v*s for v in coords), radius=radius*s, fill=color)
def line(points, color, width):
    points = [(x*s,y*s) for x,y in points]
    d.line(points, fill=color, width=width*s, joint='curve')
    radius=width*s/2
    for x,y in points:
        d.ellipse((x-radius,y-radius,x+radius,y+radius), fill=color)
box((4,4,252,252),52,'#152C4B')
box((35,80,115,185),12,'#58DED2')
box((141,80,221,185),12,'#FFCD70')
line([(49,105),(101,105)],'#152C4B',10)
line([(155,105),(207,105)],'#152C4B',10)
line([(50,52),(189,52)],'white',14)
line([(173,36),(191,52),(173,68)],'white',14)
line([(206,211),(67,211)],'white',14)
line([(83,195),(65,211),(83,227)],'white',14)
image = image.resize((256,256), Image.Resampling.LANCZOS)
image.save(out/'TaskbarOrder.png')
image.save(out/'TaskbarOrder.ico', sizes=[(n,n) for n in (16,20,24,32,40,48,64,128,256)])
