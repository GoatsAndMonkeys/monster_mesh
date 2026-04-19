import subprocess
Import("env")

try:
    branch = subprocess.check_output(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"],
        cwd=env["PROJECT_DIR"]
    ).decode().strip()
    build = subprocess.check_output(
        ["git", "rev-list", "--count", "HEAD"],
        cwd=env["PROJECT_DIR"]
    ).decode().strip()
    version = f"{branch}-b{build}"
except Exception:
    version = "unknown"

env.Append(CPPDEFINES=[("MONSTERMESH_VERSION", env.StringifyMacro(version))])
print(f"[MonsterMesh] version: {version}")
