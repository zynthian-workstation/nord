# Nord managed controller integration v1
"""View policy derived from the canonical C++ schema, never from label guessing."""
from pathlib import Path


def controller_pages(bundle,instrument_id,*,catalog=None):
    bundle=Path(bundle)
    fields=(Path(catalog) if catalog else bundle/'catalog.nord').read_bytes().decode().split('\0')
    entries=[dict(zip(('id','original','name','category','display_category','format','path'),fields[i:i+7])) for i in range(1,len(fields)-1,7)]
    entry=next((e for e in entries if e['id']==instrument_id),None)
    mask={'npno':1,'nsmp4':2,'pure':4}.get(entry['format'] if entry else '')
    if mask is None:return [('General',['gain'])]
    pages={}
    for line in (bundle/'tools/control-layout.tsv').read_text().splitlines():
        scope,group,title,index,symbol,*ranges=line.split('\t')
        if int(scope)&mask:
            if group not in pages:pages[group]=(title,[])
            pages[group][1].append(symbol)
    result=list(pages.values())
    if not result or any(not controls or len(controls)>4 for _,controls in result):raise ValueError('Invalid canonical page layout')
    return result
