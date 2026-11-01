"""Create local pairing files; no credentials are committed or sent to GitHub."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import ssl
import subprocess

def openssl_path():
    result = shutil.which("openssl")
    if not result:
        candidate = Path("C:/Program Files/Git/usr/bin/openssl.exe")
        if candidate.exists(): result = str(candidate)
    if not result: raise RuntimeError("OpenSSL is required to create a local TLS identity")
    return result

def provision(host, directory, port=7443):
    directory = Path(directory)
    if directory.exists(): raise ValueError("choose a new output directory; existing credentials are never overwritten")
    directory.mkdir(parents=True, mode=0o700)
    key, cert, p12 = (directory / n for n in ("server.key", "server.crt", "server.p12"))
    password, token = secrets.token_hex(32), secrets.token_hex(32)
    exe = openssl_path()
    subprocess.run([exe, "req", "-x509", "-newkey", "rsa:3072", "-sha256", "-nodes", "-days", "365",
                    "-subj", "/CN=iOSRemoteBridge local pairing", "-keyout", str(key), "-out", str(cert)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, IRB_P12_PASSWORD=password)
    subprocess.run([exe, "pkcs12", "-export", "-inkey", str(key), "-in", str(cert), "-out", str(p12),
                    "-keypbe", "PBE-SHA1-3DES", "-certpbe", "PBE-SHA1-3DES", "-macalg", "sha1",
                    "-passout", "env:IRB_P12_PASSWORD"], env=env, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).hexdigest()
    (directory / "device-config.json").write_text(json.dumps({"port": port, "token": token, "p12_password": password}, indent=2))
    (directory / "client-config.json").write_text(json.dumps({"host": host, "port": port, "token": token, "certificate_sha256": pin}, indent=2))
    for path in directory.iterdir(): path.chmod(0o600)
    return pin

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="iPhone Wi-Fi IPv4 address")
    parser.add_argument("--output", default="credentials/pairing")
    parser.add_argument("--port", type=int, default=7443)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535: parser.error("port must be 1024..65535")
    pin = provision(args.host, args.output, args.port)
    print(f"Created local pairing files in {args.output}; certificate SHA256: {pin}")
    print("Transfer device-config.json and server.p12 using SSH; set mobile ownership and modes 0700/0600. See docs/DEVICE_TESTING.md.")

if __name__ == "__main__": main()
