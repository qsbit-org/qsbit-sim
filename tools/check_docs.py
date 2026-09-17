#!/usr/bin/env python3
"""Validate local document links, source excerpts and module CTest references."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

SOURCE = re.compile(r'<!-- source: (\{[^\n]+\}) -->\n(.*?)<!-- /source -->', re.S)
LINK = re.compile(r'\[[^\]\n]+\]\(([^\s)]+)\)')
CTEST = re.compile(r'^\*\*CTest:\*\* (.+)$', re.M)


def excerpt(root, spec):
    text = (root / spec['path']).read_text()
    start, end = spec['start'], spec['end']
    if text.count(start) != 1 or text.count(end) != 1:
        raise ValueError(f"non-unique source boundaries: {spec['path']}")
    first, last = text.index(start), text.index(end)
    if first >= last:
        raise ValueError(f"reversed source boundaries: {spec['path']}")
    return '```cpp\n' + text[first:last].rstrip() + '\n```\n'


def anchors(text):
    result, counts = set(), {}
    # Fenced source code can contain # directives, which are not Markdown headings.
    text = re.sub(r'```.*?```', '', text, flags=re.S)
    for title in re.findall(r'^#{1,6}\s+(.+?)\s*#*$', text, re.M):
        slug = re.sub(r'[^\w\- ]', '', title.lower()).replace(' ', '-')
        index = counts.get(slug, 0)
        counts[slug] = index + 1
        result.add(slug if not index else f'{slug}-{index}')
    return result


def check(root, test_names, write=False):
    errors = []
    documents = sorted((root / 'docs').rglob('*.md'))
    documents += [root / name for name in ('README.md', 'AGENTS.md') if (root / name).exists()]
    for path in documents:
        text = path.read_text()
        def replace(match):
            try:
                expected = excerpt(root, json.loads(match[1]))
            except (OSError, ValueError, KeyError) as exc:
                errors.append(f'{path}: {exc}')
                return match[0]
            if match[2] != expected and not write:
                errors.append(f'{path}: stale source excerpt; run tools/check_docs.py --write')
            return '<!-- source: ' + match[1] + ' -->\n' + expected + '<!-- /source -->' if write else match[0]
        updated = SOURCE.sub(replace, text)
        if text.count('<!-- source:') != len(list(SOURCE.finditer(text))):
            errors.append(f'{path}: malformed source marker')
        if write and updated != text:
            path.write_text(updated)
        for target in LINK.findall(re.sub(r'```.*?```', '', updated, flags=re.S)):
            url = urlsplit(target)
            if url.scheme or url.netloc:
                continue
            destination = (path.parent / unquote(url.path)).resolve() if url.path else path
            if not destination.exists():
                errors.append(f'{path}: missing link target {target}')
            elif url.fragment and destination.suffix == '.md':
                if unquote(url.fragment) not in anchors(destination.read_text()):
                    errors.append(f'{path}: missing heading {target}')
        references = CTEST.findall(updated)
        if path.parent == root / 'docs/modules' and path.name != 'README.md' and not references:
            errors.append(f'{path}: missing CTest evidence')
        for reference in references:
            names = re.findall(r'`([^`]+)`', reference)
            if not names:
                errors.append(f'{path}: empty CTest evidence')
            for name in names:
                if name not in test_names:
                    errors.append(f'{path}: unregistered CTest {name}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True, help='configured BUILD_TESTING=ON directory')
    parser.add_argument('--ctest', default='ctest')
    parser.add_argument('--write', action='store_true', help='refresh source excerpts')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    result = subprocess.run([args.ctest, '--test-dir', str(args.build), '--show-only=json-v1'],
                            check=True, capture_output=True, text=True)
    names = {test['name'] for test in json.loads(result.stdout)['tests']}
    errors = check(root, names, args.write)
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('PASS documentation links, source excerpts and registered CTest references')
    return 0


if __name__ == '__main__':
    sys.exit(main())
