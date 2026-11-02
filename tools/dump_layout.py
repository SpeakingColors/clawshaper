#!/usr/bin/env python3
"""Dumps every control and display rectangle of the approved mockup (both pages) to tools/layout.json.
usage: python3 tools/dump_layout.py <ClawShaper mockup .html>"""
import asyncio, json, sys
from playwright.async_api import async_playwright
src=open(sys.argv[1]).read()
rep=[
 ("w.dataset.tip=o.tip||p.tip||'';","w.dataset.tip=o.tip||p.tip||'';w.dataset.pid=o.id;w.dataset.kind='knob';"),
 ("b.dataset.tip=tip||PS[id].tip;b.setAttribute('aria-pressed','false');","b.dataset.tip=tip||PS[id].tip;b.dataset.pid=id;b.dataset.kind='toggle';b.setAttribute('aria-pressed','false');"),
 ("const b=el('button','pw'+(cls?' '+cls:''));b.dataset.tip=tip;","const b=el('button','pw'+(cls?' '+cls:''));b.dataset.tip=tip;b.dataset.pid=id;b.dataset.kind='power';"),
 ("b.dataset.tip=tips[i];b.onclick=()=>setP(id,i,true);parent.appendChild(b);out.push(b)","b.dataset.tip=tips[i];b.dataset.pid=id;b.dataset.idx=i;b.dataset.kind='radio';b.onclick=()=>setP(id,i,true);parent.appendChild(b);out.push(b)"),
 ("b.dataset.tip=tips[i];b.onclick=()=>setP(id,i,true);d.appendChild(b)","b.dataset.tip=tips[i];b.dataset.pid=id;b.dataset.idx=i;b.dataset.kind='seg';b.onclick=()=>setP(id,i,true);d.appendChild(b)"),
 ("s.dataset.tip=tip;s.setAttribute('aria-label',tip.slice(0,30));s.onchange=()=>setP(id,+s.value+off,true);","s.dataset.tip=tip;s.dataset.pid=id;s.dataset.kind='select';s.dataset.off=off;s.dataset.opts=JSON.stringify(items);s.setAttribute('aria-label',tip.slice(0,30));s.onchange=()=>setP(id,+s.value+off,true);"),
]
for a,b in rep:
    assert a in src, a[:50]
    src=src.replace(a,b)
open('/tmp/cs_instr.html','w').write(src)
JS=r"""(pageIdx)=>{
 const st=document.querySelector('#stage').getBoundingClientRect();
 const R=e=>{const r=e.getBoundingClientRect();return [Math.round((r.left-st.left)*100)/100,Math.round((r.top-st.top)*100)/100,Math.round(r.width*100)/100,Math.round(r.height*100)/100]};
 const vis=e=>{const r=e.getBoundingClientRect();return r.width>0&&r.height>0&&!e.closest('[hidden]')};
 const bandOf=e=>{const c=e.closest('[data-band]');return c?+c.dataset.band:-1};
 const out={page:pageIdx,ctl:[],cv:[],txt:[],other:[]};
 document.querySelectorAll('[data-pid]').forEach(e=>{if(!vis(e))return;
  const o={kind:e.dataset.kind,pid:e.dataset.pid,idx:e.dataset.idx!==undefined?+e.dataset.idx:-1,r:R(e),tip:e.dataset.tip||'',cls:e.className,text:e.textContent.trim()};
  if(o.kind==='knob'){const kl=e.querySelector('.kl'),kv=e.querySelector('.kv'),kn=e.querySelector('.knob');o.label=kl?kl.textContent:'';o.labR=kl?R(kl):null;o.valR=kv?R(kv):null;o.knobR=kn?R(kn):null;const p=e.querySelector('.va');o.color=p?p.getAttribute('stroke'):'';}
  if(o.kind==='select'){o.opts=JSON.parse(e.dataset.opts);o.off=+e.dataset.off}
  out.ctl.push(o)});
 document.querySelectorAll('canvas').forEach(e=>{if(!vis(e))return;out.cv.push({id:e.id,cls:e.className,r:R(e),tip:(e.closest('[data-tip]')||e).dataset.tip||'',parent:e.parentElement.className})});
 const ids=new Set(out.ctl.map(c=>c.r.join()));
 document.querySelectorAll('button,select,input,[data-tip]').forEach(e=>{if(!vis(e)||e.dataset.pid!==undefined||e.closest('[data-pid]'))return;if(e.tagName==='CANVAS')return;
  out.other.push({tag:e.tagName,id:e.id,cls:e.className,r:R(e),text:e.textContent.trim().slice(0,60),tip:e.dataset.tip||''})});
 
 const col=c=>{const m=c.match(/[\d.]+/g);if(!m)return [0,0,0,0];return [+m[0],+m[1],+m[2],m.length>3?+m[3]:1]};
 out.box=[];
 document.querySelectorAll('#stage *').forEach(e=>{const r=e.getBoundingClientRect();if(r.width<2||r.height<2||e.closest('[hidden]'))return;const cs=getComputedStyle(e);
  if(['BUTTON','SELECT','CANVAS','svg','path','INPUT','circle','line','OPTION'].includes(e.tagName)||e.closest('svg')||e.closest('.knob')||e.closest('button')||e.closest('#tip,#toast,#modal'))return;
  const bg=cs.backgroundColor,bd=parseFloat(cs.borderTopWidth);const has=(bg!=='rgba(0, 0, 0, 0)')||(bd>0&&cs.borderTopStyle!=='none');
  if(has)out.box.push({c:e.className||e.id||e.tagName,r:R(e),bg:col(bg),bd:bd,bc:col(cs.borderTopColor),rad:parseFloat(cs.borderTopLeftRadius)||0})});
 out.txt=[];
 const w=document.createTreeWalker(document.querySelector('#stage'),NodeFilter.SHOW_TEXT);let n;
 while(n=w.nextNode()){const t=n.textContent.trim();if(!t)continue;const e=n.parentElement;
  if(e.closest('[hidden]')||e.closest('button')||e.closest('select')||e.closest('option')||e.closest('#tip,#toast,#modal')||e.tagName==='STYLE'||e.tagName==='SCRIPT')continue;
  if(e.closest('.kv')||e.closest('.kl'))continue;
  const rg=document.createRange();rg.selectNodeContents(n);const b=rg.getBoundingClientRect();if(b.width<1)continue;const cs=getComputedStyle(e);
  out.txt.push({t:t,c:e.className||e.tagName,r:[Math.round((b.left-st.left)*100)/100,Math.round((b.top-st.top)*100)/100,Math.round(b.width*100)/100,Math.round(b.height*100)/100],fs:parseFloat(cs.fontSize),fw:+cs.fontWeight,mono:cs.fontFamily.indexOf('Barlow')<0,it:cs.fontStyle==='italic',ls:parseFloat(cs.letterSpacing)||0,col:col(cs.color),tt:cs.textTransform})}
 return out}"""
async def main():
    async with async_playwright() as p:
        b=await p.chromium.launch();pg=await b.new_page(viewport={'width':1400,'height':900})
        await pg.goto('file:///tmp/cs_instr.html');await pg.wait_for_timeout(1500)
        res=[]
        for page in (0,):
            await pg.wait_for_timeout(500)
            res.append(await pg.evaluate(JS,page))
        json.dump(res,open('tools/layout.json','w'),indent=0)
        for r in res: print(r['page'],len(r['ctl']),len(r['cv']),len(r['other']))
        print([ (o['tag'],o['id'],o['text']) for o in res[0]['other']][:60])
        await b.close()
asyncio.run(main())
