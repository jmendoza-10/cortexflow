"""Unit tests for the brace-scope tracker.

Covers the cases mandated by the slice acceptance criteria:
    - nested braces in a function body
    - string literals containing braces
    - block- and line-comments containing braces
    - functions inside a namespace (including nested namespaces)
    - malformed input handled gracefully
"""

from diagrams.brace_scope import functions, neutralize


def test_nested_braces_in_body():
    src = """
    int outer() {
        if (true) {
            { int x = 1; }
        }
    }
    """
    fns = functions(src)
    assert 'outer' in fns
    body = fns['outer']
    assert 'int x = 1' in body
    # The outer braces are not part of the body; the inner ones are.
    assert body.count('{') == body.count('}')


def test_string_literal_with_braces():
    src = """
    void f() {
        const char* a = "{not a block}";
        const char* b = "}}}{{{ still not";
    }
    """
    fns = functions(src)
    assert 'f' in fns
    # The body was neutralized — string contents are blanked, so the literal
    # braces are gone, but the surrounding "..." delimiters remain.
    body = fns['f']
    assert '"' in body  # delimiters preserved
    assert '{not a block}' not in body  # contents neutralized
    # And the function ended at its real closing brace, not at one inside the string.
    assert 'const char* a' in body
    assert 'const char* b' in body


def test_comments_with_braces():
    src = """
    void g() {
        // closing brace inside line comment: }
        /* and one in a block { } here */
        int x = 0;
    }
    """
    fns = functions(src)
    assert 'g' in fns
    body = fns['g']
    assert 'int x = 0' in body


def test_function_inside_namespace():
    src = """
    namespace alpha {
    void f() {
        return;
    }
    }  // namespace alpha
    """
    fns = functions(src)
    assert 'alpha::f' in fns
    assert 'f' not in fns


def test_function_inside_nested_namespace():
    src = """
    namespace alpha {
    namespace beta {
        struct Foo;
        cortexflow::StateDirective Foo::handle(int& env) {
            if (env > 0) {
                return cortexflow::transition_to<Bar>();
            }
            return cortexflow::stay();
        }
    }
    }
    """
    fns = functions(src)
    assert 'alpha::beta::Foo::handle' in fns
    body = fns['alpha::beta::Foo::handle']
    assert 'transition_to' in body


def test_member_function_definition_keyed_by_class():
    src = """
    namespace ns {
    void Idle::handle(Envelope& env) {
        body_here();
    }
    }
    """
    fns = functions(src)
    assert 'ns::Idle::handle' in fns


def test_multiple_functions_in_one_namespace():
    src = """
    namespace ns {
    void a() { x; }
    int b() { y; return 0; }
    }
    """
    fns = functions(src)
    assert 'ns::a' in fns
    assert 'ns::b' in fns
    assert 'x' in fns['ns::a']
    assert 'y' in fns['ns::b']


def test_constructor_with_initializer_list():
    src = """
    namespace ns {
    Foo::Foo() : x_(0), y_(some_call(42)) {
        seed();
    }
    }
    """
    fns = functions(src)
    assert 'ns::Foo::Foo' in fns
    assert 'seed' in fns['ns::Foo::Foo']


def test_forward_declaration_does_not_create_entry():
    src = """
    namespace ns {
    class Bar;
    struct Baz;
    void f() {}
    }
    """
    fns = functions(src)
    assert 'ns::Bar' not in fns
    assert 'ns::Baz' not in fns
    assert 'ns::f' in fns


def test_class_with_inline_methods():
    src = """
    namespace ns {
    class Widget {
    public:
        void touch() { hit_; }
    };
    }
    """
    fns = functions(src)
    assert 'ns::Widget::touch' in fns


def test_malformed_input_does_not_crash():
    # Unterminated function body. The tracker should return without raising.
    src = "void f() { if (x) {"
    fns = functions(src)
    # No body was completed, so no entry is emitted. The important property
    # is that the call returned at all.
    assert isinstance(fns, dict)


def test_malformed_unterminated_string_does_not_crash():
    src = 'void f() { const char* s = "unterminated; }'
    fns = functions(src)
    assert isinstance(fns, dict)


def test_control_flow_keywords_are_not_treated_as_functions():
    src = """
    namespace ns {
    void f() {
        if (true) { x; }
        while (z) { y; }
        for (int i = 0; i < 10; ++i) { i; }
    }
    }
    """
    fns = functions(src)
    # Only `f` is a function; `if`, `while`, `for` should not appear.
    assert set(fns.keys()) == {'ns::f'}


def test_neutralize_preserves_length():
    src = "// hello {\n\"world{}\"\n/* a { b } c */\n"
    out = neutralize(src)
    assert len(out) == len(src)
    # Braces inside comments and strings are erased.
    assert '{' not in out
    assert '}' not in out
