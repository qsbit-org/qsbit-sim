"""Check rendered links, clickable architecture and trace playback in Chromium."""

import argparse
import json
import threading
import xml.etree.ElementTree as ET
from functools import lru_cache, partial
from html.parser import HTMLParser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

from playwright.sync_api import expect, sync_playwright


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.ids, self.links = set(), []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        for key in ("href", "src", "data"):
            if key in attrs:
                self.links.append(attrs[key])


def check_links(site):
    pages = {p.resolve(): Page(p.read_text()) for p in site.rglob("*.html")}

    @lru_cache(maxsize=None)
    def destination(parent, relative):
        target = (parent / relative).resolve()
        return target / "index.html" if target.is_dir() else target

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
                errors.append(f"{path.name}: missing {href}")
            elif target.suffix == ".svg":
                diagrams.add(target)
            elif (
                url.fragment and target in pages and unquote(url.fragment) not in pages[target].ids
            ):
                errors.append(f"{path.name}: missing anchor {href}")
    for path in diagrams:
        for element in ET.parse(path).iter():
            href = element.get("{http://www.w3.org/1999/xlink}href") or element.get("href")
            if href:
                target = (path.parent / unquote(href)).resolve()
                if not target.exists():
                    errors.append(f"{path.name}: missing SVG target {href}")
    assert not errors, "\n".join(errors)


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def check_browser(site):
    server = ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(site)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{server.server_port}"
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch()
            page = browser.new_page(viewport={"width": 1440, "height": 1050})
            errors = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(base + "/index.html")
            page.get_by_role("main").locator('a[href="quickstart.html"]').first.click()
            page.wait_for_url("**/quickstart.html")
            page.get_by_role("main").locator('a[href="execution.html"]').first.click()
            page.wait_for_url("**/execution.html")
            page.goto(base + "/architecture.html")
            diagram = page.locator('object[type="image/svg+xml"]').first
            page.wait_for_function(
                "Boolean(document.querySelector('object')?.contentDocument?.querySelector('svg a'))"
            )
            page.wait_for_function("document.querySelector('object').style.width !== ''")
            initial_width = diagram.evaluate("el => parseFloat(el.style.width)")
            page.click("#diagram-in")
            assert diagram.evaluate("el => parseFloat(el.style.width)") > initial_width
            page.click("#diagram-fit")
            assert diagram.evaluate("el => parseFloat(el.style.width)") <= page.locator(
                ".architecture-viewport"
            ).evaluate("el => el.clientWidth")
            page.click("#diagram-actual")
            page.screenshot(path=str(site.parent / "architecture.png"), full_page=True)
            # Exercise an actual SVG link in its object document and require top-level navigation.
            diagram.evaluate(
                "el => el.contentDocument.querySelector('a').dispatchEvent(new MouseEvent('click', {bubbles: true}))"
            )
            page.wait_for_url("**/modules/*.html")
            page.goto(base + "/api.html")
            expect(page.locator("dl.cpp").first).to_be_visible()
            page.goto(base + "/execution.html")
            expect(page.locator("#trace-next")).to_be_enabled()
            bundle = json.loads((site / "_static/trace-examples.json").read_text())
            for example_index, example in enumerate(bundle["examples"]):
                page.select_option("#trace-example", str(example_index))
                expect(page.locator("#trace-node-registers a")).to_have_attribute(
                    "href", "modules/measurement-registers.html"
                )
                page.locator(".replay-records > summary").click()
                initial = json.loads(page.locator("#trace-event").inner_text())
                initial_index = example["events"].index(initial)
                page.click("#trace-next")
                observed = json.loads(page.locator("#trace-event").inner_text())
                assert observed == example["events"][initial_index + 1]
                page.click("#trace-back")
                assert json.loads(page.locator("#trace-event").inner_text()) == initial
                page.click("#trace-tick")
                tick_event = json.loads(page.locator("#trace-event").inner_text())
                assert tick_event["tick"] > example["events"][0]["tick"]
                tick_index = example["events"].index(tick_event)
                assert example["events"][tick_index + 1]["tick"] > tick_event["tick"]
                for stage in ("fetch", "decode", "execute"):
                    position = next(
                        i
                        for i, e in enumerate(example["events"])
                        if e["kind"] == "CpuPipelineUpdated" and e["pipeline"][stage]
                    )
                    page.select_option("#trace-jump", str(position))
                    slot = example["events"][position]["pipeline"][stage]
                    cell = page.locator(f'.cpu-stage[data-stage="{stage}"]')
                    expect(cell).to_contain_text(f"#{slot['id']} · 0x{slot['pc']:08x}")
                    assert cell.evaluate("el => el.classList.contains('occupied')")
                accepted = next(
                    i for i, e in enumerate(example["events"]) if e["kind"] == "CodewordQueued"
                )
                page.select_option("#trace-jump", str(accepted))
                event = example["events"][accepted]
                expect(page.locator("#trace-state-reserve")).to_contain_text(
                    f"p{event['port']} · cw {event['codeword']}"
                )
                enqueued = next(
                    i for i, e in enumerate(example["events"]) if e["kind"] == "TimingPointEnqueued"
                )
                page.select_option("#trace-jump", str(enqueued))
                expect(page.locator("#trace-state-timing .queue-entry").first).to_contain_text(
                    f"t = {event['cycle']}"
                )
                sampled = next(
                    i for i, e in enumerate(example["events"]) if e["kind"] == "MeasurementSampled"
                )
                page.select_option("#trace-jump", str(sampled))
                expect(page.locator("#trace-state-registers")).to_contain_text(
                    "No result delivered"
                )
                # Both gates and CPU/fast visibility must match original records, including equal-tick events.
                for kind in [
                    "TimingPointTriggered",
                    "OperationStart",
                    "MeasurementRegisterUpdated",
                    "ExecutionFlagsUpdated",
                ]:
                    position = next(i for i, e in enumerate(example["events"]) if e["kind"] == kind)
                    page.select_option("#trace-jump", str(position))
                    assert (
                        json.loads(page.locator("#trace-event").inner_text())
                        == example["events"][position]
                    )
                    if kind == "TimingPointTriggered":
                        assert page.locator("#trace-node-timing").evaluate(
                            "el => el.classList.contains('changed')"
                        )
                event = example["events"][position]
                expect(page.locator("#trace-state-trigger")).to_contain_text(
                    f"Flags committed at {event['tick']} ns"
                )
                expect(page.locator("#trace-delivery")).to_contain_text(f"{event['tick']} ns")
                # Seeking backwards removes feedback that has not arrived yet.
                page.select_option("#trace-jump", str(accepted))
                expect(page.locator("#trace-state-registers")).to_contain_text(
                    "No result delivered"
                )
                last = len(example["events"]) - 1
                page.locator("#trace-position").evaluate(
                    '(el, value) => { el.value = value; el.dispatchEvent(new Event("input", {bubbles: true})); }',
                    example["events"][last]["tick"],
                )
                assert (
                    json.loads(page.locator("#trace-event").inner_text()) == example["events"][last]
                )
                assert page.locator("#trace-next").is_disabled()
                page.locator(".replay-records > summary").click()
            page.select_option("#trace-example", "0")
            page.select_option("#trace-speed", "60")
            page.click("#trace-play")
            page.wait_for_function("Number(document.querySelector('#trace-position').value) > 0")
            page.click("#trace-play")
            paused_position = page.locator("#trace-position").input_value()
            page.wait_for_timeout(150)
            assert page.locator("#trace-position").input_value() == paused_position
            page.get_by_role("button", name="CPU result", exact=True).click()
            page.screenshot(path=str(site.parent / "execution-desktop.png"), full_page=True)
            page.click("#trace-expand")
            expect(page.locator("#trace-expand")).to_have_attribute("aria-pressed", "true")
            page.locator("#trace-player").screenshot(
                path=str(site.parent / "execution-expanded.png")
            )
            page.keyboard.press("Escape")
            expect(page.locator("#trace-expand")).to_have_attribute("aria-pressed", "false")
            page.evaluate("document.body.dataset.theme = 'dark'")
            page.screenshot(path=str(site.parent / "execution-dark.png"), full_page=True)
            page.set_viewport_size({"width": 390, "height": 844})
            assert page.locator("#trace-tick").is_visible()
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
            page.screenshot(path=str(site.parent / "execution-mobile.png"), full_page=True)
            assert not errors, "\n".join(errors)
            check_replay_state(page)
            # A missing bundle must leave controls disabled and show a usable error.
            page.route(
                "**/trace-examples.json", lambda route: route.fulfill(status=404, body="missing")
            )
            page.reload()
            expect(page.locator("#trace-status")).to_contain_text("HTTP 404")
            assert page.locator("#trace-next").is_disabled()
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def check_replay_state(page):
    """Exercise scope and seek boundaries with a small independent recording."""
    result = page.evaluate("""() => {
      const events = [
        {kind: 'SessionStarted', tick: 0, epoch: 1},
        {kind: 'CodewordQueued', tick: 1, epoch: 1, core: 1, port: 2, codeword: 7, cycle: 12},
        {kind: 'TimingPointSubmitted', tick: 2, epoch: 1, core: 1, label: 1, cycle: 12},
        {kind: 'TimingPointEnqueued', tick: 3, epoch: 1, core: 1, label: 1, cycle: 2, value: 1},
        {kind: 'MeasurementRegisterUpdated', tick: 4, epoch: 1, core: 2, id: 1, targets: [0], value: 1},
        {kind: 'OperationStart', tick: 5, epoch: 1, core: 1, id: 9, port: 0},
        {kind: 'OperationStart', tick: 5, epoch: 1, core: 2, id: 9, port: 1},
        {kind: 'TimingPointTriggered', tick: 6, epoch: 1, core: 1, label: 1, cycle: 12},
        {kind: 'TimerPaused', tick: 7, epoch: 1, core: 1},
        {kind: 'SessionReset', tick: 8, epoch: 2},
      ];
      const r = new QsbitTrace.Recording({events, cores: [{id: 1, configuration: {mappings: [
        {port: 2, codeword: 7, actions: [{port: 0, operation: 'x'}, {port: 1, operation: 'y'}]}
      ]}}]});
      const repeats = new QsbitTrace.Recording({events: Array.from({length: 260}, (_, i) =>
        ({kind: i === 0 ? 'SessionStarted' : 'CpuStalled', tick: i, epoch: 1, id: 1, detail: 'memory response'}))});
      return {queued: r.at(3), feedback: r.at(4), concurrent: r.at(6), fired: r.at(7),
        paused: r.at(8), reset: r.at(9), rewind: r.at(3), intervals: r.intervals,
        next: r.nextTick(4), previous: r.previousTick(6), configuration: r.configuration(2),
        mapped: [...r.queuedEvents(1, r.at(3).cores[1].queue)],
        stalled: repeats.at(259).cores[0].stall.tick, earlier: repeats.at(129).cores[0].stall.tick,
        changes: repeats.steps, seek: [r.indexAtTick(5), r.indexAtTick(100), r.indexAtTick(-1)]};
    }""")
    queued = result["queued"]["cores"]["1"]["queue"]
    assert queued[0]["point"] == 12
    assert queued[0]["codewords"][0]["codeword"] == 7
    assert not result["feedback"]["cores"]["1"]["registers"]
    assert result["feedback"]["cores"]["2"]["registers"]["0"]["value"] == 1
    assert len(result["concurrent"]["active"]) == 2
    assert not result["fired"]["cores"]["1"]["queue"]
    assert result["fired"]["cores"]["1"]["occupancy"]["value"] == 0
    assert result["paused"]["cores"]["1"]["paused"]["tick"] == 7
    assert not result["reset"]["active"]
    assert "1" not in result["reset"]["cores"]
    assert result["rewind"] == result["queued"]
    assert all(i["end"] == 8 and i["aborted"] for i in result["intervals"])
    assert result["next"] == 6 and result["previous"] == 4
    assert result.get("configuration") is None
    assert result["mapped"] == [
        [0, [{"point": 12, "operation": "x"}]],
        [1, [{"point": 12, "operation": "y"}]],
    ]
    assert result["stalled"] == 259 and result["earlier"] == 129
    assert result["changes"] == [0, 1]
    assert result["seek"] == [6, 9, 0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", type=Path, required=True)
    args = parser.parse_args()
    site = args.site.resolve()
    assert (site / "index.html").is_file(), "Build the website first"
    check_links(site)
    print("PASS rendered links and architecture targets", flush=True)
    check_browser(site)
    print("PASS website navigation, diagrams, API and trace playback")


if __name__ == "__main__":
    main()
