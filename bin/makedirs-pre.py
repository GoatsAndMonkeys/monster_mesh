#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
#
# Pre-script: runs BEFORE the platform BUILD_SCRIPT.
#
# 1. Pre-creates all variant-dir output directories so they exist early.
# 2. Monkey-patches SCons File.prepare() so output dirs are re-created
#    immediately before each compile step, in case something removes them.
#
import hashlib
import os

Import("env")

_SOURCE_EXTS = ('.c', '.cpp', '.cc', '.cxx', '.S', '.s')


def _precreate_build_dirs(env):
    project_dir = env.subst("$PROJECT_DIR")
    pioenv      = env.get("PIOENV")
    build_dir   = env.subst("$BUILD_DIR")           # use PIO's actual build dir
    libdeps_dir = os.path.join(project_dir, ".pio", "libdeps", pioenv)
    src_dir     = os.path.join(project_dir, "src")

    created = 0

    # --- library output dirs ---
    if os.path.isdir(libdeps_dir):
        for lib_name in os.listdir(libdeps_dir):
            lib_path = os.path.join(libdeps_dir, lib_name)
            if not os.path.isdir(lib_path):
                continue
            lib_hash = hashlib.sha1(lib_path.encode()).hexdigest()[:3]
            lib_build_root = os.path.join(build_dir, "lib" + lib_hash, lib_name)
            src_subdir = os.path.join(lib_path, "src")
            walk_root = src_subdir if os.path.isdir(src_subdir) else lib_path
            for root, dirs, files in os.walk(walk_root):
                if any(f.endswith(_SOURCE_EXTS) for f in files):
                    rel = os.path.relpath(root, walk_root)
                    out = lib_build_root if rel == "." else os.path.join(lib_build_root, rel)
                    if not os.path.isdir(out):
                        os.makedirs(out, exist_ok=True)
                        created += 1

    # --- project source output dirs ---
    if os.path.isdir(src_dir):
        for root, dirs, files in os.walk(src_dir):
            if any(f.endswith(_SOURCE_EXTS) for f in files):
                rel = os.path.relpath(root, src_dir)
                out = os.path.join(build_dir, "src") if rel == "." else os.path.join(build_dir, "src", rel)
                if not os.path.isdir(out):
                    os.makedirs(out, exist_ok=True)
                    created += 1

    print(f"[makedirs-pre] Created {created} output directories in {build_dir}")


def _patch_scons_prepare():
    """
    Monkey-patch SCons.Node.FS.File.prepare so that the output directory is
    (re-)created immediately before every compile step.
    """
    try:
        import SCons.Node.FS as _FS
        _orig = _FS.File.prepare

        def _patched(self):
            try:
                dir_abs = self.dir.get_abspath()
                if not os.path.isdir(dir_abs):
                    os.makedirs(dir_abs, exist_ok=True)
            except Exception:
                pass
            return _orig(self)

        _FS.File.prepare = _patched
    except Exception as exc:
        print(f"[makedirs-pre] WARNING: could not patch File.prepare: {exc}")


_precreate_build_dirs(env)
_patch_scons_prepare()
