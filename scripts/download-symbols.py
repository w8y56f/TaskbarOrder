#!/usr/bin/env python3
r"""Download only Microsoft public PDBs requested by TaskbarOrder environment.json.

Runs on macOS/Windows with Python 3; the application itself does not use Python.
Copy the resulting symbols folder to %LOCALAPPDATA%\TaskbarOrder\symbols.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import urllib.request


def requests_from_report(report: dict) -> list[tuple[str, str]]:
    if report.get('format') != 1 or report.get('architecture') != 'x64':
        raise ValueError('Unsupported environment report format or architecture')
    modules = report.get('modules')
    if not isinstance(modules, list) or len(modules) != 2:
        raise ValueError('Expected exactly two taskbar modules')
    result = []
    seen = set()
    for module in modules:
        if not isinstance(module, dict):
            raise ValueError('Invalid module entry')
        filename, pdb, key = (module.get(field) for field in ('module', 'pdb', 'key'))
        if filename not in ('taskbar.dll', 'Taskbar.View.dll', 'ExplorerExtensions.dll') or filename in seen:
            raise ValueError('Unexpected or repeated taskbar module')
        seen.add(filename)
        if not isinstance(pdb, str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,190}\.pdb', pdb):
            raise ValueError('Invalid PDB filename')
        if not isinstance(key, str) or not re.fullmatch(r'[0-9A-F]{33,40}', key):
            raise ValueError('Invalid PDB GUID/age key')
        result.append((pdb, key))
    if 'taskbar.dll' not in seen:
        raise ValueError('Missing taskbar.dll')
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('symbols'))
    args = parser.parse_args()
    pairs = requests_from_report(json.loads(args.report.read_text(encoding='utf-8-sig')))
    for pdb, key in pairs:
        destination = args.output / pdb / key / pdb
        if destination.exists():
            print(f'Already present: {destination}')
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix('.pdb.partial')
        url = f'https://msdl.microsoft.com/download/symbols/{pdb}/{key}/{pdb}'
        print(f'Downloading {pdb} ({key}) …', flush=True)
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'TaskbarOrder/0.1'})
            with urllib.request.urlopen(request, timeout=30) as response, temporary.open('wb') as output:
                first = response.read(65536)
                if not first.startswith(b'Microsoft C/C++ MSF'):
                    raise ValueError('The server did not return an uncompressed MSF PDB')
                output.write(first)
                size = len(first)
                while chunk := response.read(65536):
                    size += len(chunk)
                    if size > 512 * 1024 * 1024:
                        raise ValueError('PDB exceeds the 512 MiB limit')
                    output.write(chunk)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
        print(f'Saved {destination}')
    print('Copy this symbols folder into %LOCALAPPDATA%\\TaskbarOrder, then click Prepare symbols in the application.')


if __name__ == '__main__':
    main()
