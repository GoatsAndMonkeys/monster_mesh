import subprocess
Import("env")

try:
    branch = subprocess.check_output(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"],
        cwd=env["PROJECT_DIR"]
    ).decode().strip()
except Exception:
    branch = "unknown"

env.Append(CPPDEFINES=[("MONSTERMESH_VERSION", env.StringifyMacro(branch))])
print(f"[MonsterMesh] version: {branch}")
