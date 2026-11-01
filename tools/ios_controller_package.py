"""Package only the built app; never copy local pairing files into an IPA."""
import hashlib,os,shutil,zipfile
from pathlib import Path
root=Path(__file__).resolve().parent.parent
folder=root/'dist/ios-controller'
payload=folder/'Payload'
app=payload/'iOSRemoteBridge.app'
assert app.is_dir()
for path in payload.rglob('*'):
    assert not path.is_symlink()
    assert path.name not in {'client-config.json','device-config.json','server.p12'}
    assert path.suffix.lower() not in {'.key','.pem','.p12','.pfx'}
archive=folder/'iOSRemoteBridge-Controller-unsigned.ipa'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as output:
    for path in sorted(payload.rglob('*')):
        if path.is_file(): output.write(path,path.relative_to(folder))
with zipfile.ZipFile(archive) as output:
    assert output.testzip() is None
    assert 'Payload/iOSRemoteBridge.app/Info.plist' in output.namelist()
    assert 'Payload/iOSRemoteBridge.app/iOSRemoteBridge' in output.namelist()
shutil.copyfile(root/'docs/IOS_CLIENT.md',folder/'INSTALL.md')
(folder/'BUILD.txt').write_text('Objective-C + UIKit iOS controller\nSource: '+os.environ.get('GITHUB_SHA','local')+'\nCI: https://github.com/Justintunsday/iOSRemoteBridge/actions/runs/'+os.environ.get('GITHUB_RUN_ID','local')+'\nRequires re-signing through AltStore, Sideloadly or Xcode before ordinary device installation.\n')
with archive.open('rb') as stream: checksum=hashlib.file_digest(stream,'sha256').hexdigest()
(folder/'SHA256SUMS.txt').write_text(checksum+'  '+archive.name+'\n')
print(f'IPA packaged: {archive.stat().st_size:,} bytes; archive structure and credential exclusion checks passed')
