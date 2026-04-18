"""
PlatformIO extra_scripts pre-script.

The pico-sdk PlatformIO builder (picosdk.py) can only find board headers that
live inside the SDK's own boards directory. This script runs after that builder
has generated config_autogen.h and overwrites it with the content of our
project-local board header, spliced together with the SDK's rename_exceptions.h
exactly as picosdk.py would have done.
"""
import os
from pathlib import Path
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()
FRAMEWORK_DIR = platform.get_package_dir("framework-picosdk")

def overwrite_config_autogen(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$PROJECT_BUILD_DIR")
    pio_env = env.subst("$PIOENV")
    
    board_header = os.path.join(project_dir, "boards", "waveshare_rp2350b_core.h")
    rename_exceptions = os.path.join(
        FRAMEWORK_DIR, "src", "rp2_common", "cmsis", "include", "cmsis", "rename_exceptions.h"
    )
    config_autogen = os.path.join(build_dir, pio_env, "generated", "pico", "config_autogen.h")

    content = Path(board_header).read_text() + Path(rename_exceptions).read_text()
    os.makedirs(os.path.dirname(config_autogen), exist_ok=True)
    Path(config_autogen).write_text(content)
    print("inject_board_header: wrote %s" % config_autogen)

# Register as a post-action on the entire build so it fires after picosdk.py's
# gen_config_autogen but before any source files are compiled.
env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", overwrite_config_autogen)

# Also run it immediately at script-load time, since config_autogen.h is
# consumed during dependency scanning before the elf target is built.
overwrite_config_autogen(None, None, env)
