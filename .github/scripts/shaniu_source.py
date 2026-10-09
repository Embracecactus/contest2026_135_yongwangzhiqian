#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Resolve Actions inputs before repo sync; keep manifest and team source identical."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

TEAM = 'contest2026_135_yongwangzhiqian'


def sha(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9a-f]{40}', value):
        raise ValueError('expected a full lowercase commit SHA')
    return value


def repository(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+', value):
        raise ValueError('invalid GitHub repository')
    if value.split('/')[1] in ('.', '..'):
        raise ValueError('invalid repository name')
    return value


def resolve_source(env, event):
    kind = env.get('GITHUB_EVENT_NAME', event.get('event_name'))
    if kind not in ('push', 'pull_request', 'workflow_dispatch'):
        raise ValueError('unsupported event; privileged PR events are not allowed')
    repo = repository(env['GITHUB_REPOSITORY'])
    candidate = sha(env['GITHUB_SHA'])
    ref = env['GITHUB_REF']
    if not re.fullmatch(r'refs/(heads|tags|pull)/[A-Za-z0-9_./-]+', ref) or '..' in ref:
        raise ValueError('invalid event ref')
    base, head, head_repo = None, candidate, None
    if kind == 'pull_request':
        number = event.get('number')
        if type(number) is not int or number <= 0 or ref != f'refs/pull/{number}/merge':
            raise ValueError('PR requires its candidate merge ref')
        pr = event['pull_request']
        if repository(pr['base']['repo']['full_name']) != repo:
            raise ValueError('PR candidate must belong to the base repository')
        base, head = sha(pr['base']['sha']), sha(pr['head']['sha'])
        head_repo = repository(pr['head']['repo']['full_name'])
        if pr.get('merge_commit_sha') and sha(pr['merge_commit_sha']) != candidate:
            raise ValueError('PR merge SHA differs from the event candidate')
    else:
        if not ref.startswith(('refs/heads/', 'refs/tags/')):
            raise ValueError('push/dispatch requires a branch or tag ref')
        if kind == 'push':
            if sha(event['after']) != candidate:
                raise ValueError('push SHA differs from the event candidate')
            if event.get('before') and event['before'] != '0' * 40:
                base = sha(event['before'])
    return dict(schema=1, event=kind, repository=repo,
                repository_url=f'https://github.com/{repo}.git', source_ref=candidate,
                candidate_sha=candidate, head_sha=head, base_sha=base,
                head_repository=head_repo)


def write_override(identity, path):
    repo = repository(identity['repository'])
    owner, name = repo.split('/')
    if name != TEAM:
        raise ValueError('candidate repository must retain the team project name')
    root = ET.Element('manifest')
    ET.SubElement(root, 'remote', name='shaniu-candidate', fetch=f'https://github.com/{owner}/')
    ET.SubElement(root, 'extend-project', name=TEAM, path=TEAM,
                  remote='shaniu-candidate', revision=sha(identity['candidate_sha']))
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(root).write(path, encoding='utf-8', xml_declaration=True)


def git(directory, *args):
    return subprocess.check_output(['git', '-C', str(directory), *args], text=True).strip()


def verify_checkouts(identity, manifest_dir, source_dir, event_dir):
    result = dict(identity)
    candidate = sha(identity['candidate_sha'])
    for field, directory in (('manifest_sha', manifest_dir), ('source_sha', source_dir), ('event_sha', event_dir)):
        result[field] = git(directory, 'rev-parse', 'HEAD')
        if result[field] != candidate:
            raise ValueError(f'{field} differs from candidate SHA')
    if identity['event'] == 'pull_request':
        parents = git(event_dir, 'show', '-s', '--format=%P', 'HEAD').split()
        if parents != [identity['base_sha'], identity['head_sha']]:
            raise ValueError('candidate parents differ from the event base/head')
        result['candidate_parents'] = parents
    return result


def verify_delivery(identity, delivery):
    """Re-resolve the event independently of the downloaded public artifact."""
    archived = json.loads((delivery / 'source-inputs.json').read_text())
    for key, value in identity.items():
        if archived.get(key) != value:
            raise ValueError(f'delivered {key} differs from event')
    for field in ('manifest_sha', 'source_sha', 'event_sha'):
        if archived.get(field) != identity['candidate_sha']:
            raise ValueError(f'delivered {field} differs from candidate')
    root = ET.parse(delivery / 'declared-manifest.xml').getroot()
    project = root.find(f"./project[@path='{TEAM}']")
    if project is None or project.get('revision') != identity['candidate_sha']:
        raise ValueError('delivered manifest does not pin team source')
    remotes = {r.get('name'): r.get('fetch') for r in root.findall('remote')}
    url = remotes[project.get('remote')].rstrip('/') + '/' + project.get('name') + '.git'
    if url != identity['repository_url']:
        raise ValueError('delivered manifest uses a different source repository')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('resolve', 'override', 'verify', 'delivery'))
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--event-checkout', type=Path)
    args = parser.parse_args()
    if args.action in ('resolve', 'delivery'):
        identity = resolve_source(os.environ, json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text()))
    else:
        identity = json.loads(args.inputs.read_text())
    if args.action == 'resolve':
        args.inputs.write_text(json.dumps(identity, indent=2) + '\n')
    elif args.action == 'override':
        write_override(identity, args.output)
    elif args.action == 'verify':
        identity = verify_checkouts(identity, args.manifest, args.source, args.event_checkout)
        args.inputs.write_text(json.dumps(identity, indent=2) + '\n')
    else:
        verify_delivery(identity, args.inputs)
    print(json.dumps(identity, sort_keys=True))


if __name__ == '__main__':
    main()
