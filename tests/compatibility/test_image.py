"""Read-only validation of a supplied OPENSTEP disk image."""
import os
from support import check, run, tool

image = os.environ['TEST_IMAGE']
for command in ['info', 'browse']:
    tool(command, image)
tool('info', '--json', image)
check(image)
run(os.environ['NEXTUFS_TEST'], image)
