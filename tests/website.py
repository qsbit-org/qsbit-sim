"""Check rendered links, clickable architecture and trace playback in Chromium."""
import argparse
from functools import lru_cache, partial
from html.parser import HTMLParser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import threading
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

from playwright.sync_api import expect, sync_playwright


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
    for path in diagrams:
        for element in ET.parse(path).iter():
            href = element.get('{http://www.w3.org/1999/xlink}href') or element.get('href')
            if href:
                target = (path.parent / unquote(href)).resolve()
                if not target.exists():
                    errors.append(f'{path.name}: missing SVG target {href}')
    assert not errors, '\n'.join(errors)


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def check_browser(site):
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
            page.get_by_role('main').locator('a[href="quickstart.html"]').first.click()
            page.wait_for_url('**/quickstart.html')
            page.get_by_role('main').locator('a[href="execution.html"]').first.click()
            page.wait_for_url('**/execution.html')
            page.goto(base + '/architecture.html')
            diagram = page.locator('object[type="image/svg+xml"]').first
            page.wait_for_function("Boolean(document.querySelector('object')?.contentDocument?.querySelector('svg a'))")
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
            page.goto(base + '/api.html')
            expect(page.locator('dl.cpp').first).to_be_visible()
            page.goto(base + '/execution.html')
            expect(page.locator('#trace-next')).to_be_enabled()
            bundle = json.loads((site / '_static/trace-examples.json').read_text())
            for example_index, example in enumerate(bundle['examples']):
                page.select_option('#trace-example', str(example_index))
                expect(page.locator('#trace-owners a[data-owner="feedback"]')).to_have_attribute(
                    'href', 'modules/measurement-registers.html')
                page.click('#trace-next')
                observed = json.loads(page.locator('#trace-event').inner_text())
                assert observed == example['events'][1]
                page.click('#trace-prev')
                assert json.loads(page.locator('#trace-event').inner_text()) == example['events'][0]
                page.click('#trace-tick')
                assert json.loads(page.locator('#trace-event').inner_text())['tick'] > example['events'][0]['tick']
                accepted = next(i for i, e in enumerate(example['events']) if e['kind'] == 'CodewordQueued')
                page.select_option('#trace-jump', str(accepted))
                event = example['events'][accepted]
                observed_values = [list(map(int, re.findall(r'\d+', value)))
                                   for value in page.locator('#trace-observed dd').all_text_contents()]
                assert [event['cycle'], event['tick']] in observed_values
                # Both gates and CPU/fast visibility must match original records, including equal-tick events.
                for kind in ['TimingPointTriggered', 'OperationStart', 'MeasurementRegisterUpdated', 'ExecutionFlagsUpdated']:
                    position = next(i for i, e in enumerate(example['events']) if e['kind'] == kind)
                    page.select_option('#trace-jump', str(position))
                    assert json.loads(page.locator('#trace-event').inner_text()) == example['events'][position]
                event = example['events'][position]
                usable = event['tick'] + example['configuration']['tcu']['period']
                observed_values = [list(map(int, re.findall(r'\d+', value)))
                                   for value in page.locator('#trace-observed dd').all_text_contents()]
                assert [event['value'], event['tick'], usable] in observed_values
                last = len(example['events']) - 1
                page.locator('#trace-position').evaluate('(el, value) => { el.value = value; el.dispatchEvent(new Event("input", {bubbles: true})); }', last)
                assert json.loads(page.locator('#trace-event').inner_text()) == example['events'][last]
                assert page.locator('#trace-next').is_disabled()
            page.select_option('#trace-example', '0')
            page.select_option('#trace-speed', '60')
            page.click('#trace-play')
            page.wait_for_function("Number(document.querySelector('#trace-position').value) > 0")
            page.click('#trace-play')
            paused_position = page.locator('#trace-position').input_value()
            page.wait_for_timeout(150)
            assert page.locator('#trace-position').input_value() == paused_position
            page.screenshot(path=str(site.parent / 'execution-desktop.png'), full_page=True)
            page.set_viewport_size({'width': 390, 'height': 844})
            assert page.locator('#trace-next').is_visible()
            page.screenshot(path=str(site.parent / 'execution-mobile.png'), full_page=True)
            assert not errors, '\n'.join(errors)
            # A missing bundle must leave controls disabled and show a usable error.
            page.route('**/trace-examples.json', lambda route: route.fulfill(status=404, body='missing'))
            page.reload()
            expect(page.locator('#trace-status')).to_contain_text('HTTP 404')
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
    check_links(site)
    print('PASS rendered links and architecture targets', flush=True)
    check_browser(site)
    print('PASS website navigation, diagrams, API and trace playback')


if __name__ == '__main__':
    main()
