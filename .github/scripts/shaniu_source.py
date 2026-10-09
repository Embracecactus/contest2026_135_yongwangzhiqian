#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Resolve Actions inputs before repo sync; keep manifest and team source identical."""
import argparse
import json
import hashlib
import shutil
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
    base, head, head_repo, reported_merge = None, candidate, None, None
    if kind == 'pull_request':
        number = event.get('number')
        if type(number) is not int or number <= 0 or ref != f'refs/pull/{number}/merge':
            raise ValueError('PR requires its candidate merge ref')
        pr = event['pull_request']
        if repository(pr['base']['repo']['full_name']) != repo:
            raise ValueError('PR candidate must belong to the base repository')
        base, head = sha(pr['base']['sha']), sha(pr['head']['sha'])
        head_repo = repository(pr['head']['repo']['full_name'])
        # PR webhook merge_commit_sha can lag on synchronize. The Actions SHA
        # and the checked-out commit's base/head parents are authoritative.
        if pr.get('merge_commit_sha'):
            reported_merge = sha(pr['merge_commit_sha'])
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
                candidate_sha=candidate, head_sha=head, base_sha=base, fetch_ref=ref,
                head_repository=head_repo, reported_merge_sha=reported_merge)


def write_override(identity, path):
    repo = repository(identity['repository'])
    owner, name = repo.split('/')
    if name != TEAM:
        raise ValueError('candidate repository must retain the team project name')
    root = ET.Element('manifest')
    ET.SubElement(root, 'remote', name='shaniu-candidate', fetch=f'https://github.com/{owner}/')
    ET.SubElement(root, 'extend-project', name=TEAM, path=TEAM,
                  remote='shaniu-candidate', revision=sha(identity['candidate_sha']),
                  upstream=identity['fetch_ref'])
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
    result.update(verify_candidate(identity, event_dir))
    return result


def verify_candidate(identity, directory):
    if git(directory, 'rev-parse', 'HEAD') != sha(identity['candidate_sha']):
        raise ValueError('event checkout differs from candidate SHA')
    if identity['event'] == 'pull_request':
        parents = git(directory, 'show', '-s', '--format=%P', 'HEAD').split()
        if parents != [identity['base_sha'], identity['head_sha']]:
            raise ValueError('candidate parents differ from the event base/head')
        return {'candidate_parents': parents}
    return {}


MAP_NAMES = {'bl1': 'bl.map', 'bl2': 'bl2.map', 'cp': 'nuttx.map', 'ap': 'nuttx.map'}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def evidence_hashes(build):
    expected = {}
    for section, suffix in (('elfs', '.elf'), ('inputs', '.bin')):
        for name, record in build[section].items():
            expected[name + suffix] = record['sha256']
    for role in ('cp', 'ap'):
        expected[role + '.config'] = build['roles'][role]['resolved_config_sha256']
        expected[role + '.defconfig'] = build['roles'][role]['seed_defconfig_sha256']
    return expected


def stage_build_evidence(manifest_path, repository_path, output_dir):
    """Copy only matching public build inputs; never export a whole build tree."""
    manifest_path = Path(manifest_path).resolve()
    pair = manifest_path.parents[2]
    repository_path = Path(repository_path).resolve()
    build = json.loads(manifest_path.read_text())
    sources = {}

    def bounded(root, relative):
        path = (root / relative).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError('build evidence escapes its root or is missing')
        return path

    for section, suffix in (('elfs', '.elf'), ('inputs', '.bin')):
        for name, record in build[section].items():
            if name not in ('bl1', 'bl2', 'boot', 'cp', 'ap'):
                raise ValueError('unexpected public build role')
            sources[name + suffix] = bounded(pair, record['path'])
    for role, map_name in MAP_NAMES.items():
        elf = sources[role + '.elf']
        sources[role + '.map'] = bounded(pair, elf.parent / map_name)
    for role in ('cp', 'ap'):
        sources[role + '.config'] = bounded(pair, sources[role + '.elf'].parent / '.config')
        sources[role + '.defconfig'] = bounded(pair, f'configs/mcuboot/{role}/defconfig')
    sources['partitions.csv'] = bounded(repository_path, build['layout']['partition'])
    # Validate every input before copying anything into the public artifact.
    for name, expected in evidence_hashes(build).items():
        if digest(sources[name]) != expected:
            raise ValueError(f'build evidence hash mismatch: {name}')
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        shutil.copyfile(source, output_dir / name)


def verify_build_evidence(delivery):
    release = json.loads((delivery / 'firmware/release.json').read_text())
    build = json.loads((delivery / 'firmware' / release['build_manifest']['path']).read_text())
    evidence = delivery / 'build-evidence'
    for name, expected in evidence_hashes(build).items():
        if digest(evidence / name) != expected:
            raise ValueError(f'delivered build evidence hash mismatch: {name}')
    for name in (*[role + '.map' for role in MAP_NAMES], 'partitions.csv'):
        if not (evidence / name).is_file() or not (evidence / name).stat().st_size:
            raise ValueError(f'missing public build evidence: {name}')
    forbidden = ('ENGINEERING_TEST', 'FACTORY_DIAGNOSTICS', 'POWER_PREPARE_VALIDATION',
                 'AUDIO_PLAYBACK_VALIDATION', 'AUDIO_CAPTURE_VALIDATION', 'AUDIO_PIPELINE_VALIDATION')
    for role in ('cp', 'ap'):
        config = (evidence / (role + '.config')).read_text()
        if any(re.search(r'^CONFIG_BK7258_' + flag + r'=[ym]$', config, re.M) for flag in forbidden):
            raise ValueError('engineering entry enabled in standard delivery')


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
    verify_build_evidence(delivery)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('resolve', 'override', 'verify', 'delivery', 'stage-evidence'))
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--event-checkout', type=Path)
    args = parser.parse_args()
    if args.action == 'stage-evidence':
        stage_build_evidence(args.inputs, args.source, args.output)
        print('matching public ELF/map/BIN/config/partition evidence staged')
        return
    if args.action in ('resolve', 'delivery'):
        identity = resolve_source(os.environ, json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text()))
    else:
        identity = json.loads(args.inputs.read_text())
    if args.action == 'resolve':
        if args.event_checkout is None:
            raise ValueError('resolve requires an event checkout')
        verify_candidate(identity, args.event_checkout)
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
