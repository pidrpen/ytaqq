"""Small documented patch for embedding QwenPaw without its browser host.

The native CursorPad tools are injected into QwenPawAgent. Importing its
optional coding mixin otherwise eagerly imports Playwright and Node even
though no browser tool is exposed. Default upstream behavior is unchanged.
"""
def patch_paw_source(name, data):
    if name != 'qwenpaw/agents/tools/__init__.py':
        return data
    text = data.decode('utf-8')
    before = '_BROWSER_EXPERIMENTAL = load_config().browser.experimental'
    after = ('_CURSORPAD_NATIVE_ONLY = __import__("os").environ.get("QWENPAW_CURSORPAD_NATIVE_ONLY") == "1"\n'
             '_BROWSER_EXPERIMENTAL = not _CURSORPAD_NATIVE_ONLY and load_config().browser.experimental')
    fallback = 'else:\n    from .deprecated_browser.browser_control import browser'
    assert text.count(before) == text.count(fallback) == 1, 'Upstream browser bootstrap changed; review patch'
    return text.replace(before, after).replace(fallback, 'elif not _CURSORPAD_NATIVE_ONLY:\n    from .deprecated_browser.browser_control import browser').encode('utf-8')
