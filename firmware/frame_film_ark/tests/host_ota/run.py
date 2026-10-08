"""Compile the actual staged OTA functions with small host API stubs."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default=shutil.which("clang") or shutil.which("cc"))
    args = parser.parse_args()
    if not args.cc:
        parser.error("clang or cc is required; pass --cc PATH")
    here = Path(__file__).resolve().parent
    source = here.parents[1] / "components/film_service/src/service_ota.c"
    # Extract actual production code, not a copied implementation. Fail if its
    # boundary changes so tests cannot silently run a stale duplicate.
    marker = "/* Staged WiFi OTA. Boot rollback is not enabled. */"
    actual = source.read_text(encoding="utf-8").split(marker, 1)[1]
    with tempfile.TemporaryDirectory(prefix="ark-ota-host-") as temp:
        temp = Path(temp)
        (temp / "actual_ota.inc").write_text(actual, encoding="utf-8")
        executable = temp / "host_ota.exe"
        subprocess.run([args.cc, "-Wall", "-Wextra", "-Werror", "-I", str(temp),
                        str(here / "test_ota.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
