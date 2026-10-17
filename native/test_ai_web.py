"""Offline parser/relevance checks for the native AC_WEB_TEST executable."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import json


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--probe', type=Path, required=True)
    args = ap.parse_args()
    probe = str(args.probe.resolve())
    with tempfile.TemporaryDirectory(prefix='cursorpad-web-') as tmp:
        root = Path(tmp)
        def run(mode, content, query='Python'):
            f = root/'fixture.xml'
            f.write_text(content, encoding='utf-8')
            p = subprocess.run([probe, mode, str(f), query], capture_output=True, encoding='utf-8', check=False)
            return p.returncode, p.stdout
        def urls(query):
            return subprocess.run([probe, '--urls', query], capture_output=True, encoding='utf-8', check=True).stdout
        assert urls('привет') == 'count 0\n'
        assert urls('что по ссылке «https://www.ozon.ru/product/item-123/?x=1&y=2»') == 'count 1\nhttps://www.ozon.ru/product/item-123/?x=1&y=2\n'
        assert urls('[товар](https://example.org/a).') == 'count 1\nhttps://example.org/a\n'
        assert urls('https://example.org/a https://example.org/a').startswith('count 1\n')
        assert urls('https://example.org/a https://example.org/b').startswith('count 2\n')
        assert urls('https://example.org/a https://example.org/b https://example.org/b').startswith('count 2\n')
        assert urls('https://example.org/a https://example.org/b https://example.org/c') == 'count -1\n'
        assert urls('https://example.org/' + 'a'*3300) == 'count -1\n'
        assert urls('HTTP://example.org/').startswith('count 1\n')
        # Explicit private/unsupported URLs must fail in direct mode, never search.
        for url in ('https://localhost/', 'http://example.org/'):
            p = subprocess.run([probe, url], capture_output=True, encoding='utf-8')
            assert p.returncode == 1 and 'Прямая ссылка:' in p.stdout and 'Общий поиск не выполнялся' in p.stdout
            assert 'Tavily' not in p.stdout
        assert subprocess.run([probe, '--local-tests'], capture_output=True).returncode == 0
        for query in ('ы', ' а ', 'Привет!!!', 'спасибо', 'ОК'):
            p = subprocess.run([probe, query], capture_output=True, encoding='utf-8')
            assert p.returncode == 1 and 'Локально:' in p.stdout and 'HTTP 400' not in p.stdout
        rss = ('<rss><channel><link>https://not-a-result.example/</link>'
               '<item><title>Python документация</title><link>https://docs.python.org/?a=1&amp;b=2</link>'
               '<description>hashlib и SHA-256</description></item></channel></rss>')
        code, text = run('--rss', rss)
        assert code == 0 and 'Python документация' in text and '?a=1&b=2' in text
        assert 'not-a-result' not in text
        assert run('--rss', rss, 'столица Франции')[0] == 1
        assert run('--rss', rss.replace('Python', 'Сталь'), 'твёрдость стали')[0] == 0
        assert run('--rss', rss.replace('https://docs.python.org/', 'file:///C:/'))[0] == 1
        assert run('--rss', rss.replace('https://docs.python.org/', 'javascript:alert(1)'))[0] == 1
        assert run('--rss', rss.replace('</item>', ''))[0] == 1
        repeated = '<rss>'+rss*8+'</rss>'
        assert run('--rss', repeated)[1].count('https://docs.python.org/') == 3
        wiki = ('<api><query><search><p title="Париж" pageid="6214" '
                'snippet="Столица &lt;span class=&quot;searchmatch&quot;&gt;Франции&lt;/span&gt;." />'
                '</search></query></api>')
        code, text = run('--wiki', wiki)
        assert code == 0 and 'Париж' in text and 'Франции' in text
        assert '<span' not in text and 'https://ru.wikipedia.org/?curid=6214' in text
        assert run('--wiki', wiki.replace('6214', '6214&amp;evil=1'))[0] == 1
        assert run('--wiki', '<api><error code="badquery" /></api>')[0] == 1
        assert run('--wiki', wiki.replace('/>', ''))[0] == 1
        html = '<html><head><script>wrong-head</script></head><nav>wrong-menu</nav><main><h1>Документ</h1><p>Факты</p><script>wrong-script</script><style>wrong-style</style></main><footer>wrong-footer</footer></html>'
        code, text = run('--plain', html)
        assert code == 0 and 'Документ' in text and 'Факты' in text and 'wrong-' not in text
        for url in ('https://127.0.0.1/', 'https://localhost/', 'https://192.168.1.1/', 'http://example.org/', 'https://user:password@example.org/'):
            assert subprocess.run([probe, '--page', url], capture_output=True).returncode == 1
        response = {'answer': 'ignored', 'results': [
            {'title': 'Погода 🌦', 'url': 'https://example.org/weather', 'content': 'Короткая выдержка',
             'raw_content': 'Прогноз на завтра. '+('Температура +5. ' * 30)},
            {'title': 'Новости', 'url': 'https://example.org/news', 'content': 'Короткая выдержка',
             'raw_content': 'Свежие события. '+('Подробности новости. ' * 30)}]}
        code, text = run('--tavily', json.dumps(response), 'погода завтра')
        assert code == 0 and 'Погода 🌦' in text and 'Новости' in text
        assert text.count('Прочитан текст страницы') == 2
        assert 'https://example.org/weather' in text and 'ignored' not in text
        response['results'][0]['raw_content'] = None
        code, text = run('--tavily', json.dumps(response), 'погода завтра')
        assert code == 0 and text.count('Прочитан текст страницы') == 1 and 'Только поисковая выдержка' in text
        assert run('--tavily', '{"results":[]}')[0] == 1
        assert run('--tavily', '{"error":"rate limit"}')[0] == 1
        assert run('--tavily', '{"results":[{"url":"https://example.org/"}')[0] == 1
        assert run('--tavily', '['*30+'0'+']'*30)[0] == 1
    print('PASS: Tavily JSON/Unicode/raw-vs-snippet/errors/depth + HTML/URL regressions')


if __name__ == '__main__':
    main()
