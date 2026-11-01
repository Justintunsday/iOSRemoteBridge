"""Generate small, tagged H.264 grayscale fixtures with a known luma ramp."""
from pathlib import Path
import subprocess
import sys

def main():
    executable, destination = sys.argv[1:]
    folder = Path(destination); folder.mkdir(parents=True, exist_ok=True)
    levels = (0, 8, 16, 32, 64, 128, 192, 224, 235, 240, 248, 255)
    row = b"".join(bytes([level]) * 16 for level in levels)
    raw = folder / "gray-ramp.yuv"
    raw.write_bytes(row * 64 + bytes([128]) * (192 * 64 // 2))
    for name, color_range, pixel_format in (("full", "pc", "yuvj420p"), ("limited", "tv", "yuv420p")):
        subprocess.run([executable, "-hide_banner", "-loglevel", "error", "-y",
                        "-f", "rawvideo", "-pixel_format", "yuv420p", "-video_size", "192x64",
                        "-color_range", color_range, "-i", str(raw), "-frames:v", "1",
                        "-c:v", "libx264", "-qp", "1", "-profile:v", "main", "-tune", "zerolatency",
                        "-pix_fmt", pixel_format, "-color_range", color_range,
                        "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709",
                        "-f", "h264", str(folder / f"gray-{name}.h264")], check=True)
    print("Generated full/limited-range H.264 ramps with near-black and near-white levels.")

if __name__ == "__main__": main()
