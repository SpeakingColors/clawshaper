#!/usr/bin/env python3
"""Build docs/manual.html from tools/layout.json (control names and info-bubble texts of the mockup),
tools/params.json (ranges, defaults) and the two window screenshots. CI turns it into a PDF.
Run from the repo root after refreshing docs/screenshot.png."""
import json, html, re
L = json.load(open('tools/layout.json'))
P = {p['id']: p for p in json.load(open('tools/params.json'))}
e = html.escape
def rng(pid):
    p = P.get(pid)
    if not p: return ''
    f = lambda v: ('%g' % v)
    return '%s to %s' % (f(p['min']), f(p['max']))
def dfl(pid):
    p = P.get(pid)
    return '%g' % p['def'] if p else ''
def rows(page):
    tips = {}
    for c in page['ctl']:
        tips.setdefault(c['pid'], []).append((c.get('text') or c.get('label') or '', c['tip']))
    oth = {o['id']: o['tip'] for o in page['other'] if o.get('id')}
    cvs = [c for c in page['cv'] if c.get('parent') != 'cheek']
    def tip(pid, join=False):
        t = tips.get(pid, [])
        if not t: return ''
        return ' '.join('%s: %s' % (n, x) for n, x in t) if join else t[0][1]
    def band(pid): return 'b0.' + pid
    cv = lambda frag: next((c['tip'] for c in cvs if frag in c['tip']), '')
    R = [('Preset arrows, name, Save', oth.get('pName', '') + ' ' + oth.get('pSave', ''), '', ''),
         ('Bypass', oth.get('bBypass', ''), '', ''), ('Guide, Settings, About', 'Short guide, info bubbles, power saving and colour look, and about.', '', ''),
         ('Input dB', tip('m.inG'), rng('m.inG'), dfl('m.inG')), ('Bands', tip('m.bands'), rng('m.bands'), dfl('m.bands')),
         ('Filter (slope)', tip('m.slope'), '12 to 96 dB/oct', '24 dB'), ('Listen', tip('m.listen', True), '', 'All'),
         ('Mix', tip('m.mix'), rng('m.mix'), dfl('m.mix')), ('Boost', tip('m.boost', True), '', 'x1'),
         ('FFT', tip('m.fft'), '', 'In + Out'), ('Pre and Post meters', cv('Level before') + ' ' + cv('Level after'), '', ''),
         ('Frequency display', cv('Frequency view'), '', ''), ('Out dB', tip('m.outG'), rng('m.outG'), dfl('m.outG')),
         ('Ceil', tip('m.ceil'), rng('m.ceil'), dfl('m.ceil')), ('Lim', tip('m.limOn'), 'on or off', 'on'),
         ('Release', tip('m.lRel'), rng('m.lRel') + ' ms', 'Auto'), ('Auto', tip('m.lAuto'), '', 'on'), ('Look (lookahead)', tip('m.la'), rng('m.la') + ' ms', dfl('m.la')),
         ('Limiter GR meter', cv('Limiter gain reduction'), '', ''),
         ('Band Mix', tip(band('mix')), rng(band('mix')), dfl(band('mix'))), ('Band power', tip(band('on')), 'on or off', 'on'),
         ('Mute, Solo', 'Mute removes a band from the output; Solo plays only the soloed bands. The strips under the display follow the crossovers.', '', 'off'),
         ('Scope and Gain', cv('Transient picture'), '', 'Scope'), ('Zoom', tip(band('zoom')), '1x to 8x', '4x'),
         ('Speed', tip(band('spd'), True), '', 'Medium'), ('Attack', tip(band('atk')), rng(band('atk')) + ' %', dfl(band('atk'))),
         ('Sustain', tip(band('sus')), rng(band('sus')) + ' %', dfl(band('sus'))), ('Max dB', tip(band('max')), rng(band('max')) + ' dB', dfl(band('max'))),
         ('Band Output', tip(band('outG')), rng(band('outG')) + ' dB', dfl(band('outG'))),
         ('Level meter and Threshold', cv('Band level in dB'), '-60 to 0 dB', '-48 dB'), ('Gain meter', cv('Gain change of this band'), '-12 to +12 dB', '')]
    return [(n, t.replace('Band 1: ', '').replace('Band 1 and', 'Band and'), r, d) for n, t, r, d in R]
def table(page):
    return '<table><tr><th>Control</th><th>What it does</th><th>Range</th><th>Default</th></tr>' + ''.join(
        '<tr><th>%s</th><td>%s</td><td>%s</td><td>%s</td></tr>' % (e(n), e(t), e(r), e(d)) for n, t, r, d in rows(page)) + '</table>'
page = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>ClawShaper manual</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root{--bg:#0b0b0d;--card:#16171a;--line:#33363d;--ink:#e8e9ee;--dim:#a9adb8;--teal:#02EBBA;--pink:#FF7BB1}
@media print{:root{--bg:#fff;--card:#f4f4f6;--line:#c9cbd3;--ink:#16171a;--dim:#4a4d55;--teal:#007a62;--pink:#b0114f}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 "Barlow Condensed","Arial Narrow",Arial,sans-serif;padding:24px max(16px,env(safe-area-inset-left,0px))}
main{max-width:980px;margin:0 auto}
h1{font-size:40px;margin:0 0 4px;font-style:italic}
h2{font-size:26px;margin:36px 0 8px;color:var(--teal);letter-spacing:1px;text-transform:uppercase;break-after:avoid}
a{color:var(--pink)}
img{max-width:100%;height:auto;border:1px solid var(--line);border-radius:6px;display:block;margin:8px 0}
table{border-collapse:collapse;width:100%;font-size:15px;display:block;overflow-x:auto}
td,th{border-bottom:1px solid var(--line);padding:5px 8px;text-align:left;vertical-align:top}
th{white-space:nowrap}
code,kbd{font-family:"JetBrains Mono",Consolas,monospace;font-size:13px;background:var(--card);border:1px solid var(--line);border-radius:3px;padding:0 4px}
.dim{color:var(--dim)}
@media print{body{padding:0}tr{break-inside:avoid}img{break-inside:avoid}}
</style></head><body><main>
<h1>ClawShaper</h1>
<p class="dim">Manual for version @VER@. Studio 87. Free and open source (AGPL-3.0-or-later). Collects nothing, never uses the network.</p>
<p>A multiband transient shaper with a safety limiter. It turns the punch (attack) and the tail (sustain) of your sound up or down, in up to four bands, and it reacts to changes in level, not to the absolute level.</p>

<h2>Quick start</h2>
<ol>
<li>Put ClawShaper on a drum or bass track. Pick a preset (for example <b>Kick Snap</b>) or start from <b>Start Here</b>.</li>
<li>Choose how many bands (1 to 4) and drag the crossover lines in the big display to split the sound where you want.</li>
<li>In a band: <b>Attack</b> up gives more punch, down softens it. <b>Sustain</b> up gives longer tails, down tightens. Use <b>Listen</b> to hear only the attack or only the sustain part.</li>
<li>Drag the white <b>Threshold</b> slider on a band's level meter: below it nothing is shaped, so quiet noise is not boosted.</li>
<li>Compare with <b>Bypass</b> (B). The dry signal is delayed to match. The limiter is always last and holds the Ceiling.</li>
</ol>

<h2>The window</h2>
<img src="screenshot.png" alt="ClawShaper window">
@T1@

<h2>How it works</h2>
<ul>
<li>Each band has a fast and a slow level follower. Their difference in dB says whether a hit is starting (attack) or fading (sustain). Only that difference matters, so the same settings work at any level.</li>
<li>Attack and Sustain set how many dB the band is turned up or down. <b>Max dB</b> is a soft limit on that gain. <b>Boost</b> (x1, x2, x4) multiplies the strength of both.</li>
<li><b>Speed</b> sets how quickly the followers react (Fast, Medium, Slow). Low bands are automatically slower so a steady bass note is not mistaken for hits.</li>
<li>The audio is delayed by 1 ms against the detector so the gain is already up when a hit arrives. Mix 0% is the delayed input, unchanged.</li>
<li>The limiter (lookahead 1 to 7.5 ms, Auto or manual release) is the last stage at every setting, so the output never goes above the Ceiling.</li>
</ul>
<p class="dim">The detector, the gain law and all time constants are our own design; they are not copied from any commercial product.</p>

<h2>Latency</h2>
<p>The plugin reports its latency to the host: 1 ms for the detector plus the limiter lookahead (4.0 ms at the default 3 ms lookahead). It is shown under the Pre meter. Mix and Bypass stay in time.</p>

<h2>Keyboard and mouse</h2>
<table><tr><td><kbd>B</kbd></td><td>Bypass on or off</td></tr><tr><td><kbd>[</kbd> <kbd>]</kbd></td><td>Previous and next preset</td></tr><tr><td>Knobs</td><td>Drag up or down, mouse wheel. Hold Shift for fine steps. Double-click to reset.</td></tr><tr><td>Threshold slider</td><td>Drag, mouse wheel, double-click to reset to -48 dB.</td></tr><tr><td>Crossover lines</td><td>Drag in the big display; double-click to reset.</td></tr><tr><td>Window</td><td>Drag a corner; it keeps its proportions. Double-click the wood to return to 100%.</td></tr><tr><td>Presets</td><td>Save stores your own preset in your user data folder (Studio 87/ClawShaper/presets). They are only ever on your computer.</td></tr></table>

<h2>Install and uninstall</h2>
<ul>
<li><b>Linux:</b> unpack the zip, run <code>install.sh</code> (copies the VST3 to <code>~/.vst3</code>). <code>uninstall.sh</code> removes it.</li>
<li><b>Windows:</b> run the installer. It adds an uninstaller. Close your DAW first.</li>
<li><b>macOS:</b> open the package. The build is not notarised, so macOS may ask you to allow it in System Settings, Privacy and Security.</li>
</ul>
<p>After installing, rescan plugins in your host.</p>

<h2>Licence and thanks</h2>
<p>AGPL-3.0-or-later. Built with JUCE (AGPLv3). Fonts: Barlow Condensed and JetBrains Mono, SIL Open Font Licence 1.1. Source: <a href="https://github.com/SpeakingColors/clawshaper">github.com/SpeakingColors/clawshaper</a>.</p>
</main></body></html>
'''
open('docs/manual.html', 'w').write(page.replace('@VER@', '0.1.0').replace('@T1@', table(L[0])))
print('manual ok', len(rows(L[0])))
