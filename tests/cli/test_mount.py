"""Help is available in both builds; the offline mount stub has a precise status."""
import subprocess
from support import TOOL, tool

tool('mount', '--help')
result = subprocess.run([TOOL, 'mount'], capture_output=True)
if b'mount support is unavailable' in result.stderr:
    assert result.returncode == 2
    result = subprocess.run([TOOL, 'mount', 'ignored-image', 'ignored-mountpoint'], capture_output=True)
    assert result.returncode == 2 and b'mount support is unavailable' in result.stderr
else:
    assert result.returncode == 1 and b'usage:' in result.stderr
