"""Small shared helpers for executable image tests; no build orchestration."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
TOOL = os.environ.get('NEXTUFS', str(ROOT / 'nextufs'))


def run(*args, status=0, timeout=120, **kwargs):
    command = [os.fsdecode(a) for a in args]
    print('+', ' '.join(command), flush=True)
    result = subprocess.run(command, capture_output=True, timeout=timeout, **kwargs)
    if b"ERROR: AddressSanitizer" in result.stderr or b"ERROR: LeakSanitizer" in result.stderr or b"runtime error:" in result.stderr:
        raise AssertionError(f"sanitizer finding in {command}: {result.stderr!r}")
    if result.returncode != status:
        raise AssertionError(f'{command}: expected {status}, got {result.returncode}\n'
                             f'{result.stdout!r}\n{result.stderr!r}')
    return result.stdout


def tool(*args, **kwargs):
    return run(TOOL, *args, **kwargs)


def image(path, size='64M', raw=False):
    tool('mkimg', *(['--raw'] if raw else []), path, size)
    return path


def check(image):
    tool('fsck', '-n', image)


def read(image, path):
    return tool('browse', '--raw', image, path)


def node(image, path):
    return json.loads(tool('browse', '--json', image, path))[0]


def api_fixture(path):
    image(path)
    for directory in ['/private', '/private/tmp', '/private/etc', '/usr']:
        tool('mkfile', '--mkdir', path, directory)
    tool('mkfile', '--symlink', path, 'private/etc', '/etc')
    tool('mkfile', path, '/etc/passwd', '#\n# You probably want to keep this synthetic test account.\n')
    payload = path.parent / 'kernel-payload'
    payload.write_bytes(bytes(range(256)) * 1024)
    tool('mkfile', '--from-file', path, '/mach_kernel', payload)
    return path


def interrupted(signum, frame):
    raise KeyboardInterrupt


@contextmanager
def mounted(image, work, options="rw"):
    """Own one fresh mountpoint; wait for readiness and always reap our process."""
    mount = work / 'mount'
    mount.mkdir()
    previous = signal.signal(signal.SIGTERM, interrupted)
    process = None
    try:
        with (work / 'mount.log').open('wb') as log:
            process = subprocess.Popen([TOOL, 'mount', str(image), str(mount),
                                        '-o', options, '-f'], stdout=log, stderr=log)
            deadline = time.monotonic() + 10
            while not os.path.ismount(mount):
                if process.poll() is not None:
                    raise AssertionError(f'mount exited {process.returncode}; see {work / "mount.log"}')
                if time.monotonic() >= deadline:
                    raise TimeoutError('FUSE mount did not become ready')
                time.sleep(0.05)
            try:
                yield mount
            finally:
                run('fusermount3', '-u', mount)
            process.wait(timeout=10)
            if process.returncode != 0:
                raise AssertionError(f'mount exited {process.returncode}')
    finally:
        if process is not None:
            subprocess.run(['fusermount3', '-uz', str(mount)], capture_output=True, timeout=10)
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        signal.signal(signal.SIGTERM, previous)


def vdi(source, target, parent=None, block=1024 * 1024):
    """Generate a standard dynamic VDI or an empty differencing child."""
    import struct
    size = source.stat().st_size
    count = (size + block - 1) // block
    data_offset = ((512 + count * 4 + 4095) // 4096) * 4096
    head = bytearray(data_offset)
    for off, value in [(0x40, 0xbeda107f), (0x44, 0x10001), (0x48, 0x190),
                       (0x4c, 4 if parent else 1), (0x154, 512), (0x158, data_offset),
                       (0x168, 512), (0x178, block), (0x180, count),
                       (0x184, 0 if parent else count)]:
        struct.pack_into('<I', head, off, value)
    struct.pack_into('<Q', head, 0x170, size)
    head[0x188:0x198] = os.urandom(16)
    if parent:
        with parent.open('rb') as stream:
            stream.seek(0x188)
            head[0x1a8:0x1b8] = stream.read(16)
    for i in range(count):
        struct.pack_into('<I', head, 512+i*4, 0xffffffff if parent else i)
    with target.open('wb') as stream:
        stream.write(head)
        if not parent:
            stream.write(source.read_bytes())
            stream.truncate(data_offset + count * block)
    return target
