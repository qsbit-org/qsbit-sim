"""Check rendered links, clickable architecture and trace playback in Chromium."""
import argparse
from functools import lru_cache, partial
from html.parser import HTMLParser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

from playwright.sync_api import sync_playwright


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.ids, self.links = set(), []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        for key in ('href', 'src', 'data'):
            if key in attrs:
                self.links.append(attrs[key])


def check_links(site):
    pages = {p.resolve(): Page(p.read_text()) for p in site.rglob('*.html')}
    @lru_cache(maxsize=None)
    def destination(parent, relative):
        target = (parent / relative).resolve()
        return target / 'index.html' if target.is_dir() else target

    @lru_cache(maxsize=None)
    def exists(target):
        return target.exists()

    errors, diagrams = [], set()
    for path, page in pages.items():
        for href in page.links:
            url = urlsplit(href)
            if url.scheme or url.netloc:
                continue
            target = destination(path.parent, unquote(url.path)) if url.path else path
            if target not in pages and not exists(target):
                errors.append(f'{path.name}: missing {href}')
            elif target.suffix == '.svg':
                diagrams.add(target)
            elif url.fragment and target in pages and unquote(url.fragment) not in pages[target].ids:
                errors.append(f'{path.name}: missing anchor {href}')
    module_pages = {p.name for p in (site / 'modules').glob('*.html') if p.name != 'README.html'}
    linked = set()
    for path in diagrams:
        for element in ET.parse(path).iter():
            href = element.get('{http://www.w3.org/1999/xlink}href') or element.get('href')
            if href:
                target = (path.parent / unquote(href)).resolve()
                if not target.exists():
                    errors.append(f'{path.name}: missing SVG target {href}')
                linked.add(target.name)
    if not module_pages <= linked:
        errors.append(f'unlinked architecture modules: {module_pages - linked}')
    assert not errors, '\n'.join(errors)
    return module_pages


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def check_browser(site, module_pages):
    server = ThreadingHTTPServer(('127.0.0.1', 0), partial(QuietHandler, directory=str(site)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f'http://127.0.0.1:{server.server_port}'
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page(viewport={'width': 1440, 'height': 1050})
            errors = []
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.goto(base + '/index.html')
            page.get_by_role('main').get_by_role('link', name='Run your first simulation', exact=True).first.click()
            page.wait_for_url('**/quickstart.html')
            assert page.get_by_role('heading', name='Run the feedback program', exact=True).count() == 1
            page.get_by_role('main').get_by_role('link', name='execution player', exact=True).click()
            page.wait_for_url('**/execution.html')
            assert page.get_by_role('heading', name='Follow the feedback path', exact=True).count() == 1
            assert page.get_by_role('heading', name='Example schedules', exact=True).count() == 1
            page.goto(base + '/architecture.html')
            diagram = page.locator('object[type="image/svg+xml"]').first
            page.wait_for_function("document.querySelector('object')?.contentDocument?.querySelectorAll('a').length >= 21")
            page.wait_for_function("document.querySelector('object').style.width !== ''")
            initial_width = diagram.evaluate('el => parseFloat(el.style.width)')
            page.click('#diagram-in')
            assert diagram.evaluate('el => parseFloat(el.style.width)') > initial_width
            page.click('#diagram-fit')
            assert diagram.evaluate('el => parseFloat(el.style.width)') <= page.locator('.architecture-viewport').evaluate('el => el.clientWidth')
            page.click('#diagram-actual')
            page.screenshot(path=str(site.parent / 'architecture.png'), full_page=True)
            # Exercise an actual SVG link in its object document and require top-level navigation.
            diagram.evaluate("el => el.contentDocument.querySelector('a').dispatchEvent(new MouseEvent('click', {bubbles: true}))")
            page.wait_for_url('**/modules/*.html')
            for name in sorted(module_pages):
                page.goto(base + '/modules/' + name)
                assert page.locator('h1').count() == 1, name
                assert page.get_by_role('heading', name='Objects and state', exact=True).count() == 1, name
                assert page.locator('object[type="image/svg+xml"]').count() == 1, name
            page.goto(base + '/api.html')
            assert page.locator('body').inner_text().find('ICpuCycleModel') >= 0
            page.goto(base + '/execution.html')
            page.wait_for_function("document.querySelector('#trace-status').textContent.startsWith('Event 1 /')")
            bundle = json.loads((site / '_static/trace-examples.json').read_text())
            for example_index, example in enumerate(bundle['examples']):
                page.select_option('#trace-example', str(example_index))
                page.click('#trace-next')
                observed = json.loads(page.locator('#trace-event').inner_text())
                assert observed == example['events'][1]
                page.click('#trace-prev')
                assert json.loads(page.locator('#trace-event').inner_text()) == example['events'][0]
                page.click('#trace-tick')
                assert json.loads(page.locator('#trace-event').inner_text())['tick'] > example['events'][0]['tick']
                accepted = next(i for i, e in enumerate(example['events']) if e['kind'] == 'ProducerAccepted')
                page.select_option('#trace-jump', str(accepted))
                assert 'last recorded' in page.locator('#trace-observed').inner_text()
                assert '8 at' in page.locator('#trace-observed').inner_text()
                # Both gates and CPU/fast visibility must match original records, including equal-tick events.
                for kind in ['LabelFired', 'OperationStart', 'CpuResultVisible', 'FastResultVisible']:
                    position = next(i for i, e in enumerate(example['events']) if e['kind'] == kind)
                    page.select_option('#trace-jump', str(position))
                    assert json.loads(page.locator('#trace-event').inner_text()) == example['events'][position]
                last = len(example['events']) - 1
                page.locator('#trace-position').evaluate('(el, value) => { el.value = value; el.dispatchEvent(new Event("input", {bubbles: true})); }', last)
                assert 'SimulationCompleted' in page.locator('#trace-status').inner_text()
                assert page.locator('#trace-next').is_disabled()
            page.select_option('#trace-example', '0')
            page.select_option('#trace-speed', '60')
            page.click('#trace-play')
            page.wait_for_function("!document.querySelector('#trace-status').textContent.startsWith('Event 1 /')")
            page.click('#trace-play')
            assert page.locator('#trace-play').inner_text() == 'Play'
            page.screenshot(path=str(site.parent / 'execution-desktop.png'), full_page=True)
            page.set_viewport_size({'width': 390, 'height': 844})
            assert page.locator('#trace-next').is_visible()
            page.screenshot(path=str(site.parent / 'execution-mobile.png'), full_page=True)
            assert not errors, '\n'.join(errors)
            # A missing bundle must leave controls disabled and show a usable error.
            page.route('**/trace-examples.json', lambda route: route.fulfill(status=404, body='missing'))
            page.reload()
            page.wait_for_function("document.querySelector('#trace-status').textContent.startsWith('Cannot load execution data:')")
            assert page.locator('#trace-next').is_disabled()
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--site', type=Path, required=True)
    args = parser.parse_args()
    site = args.site.resolve()
    assert (site / 'index.html').is_file(), 'Build the website first'
    modules = check_links(site)
    print('PASS rendered links and architecture targets', flush=True)
    check_browser(site, modules)
    print(f'PASS website links, {len(modules)} module diagrams, API and three trace playbacks')


if __name__ == '__main__':
    main()
