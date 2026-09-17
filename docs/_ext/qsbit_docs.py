"""Build C++ references and deterministic trace examples beside the HTML output."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

from sphinx.errors import ExtensionError


def run(command, **kwargs):
    try:
        return subprocess.run(command, check=True, capture_output=True, text=True, timeout=120, **kwargs).stdout
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        detail = getattr(exc, 'stderr', '')
        raise ExtensionError(f'{command[0]} failed: {exc}\n{detail}') from exc


def make_demo(root, build, output, name, program, outcomes):
    trace, summary = output / f'{name}.jsonl', output / f'{name}.summary.json'
    run([str(build / 'qsbit-sim'), '--program', str(build / 'examples' / f'{program}.elf'),
         '--backend', 'scripted', '--outcomes', outcomes, '--trace', str(trace),
         '--summary', str(summary), '--inspect', '4096'])
    state = json.loads(summary.read_text())
    events = [json.loads(line) for line in trace.read_text().splitlines()]
    if not state['success'] or not events or events[-1]['kind'] != 'SimulationCompleted':
        raise ExtensionError(f'{name}: simulation did not complete')
    if any(e['schema'] != 1 for e in events):
        raise ExtensionError(f'{name}: unsupported trace schema')
    if any(a['tick'] > b['tick'] for a, b in zip(events, events[1:])):
        raise ExtensionError(f'{name}: decreasing trace time')
    starts = [e for e in events if e['kind'] == 'OperationStart']
    expected = [('h', 1160), ('cx', 1200), ('measure', 1240), ('measure', 1240)] if program == 'bell' else [
        ('x', 1160), ('measure', 1240), ('x' if outcomes == '1' else 'z', 3560)]
    observed = [(e['operation'], e['tick']) for e in starts]
    if observed != expected:
        raise ExtensionError(f'{name}: unexpected operation schedule {observed}, expected {expected}')
    measurement_count = 2 if program == 'bell' else 1
    for kind, tick in [('MeasurementSampled', 1280), ('ResultReady', 1300),
                       ('CpuResultVisible', 1305), ('FastResultVisible', 1340)]:
        records = [e for e in events if e['kind'] == kind]
        if len(records) != measurement_count or any(e['tick'] != tick for e in records):
            raise ExtensionError(f'{name}: unexpected {kind} count or visibility tick')
    if state['memory']['4096'] != int(outcomes.split(',')[0]):
        raise ExtensionError(f'{name}: wrong final measurement signature')
    return {'name': name, 'elf_sha256': hashlib.sha256((build / 'examples' / f'{program}.elf').read_bytes()).hexdigest(), 'program': (root / 'examples' / f'{program}.S').read_text(),
            'configuration': state['configuration'], 'events': events,
            'summary': {'stop_tick': state['stop_tick'], 'memory': state['memory']}}


def prepare(app):
    root = Path(app.srcdir).parent
    build = Path(app.config.qsbit_build)
    if not build.is_absolute():
        build = root / build
    output = Path(app.outdir).parent / 'generated'
    output.mkdir(parents=True, exist_ok=True)
    xml = output / 'doxygen'
    config = '\n'.join([
        'PROJECT_NAME = qsbit-sim', f'INPUT = "{root / "include/qsbit"}"',
        f'OUTPUT_DIRECTORY = "{xml}"', 'RECURSIVE = YES', 'FILE_PATTERNS = *.hpp',
        'EXTRACT_ALL = YES', 'EXTRACT_PRIVATE = YES', 'GENERATE_HTML = NO',
        'GENERATE_LATEX = NO', 'GENERATE_XML = YES', 'QUIET = YES',
        'WARN_AS_ERROR = YES', 'WARN_IF_UNDOCUMENTED = NO',
        'ENABLE_PREPROCESSING = YES', 'MACRO_EXPANSION = NO',
    ])
    run([app.config.qsbit_doxygen, '-'], input=config)
    app.config.breathe_projects = {'qsbit': str(xml / 'xml')}
    demos = [make_demo(root, build, output, 'bell', 'bell', '1,1'),
             make_demo(root, build, output, 'feedback-one', 'feedback', '1'),
             make_demo(root, build, output, 'feedback-zero', 'feedback', '0')]
    app._qsbit_source_revision = run(['git', 'rev-parse', 'HEAD'], cwd=root).strip()
    bundle = {'schema': 1, 'revision': app._qsbit_source_revision, 'examples': demos}
    static = Path(app.outdir) / '_static'
    static.mkdir(parents=True, exist_ok=True)
    (static / 'trace-examples.json').write_text(json.dumps(bundle))


def source_links(app, docname, source):
    """Keep Markdown checkout links; point website source links at the matching revision."""
    root = Path(app.srcdir).parent
    page = Path(app.srcdir) / (docname + '.md')
    def replace(match):
        target = match[2]
        if ':' in target or target.startswith('#'):
            return match[0]
        path = (page.parent / target.split('#')[0]).resolve()
        if path.suffix == '.md' and path.is_relative_to(Path(app.srcdir)):
            return match[0]
        if path.is_file() and path.is_relative_to(root):
            relative = path.relative_to(root).as_posix()
            return f'[{match[1]}](https://github.com/Zhaoyilunnn/qsbit-sim/blob/{app._qsbit_source_revision}/{relative})'
        return match[0]
    source[0] = re.sub(r'\[([^\]\n]+)\]\(([^\s)]+)\)', replace, source[0])


def setup(app):
    app.add_config_value('qsbit_build', 'build-gcc', 'env')
    app.add_config_value('qsbit_doxygen', 'doxygen', 'env')
    app.connect('builder-inited', prepare)
    app.connect('source-read', source_links)
    return {'version': '1', 'parallel_read_safe': True, 'parallel_write_safe': True}
