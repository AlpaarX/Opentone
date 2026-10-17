#!/usr/bin/env python3
"""Install the private-use House 4 MIDI demo without redistributing its music."""
import gzip
import hashlib
import os
from pathlib import Path
import sys
import urllib.request
import xml.etree.ElementTree as ET

URL = ('https://raw.githubusercontent.com/keijiro/music/'
       '98352836bf0fae121f05d013205793fe6427bd3e/'
       'Sketches/House%204%20Project/House%204.als')
SHA256 = '1ef78d2e067bf62ed216fb7e3aedab7bb589ae24047339ce16a4d0e7e5e8333b'


def install():
    if sys.platform == 'darwin':
        data = Path.home() / 'Library'
    elif sys.platform == 'win32':
        data = Path(os.environ['APPDATA'])
    else:
        data = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config')))
    folder = data / 'Opentone/Demo/House 4'
    original = folder / 'House 4.original.als'
    if original.exists():
        downloaded = original.read_bytes()
    else:
        with urllib.request.urlopen(URL, timeout=30) as response:
            downloaded = response.read(1024 * 1024)
    if hashlib.sha256(downloaded).hexdigest() != SHA256:
        raise ValueError('The source set differs from the reviewed version; no files were changed.')
    root = ET.fromstring(gzip.decompress(downloaded))
    branches = root.findall('.//DrumBranch')
    assert len(branches) == 2 and len(root.findall('.//Operator')) == 3
    tracks = root.find('./LiveSet/Tracks')
    # These two Vengeance sample loops are absent from the author's public repository.
    for track in list(tracks):
        if track.get('Id') in ('5', '6'):
            tracks.remove(track)
    # Connect the existing C1 kick MIDI to the two nested chains.
    for branch, note in zip(branches, (36, 60)):
        branch.find('BranchInfo/ReceivingNote').set('Value', str(note))
    assert len(root.findall('./LiveSet/Tracks/*/DeviceChain/MainSequencer/'
                            'ClipTimeable/ArrangerAutomation/Events/MidiClip')) == 3
    assert not root.findall('./LiveSet/Tracks/*/DeviceChain/MainSequencer/'
                            'Sample/ArrangerAutomation/Events/AudioClip')
    adapted = ET.tostring(root, encoding='utf-8', xml_declaration=True)
    target = folder / 'House 4.als'
    if target.exists() and gzip.decompress(target.read_bytes()) != adapted:
        raise ValueError(f'{target} has local edits; move it aside before installing.')
    folder.mkdir(parents=True, exist_ok=True)
    if not original.exists():
        original.write_bytes(downloaded)
    staging = target.with_suffix('.als.tmp')
    staging.write_bytes(gzip.compress(adapted, mtime=0))
    staging.replace(target)
    print(f'Installed: {target}\nHouse 4 by Keijiro Takahashi. Private use only.')


if __name__ == '__main__':
    try:
        install()
    except (OSError, ValueError, ET.ParseError, AssertionError) as error:
        sys.exit(f'Demo installation failed: {error}')
