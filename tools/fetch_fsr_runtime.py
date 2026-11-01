"""Fetch only the two unmodified, signed AMD FSR SDK 2.4 DX12 runtime DLLs."""
from pathlib import Path
import hashlib
import sys
import urllib.request

REVISION = '7539ae8b6568ad29ed78a753404351daf3b5cca7'
FILES = {
    'amd_fidelityfx_loader_dx12.dll': 'e2191d3c0828be45eb8affd4e3ec0378f2293adc345b772a7170edcf3b021ecb',
    'amd_fidelityfx_upscaler_dx12.dll': 'aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace',
}
def main():
    directory = Path(sys.argv[1]); directory.mkdir(parents=True, exist_ok=True)
    for name, expected in FILES.items():
        path = directory / name
        if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            url = f'https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/{REVISION}/Kits/FidelityFX/signedbin/{name}'
            with urllib.request.urlopen(url, timeout=120) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError(f'AMD runtime hash mismatch: {name}')
            path.write_bytes(data)
        print(f'{name}: SHA-256 verified')
if __name__ == '__main__': main()
