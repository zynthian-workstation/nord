"""Canonical control-layout transport and generated-TTL UI annotations.

Official priorities are pprops:displayPriority; Oram additionally reads the legacy
lv2:displayPriority spelling. ControlGroup belongs to parameters, NOT port-groups.
"""
from pathlib import Path
import re


def read_layout(path):
    rows=[]
    for line in Path(path).read_text().splitlines():
        scope,group,title,index,symbol,lo,hi,default=line.split('\t')
        row=dict(scope=int(scope),group=int(group),title=title,index=int(index),symbol=symbol,
                 minimum=float(lo),maximum=float(hi),default=float(default))
        if not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*',symbol) or not title:raise ValueError('Invalid control layout')
        rows.append(row)
    if len(rows)!=125 or [r['index'] for r in rows]!=list(range(125)):raise ValueError('Noncanonical control layout')
    if len({r['symbol'] for r in rows})!=len(rows):raise ValueError('Duplicate public endpoint')
    groups={r['group'] for r in rows}
    if groups!=set(range(36)):raise ValueError('Control groups must be dense')
    for group in groups:
        members=[r for r in rows if r['group']==group]
        if len(members)>4 or len({r['title'] for r in members})!=1:raise ValueError('Inconsistent page')
        if any(not 1<=r['scope']<=7 for r in members):raise ValueError('Invalid control scope')
    return rows


def annotate(bundle):
    path=Path(bundle)/'Nord_dsp.ttl';layout=Path(bundle)/'tools/control-layout.tsv'
    if not layout.is_file():raise ValueError('Missing canonical control-layout.tsv')
    rows=read_layout(layout);text=path.read_text()
    # Idempotent: only generated Nord metadata is processed, never arbitrary TTL.
    text=re.sub(r'\n\s+(?:lv2|pprops):displayPriority \d+ ;','',text)
    if '@prefix pprops:' not in text:text='@prefix pprops: <http://lv2plug.in/ns/ext/port-props#> .\n'+text
    if '@prefix param:' not in text:text='@prefix param: <http://lv2plug.in/ns/ext/parameters#> .\n'+text
    lookup={r['index']+4:r for r in rows}
    def port(match):
        row=lookup.get(int(match[1]))
        if row is None:return match[0]
        priority=10000-row['index']
        return match[0]+f'\n        pprops:displayPriority {priority} ;\n        lv2:displayPriority {priority} ;'
    text=re.sub(r'lv2:index (\d+) ;',port,text)
    for group in range(36):
        resource=f'<urn:nord:instrument#portGroup_nord_group_{group:02d}>'
        pattern=re.escape(resource)+r'\n    a pg:InputGroup(?:, param:ControlGroup)? ;'
        replacement=resource+f'\n    a pg:InputGroup, param:ControlGroup ;\n    lv2:displayPriority {10000-group} ;'
        text,count=re.subn(pattern,lambda _:replacement,text)
        if count!=1:raise ValueError('Missing/nonunique Nord group '+str(group))
    path.write_text(text)
