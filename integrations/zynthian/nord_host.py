# Nord managed controller integration v1
"""Optional Oram view adapter. Does not remove or write ANY controller/value.

Called by the three audited Nord-only host hooks prepared by configure_zynthian_nord_ui.py.
Preset requests are asynchronous in stock Jalv: pages describe requested LV2 State,
not proof of successful sample loading. Unreadable/unknown State => Gain only.
"""
import ctypes as C
import ctypes.util
import logging
import os
from pathlib import Path
import urllib.parse
try:
    from .nord_pages import controller_pages
except ImportError:
    from nord_pages import controller_pages

URI='urn:nord:instrument'
STATE='http://lv2plug.in/ns/ext/state#state'
KEY=URI+'#instrument_id'
SEE='http://www.w3.org/2000/01/rdf-schema#seeAlso'


def local_path(uri):
    value=urllib.parse.urlsplit(str(uri))
    if value.scheme!='file' or value.netloc not in ('','localhost'):
        raise ValueError('Expected a local file URI')
    return Path(urllib.parse.unquote(value.path))


def preset_identity(uri, document):
    """Read the exact RDF State property using an isolated Lilv metadata world.

    Load only the containing standard LV2 preset bundle (manifest + seeAlso).
    No global world mutation, DSP loading, regex Turtle parsing or title inference.
    """
    lib=C.CDLL(ctypes.util.find_library('lilv-0') or 'liblilv-0.so.0');P=C.c_void_p
    def api(name,result,*args):
        fn=getattr(lib,name);fn.restype=result;fn.argtypes=list(args);return fn
    world=api('lilv_world_new',P)();nodes=[]
    if not world:raise RuntimeError('Cannot allocate Lilv metadata world')
    def node(value):
        n=api('lilv_new_uri',P,P,C.c_char_p)(world,str(value).encode());nodes.append(n);return n
    def string(n):
        return api('lilv_node_as_string',C.c_char_p,P)(n).decode('utf-8') if n else None
    owned=[]
    try:
        parent=Path(document).resolve().parent
        # Factory files live under presets/ inside the plugin bundle; user
        # presets normally have their manifest beside the state file.
        folder=next((p for p in (parent,*list(parent.parents)[:7]) if (p/'manifest.ttl').is_file()),None)
        if folder is None:return None
        directory=folder.as_uri()+'/'
        api('lilv_world_load_bundle',None,P,P)(world,node(directory))
        subject=node(uri)
        loaded=api('lilv_world_load_resource',C.c_int,P,P)(world,subject)
        if loaded<0:return None # Do not accept a partially parsed corrupt State.
        state=api('lilv_world_get',P,P,P,P,P)(world,subject,node(STATE),None);owned.append(state)
        if not state:return None
        identity=api('lilv_world_get',P,P,P,P,P)(world,state,node(KEY),None);owned.append(identity)
        return string(identity)
    finally:
        for n in owned+nodes:
            if n:api('lilv_node_free',None,P)(n)
        api('lilv_world_free',None,P)(world)


def host_context(engine, uri):
    from zyngine import zynthian_lv2
    world=zynthian_lv2.world
    plugin=world.get_all_plugins().get_by_uri(world.new_uri(URI))
    bundle=local_path(plugin.get_bundle_uri())
    if str(uri).startswith('file:'):
        document=local_path(uri)
    else:
        resource=world.new_uri(str(uri))
        location=world.get(resource,world.new_uri(SEE),None)
        if location is None:raise ValueError('Preset document not registered')
        document=local_path(location)
    data=Path(os.environ.get('ZYNTHIAN_MY_DATA_DIR','/zynthian/zynthian-my-data'))
    catalog=Path(os.environ.get('NORD_LIBRARY_CATALOG',str(data/'nord-lv2/catalog.nord')))
    return bundle,catalog if catalog.is_file() else bundle/'catalog.nord',document


def preset_changed(processor,preset):
    # Called only after stock set_preset sends its command. Keep preload/undo identity
    # separate from processor.preset_info, which intentionally remains the saved preset.
    if getattr(processor.engine,'plugin_url',None)==URI:
        processor._nord_requested_preset=str(preset[0]) if preset and preset[0] else None
        processor._nord_page_cache=None
        # Oram's UI consumes this queue on its own thread. Refresh only if the
        # control view is visible; never steal focus or invoke Tk from Jalv's thread.
        manager=getattr(processor.engine,'state_manager',None)
        if manager is not None:
            manager.send_cuia('REFRESH_SCREEN',['control'])


def screens(processor):
    """Build the real host view from EXISTING zctrl objects, on its UI read path."""
    if getattr(processor.engine,'plugin_url',None)!=URI:
        return processor.ctrl_screens_dict
    uri=getattr(processor,'_nord_requested_preset',None)
    if uri is None:
        info=getattr(processor,'preset_info',None)
        uri=str(info[0]) if info and info[0] else None
    try:
        if not uri:raise ValueError('No Nord preset selected')
        bundle,catalog,document=host_context(processor.engine,uri)
        layout=bundle/'tools/control-layout.tsv'
        signature=(uri,str(bundle),layout.stat().st_mtime_ns,layout.stat().st_size,str(catalog),catalog.stat().st_mtime_ns,
                   str(document),document.stat().st_mtime_ns,document.stat().st_size)
        cached=getattr(processor,'_nord_page_cache',None)
        if cached and cached[0]==signature:
            pages=cached[1]
        else:
            identity=preset_identity(uri,document)
            pages=controller_pages(bundle,identity,catalog=catalog)
            processor._nord_page_cache=(signature,pages)
    except Exception as error:
        logging.debug('Nord page State unavailable: %s',error)
        pages=[('General',['gain'])]
    # Never call refresh_controllers(): that would delete/recreate MIDI bindings.
    result={}
    for title,symbols in pages:
        present=[symbol for symbol in symbols if symbol in processor.controllers_dict]
        if present:result[title]=processor.build_ctrl_screen(present)
    processor.ctrl_screens_dict=result
    signature=tuple((name,tuple(ctrl.symbol for ctrl in controls)) for name,controls in result.items())
    # Oram stores an index in the WHOLE GUI list (chain headers/controllers and
    # other processors included), not an index in this processor's page dictionary.
    # Do not clamp it to len(result) on each read or the last pages keep jumping.
    if signature!=getattr(processor,'_nord_visible_pages',None) or processor.current_screen_index<0:
        processor.current_screen_index=0 if result else -1
    processor._nord_visible_pages=signature
    return result
