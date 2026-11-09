#!/usr/bin/env python3
"""Download the palera1n rootful Procursus bootstrap (iOS 15/16, cfver 1800) and dropbear with the
dependencies the bootstrap lacks, into whyred/ipsw/ssh/ (kept out of git).

  fetch-ssh-debs.py
"""
import io
import lzma
import os
import re
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..')
OUT = os.path.join(ROOT, 'whyred/ipsw/ssh')
REPO = 'https://strap.palera.in'
INDEX = REPO + '/dists/iphoneos-arm64/1800/main/binary-iphoneos-arm/Packages.xz'
BOOTSTRAP = 'https://static.palera.in/bootstraps/iphoneos-arm64/1800/bootstrap-1800.tar.zst'
WANT = ['dropbear']


def get(url, path=None):
    # curl: the server answers 403 to Python's default client.
    data = subprocess.run(['curl', '-fsSL', '--max-time', '300', url], check=True,
                          capture_output=True).stdout
    if path:
        open(path, 'wb').write(data)
    return data


def parse(text):
    pkgs, cur = {}, {}
    for line in text.splitlines() + ['']:
        if not line:
            if 'Package' in cur:
                pkgs[cur['Package']] = cur
                for p in re.split(r',\s*', cur.get('Provides', '')):
                    if p:
                        pkgs.setdefault(p.split()[0], cur)
            cur = {}
        elif not line.startswith(' ') and ':' in line:
            k, v = line.split(':', 1)
            cur[k] = v.strip()
    return pkgs


os.makedirs(OUT, exist_ok=True)
bs = os.path.join(OUT, 'bootstrap-1800.tar.zst')
if not os.path.exists(bs):
    print('bootstrap...')
    get(BOOTSTRAP, bs)
status = subprocess.run(f'zstd -dc {bs} | tar xOf - ./Library/dpkg/status', shell=True,
                        check=True, capture_output=True, text=True).stdout
installed = parse(status)
index = parse(lzma.decompress(get(INDEX)).decode(errors='replace'))

need, seen = [], set()
todo = list(WANT)
while todo:
    name = todo.pop()
    if name in seen or name in installed:
        continue
    seen.add(name)
    p = index.get(name)
    if p is None:
        sys.exit(f'{name}: not in the repo')
    need.append(p)
    for dep in re.split(r',\s*', p.get('Depends', '')) + re.split(r',\s*', p.get('Pre-Depends', '')):
        alt = [a.strip().split()[0] for a in dep.split('|') if a.strip()]
        if alt and not any(a in installed for a in alt):
            todo.append(alt[0])

for p in need:
    path = os.path.join(OUT, os.path.basename(p['Filename']))
    if not os.path.exists(path):
        get(REPO + '/' + p['Filename'], path)
    print(f"{p['Package']:16s} {p['Version']:12s} {os.path.basename(path)}")
print(f'{len(installed)} packages in the bootstrap, {len(need)} downloaded to {OUT}')
