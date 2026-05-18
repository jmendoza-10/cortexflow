"""Generated Flow diagrams pipeline.

Public API:
    brace_scope.functions(source)       -> {qualified_name: body_text}
    composition.parse(app_hpp_path)     -> AppComposition
    flow_extractor.extract(module, ...) -> FlowIR
    mermaid.render_flow(flow_ir)        -> str
"""
