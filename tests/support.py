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
def mounted(image, work):
    """Own one fresh mountpoint; wait for readiness and always reap our process."""
    mount = work / 'mount'
    mount.mkdir()
    previous = signal.signal(signal.SIGTERM, interrupted)
    process = None
    try:
        with (work / 'mount.log').open('wb') as log:
            process = subprocess.Popen([TOOL, 'mount', str(image), str(mount),
                                        '-o', 'rw', '-f', '-s'], stdout=log, stderr=log)
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
        if os.path.ismount(mount):
            subprocess.run(['fusermount3', '-u', str(mount)], capture_output=True, timeout=10)
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        signal.signal(signal.SIGTERM, previous)
