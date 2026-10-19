"""Bounded connection diagnostics. Never include request bodies or headers."""
import re
import socket
import ssl
import sys


def safe_text(value, secret='', limit=180):
    text = str(value)
    if secret:
        text = text.replace(secret, '[ключ скрыт]')
    text = re.sub(r'(?i)bearer\s+\S+|(?:sk-|tvly-)[A-Za-z0-9_-]+', '[ключ скрыт]', text)
    return ' '.join(text.split())[:limit]


def connection_error(error, endpoint, model, key, elapsed):
    reason = getattr(error, 'reason', error)
    target = 'Z.ai api.z.ai' if endpoint.startswith('https://api.z.ai/') else 'локальная модель 127.0.0.1'
    prefix = f'{target}; модель={safe_text(model, key, 60)}; {elapsed:.1f} с'
    if isinstance(reason, ssl.SSLCertVerificationError):
        code = getattr(reason, 'verify_code', '?')
        detail = safe_text(getattr(reason, 'verify_message', 'certificate verify failed'), key)
        return (f'{prefix}; этап=TLS; verify_code={code}; {detail}; '
                f'Python {sys.version.split()[0]}, {ssl.OPENSSL_VERSION}; '
                'проверьте цепочку сертификатов сервера, HTTPS-фильтр антивируса или сети')
    if isinstance(reason, ssl.SSLError):
        return f'{prefix}; этап=TLS; {safe_text(reason, key)}'
    if isinstance(reason, (TimeoutError, socket.timeout)):
        return f'{prefix}; этап=соединение/чтение; превышено время ожидания'
    if isinstance(reason, socket.gaierror):
        return f'{prefix}; этап=DNS; код={reason.errno}; имя сервера не разрешено'
    code = getattr(reason, 'winerror', None) or getattr(reason, 'errno', None)
    if code in (12037, 12038, 12044, 12045, 12057, 12157, 12175):
        return f'{prefix}; этап=TLS Windows; код={code}; {safe_text(reason, key)}; проверьте доверие Windows к сертификату и HTTPS-фильтр сети'
    return f'{prefix}; этап=соединение/чтение; {type(reason).__name__}; код={code}; {safe_text(reason, key)}'
