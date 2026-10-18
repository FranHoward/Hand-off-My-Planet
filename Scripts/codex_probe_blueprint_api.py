import unreal

bp = unreal.load_asset('/Game/Blueprint/BP_FirstPersonCharacterAI')
print('CODEX_BP', bp)
print('CODEX_BLUEPRINT_METHODS', [x for x in dir(bp) if 'graph' in x.lower() or 'node' in x.lower()])
graph = unreal.BlueprintEditorLibrary.find_event_graph(bp)
print('CODEX_GRAPH', graph)
print('CODEX_GRAPH_METHODS', [x for x in dir(graph) if 'node' in x.lower() or 'pin' in x.lower() or 'graph' in x.lower()])
for prop in ('nodes', 'ubergraph_pages', 'function_graphs'):
    try:
        obj = graph if prop == 'nodes' else bp
        value = obj.get_editor_property(prop)
        print('CODEX_PROP', prop, type(value), len(value) if hasattr(value, '__len__') else value)
    except Exception as exc:
        print('CODEX_PROP_ERROR', prop, repr(exc))
print('CODEX_NODE_CLASSES', unreal.load_class(None, '/Script/BlueprintGraph.K2Node_CallFunction'), unreal.load_class(None, '/Script/BlueprintGraph.K2Node_ComponentBoundEvent'))
