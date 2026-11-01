"""Generate a decoder-only vcpkg overlay without modifying its checkout."""
from pathlib import Path
import shutil
import sys

def prepare(vcpkg, destination):
    source = Path(vcpkg) / "ports" / "ffmpeg"
    target = Path(destination) / "ffmpeg"
    shutil.copytree(source, target)
    portfile = target / "portfile.cmake"
    text = portfile.read_text(encoding="utf-8")
    marker = 'message(STATUS "Building Options: ${OPTIONS}")'
    if text.count(marker) != 1: raise ValueError("pinned FFmpeg port changed; review overlay insertion point")
    options = (' --disable-encoders --disable-decoders --disable-parsers --disable-hwaccels'
               ' --disable-bsfs --enable-decoder=h264 --enable-parser=h264'
               ' --enable-hwaccel=h264_d3d11va --enable-hwaccel=h264_d3d11va2')
    text = text.replace(marker, '# iOSRemoteBridge: only the H.264 receiver path is required.\n'
                        f'string(APPEND OPTIONS "{options}")\n{marker}')
    portfile.write_text(text, encoding="utf-8")

if __name__ == "__main__":
    prepare(sys.argv[1], sys.argv[2])
