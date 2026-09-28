#!/usr/bin/env python3
"""Launch jpy_election onto the interactive Windows desktop."""

import os
import subprocess
import sys

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    exe = os.path.join(root, "bazel-bin", "jpy_election.exe")

    args = sys.argv[1:]
    cmdline = f'"{exe}"'
    if args:
        cmdline += " " + " ".join(args)

    # Use WMI Win32_Process to create the process directly in the interactive desktop session
    ps_cmd = (
        f'$res = Invoke-CimMethod -ClassName Win32_Process -MethodName Create '
        f'-Arguments @{{CommandLine = \'{cmdline}\'; CurrentDirectory = \'{root}\'}}; '
        f'if ($res.ReturnValue -ne 0) {{ exit $res.ReturnValue }} else {{ Write-Output $res.ProcessId }}'
    )

    try:
        proc = subprocess.run(
            ["powershell.exe", "-NoProfile", "-Command", ps_cmd],
            capture_output=True,
            text=True,
            check=True,
        )
        pid = proc.stdout.strip()
        print(f"jpy_election started successfully on screen (PID: {pid}).")
    except subprocess.CalledProcessError as e:
        print(f"Failed to launch on desktop: {e.stderr}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
