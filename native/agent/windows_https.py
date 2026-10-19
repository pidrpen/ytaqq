"""Official Z.ai requests using Windows trust and system proxy settings.

No custom CA overrides, insecure TLS flags, redirects or arbitrary destinations.
The native parent owns the process deadline/cancellation.
"""
import ctypes
from ctypes import wintypes
from email.message import Message
import io
import urllib.request
import urllib.response

ENDPOINT = 'https://api.z.ai/api/paas/v4/chat/completions'
LIMIT = 262144


def _load_api():
    api = ctypes.WinDLL('winhttp', use_last_error=True)
    handle = ctypes.c_void_p
    signatures = {
        'WinHttpOpen': (handle, [wintypes.LPCWSTR, wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD]),
        'WinHttpConnect': (handle, [handle, wintypes.LPCWSTR, wintypes.WORD, wintypes.DWORD]),
        'WinHttpOpenRequest': (handle, [handle, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD]),
        'WinHttpSetTimeouts': (wintypes.BOOL, [handle, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]),
        'WinHttpSetOption': (wintypes.BOOL, [handle, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]),
        'WinHttpSendRequest': (wintypes.BOOL, [handle, wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_size_t]),
        'WinHttpReceiveResponse': (wintypes.BOOL, [handle, ctypes.c_void_p]),
        'WinHttpQueryHeaders': (wintypes.BOOL, [handle, wintypes.DWORD, wintypes.LPCWSTR, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]),
        'WinHttpReadData': (wintypes.BOOL, [handle, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]),
        'WinHttpCloseHandle': (wintypes.BOOL, [handle]),
    }
    for name, (result, args) in signatures.items():
        function = getattr(api, name)
        function.restype, function.argtypes = result, args
    return api


def windows_post(request):
    if request.full_url != ENDPOINT or request.get_method() != 'POST':
        raise ValueError('Windows HTTPS: only the official Z.ai POST endpoint is allowed')
    api = _load_api()
    handles = []

    def checked(value, stage):
        if not value:
            code = ctypes.get_last_error()
            raise OSError(code, f'Windows HTTPS: {stage}; {ctypes.FormatError(code).strip()}')
        return value

    def owned(value, stage):
        handle = checked(value, stage)
        handles.append(handle)
        return handle

    try:
        # AUTOMATIC_PROXY uses Windows proxy/PAC discovery (Windows 8.1+).
        session = api.WinHttpOpen('CursorPad Z.ai Windows HTTPS', 4, None, None, 0)
        if not session and ctypes.get_last_error() == 87:  # Older Windows.
            session = api.WinHttpOpen('CursorPad Z.ai Windows HTTPS', 0, None, None, 0)
        session = owned(session, 'сессия/прокси')
        timeout = min(120000, max(1000, int(getattr(request, 'timeout', 120) * 1000)))
        checked(api.WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout), 'таймауты')
        connection = owned(api.WinHttpConnect(session, 'api.z.ai', 443, 0), 'соединение')
        handle = owned(api.WinHttpOpenRequest(connection, 'POST', '/api/paas/v4/chat/completions',
                       None, None, None, 0x00800000), 'создание HTTPS-запроса')
        redirects = wintypes.DWORD(0)  # WINHTTP_OPTION_REDIRECT_POLICY_NEVER
        checked(api.WinHttpSetOption(handle, 88, ctypes.byref(redirects), ctypes.sizeof(redirects)), 'запрет перенаправлений')
        headers = ''.join(f'{name}: {value}\r\n' for name, value in request.header_items())
        payload = request.data or b''
        data = ctypes.create_string_buffer(payload)
        checked(api.WinHttpSendRequest(handle, headers, len(headers), data, len(payload), len(payload), 0), 'DNS/соединение/TLS/отправка')
        checked(api.WinHttpReceiveResponse(handle, None), 'ожидание HTTP')
        status, size = wintypes.DWORD(), wintypes.DWORD(ctypes.sizeof(wintypes.DWORD))
        checked(api.WinHttpQueryHeaders(handle, 19 | 0x20000000, None,
                ctypes.byref(status), ctypes.byref(size), None), 'HTTP-статус')
        chunks, count = [], 0
        while True:
            buffer, got = ctypes.create_string_buffer(16384), wintypes.DWORD()
            checked(api.WinHttpReadData(handle, buffer, min(16384, LIMIT + 1 - count), ctypes.byref(got)), 'чтение ответа')
            if not got.value:
                break
            count += got.value
            if count > LIMIT:
                raise ValueError('Windows HTTPS: API response exceeds limit')
            chunks.append(buffer.raw[:got.value])
        response = urllib.response.addinfourl(io.BytesIO(b''.join(chunks)), Message(), ENDPOINT, status.value)
        response.msg = 'Z.ai Windows HTTPS'
        return response
    finally:
        for handle in reversed(handles):
            api.WinHttpCloseHandle(handle)


class WindowsHTTPSHandler(urllib.request.HTTPSHandler):
    def https_open(self, request):
        return windows_post(request)
