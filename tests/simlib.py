"""Helpers shared by the tests: build a firmware, run it in simavr, parse the event log."""
import os, subprocess, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIMTEST = os.path.join(ROOT, "tools", "bin", "simtest.exe")
CPU_HZ = 16_000_000
RECORD_FIELDS = {"U": 3, "P": 4, "V": 5, "S": 3, "M": 3, "A": 3, "E": 5}   # see the list at the top of tools/simtest.c


def _env():
    env = dict(os.environ)
    # make sure avr-gcc / make (winget install) and the MSYS2 DLLs can be found
    extra = glob.glob(os.path.expandvars(
        r"%LOCALAPPDATA%\Microsoft\WinGet\Packages\ZakKemble.avr-gcc*\avr-gcc-*\bin"))
    extra += [r"C:\msys64\ucrt64\bin"]
    env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    return env


def build(name="main", extra_cflags=""):
    """Build build/<name>.elf (main = src/main.c, anything else = tests/fw/<name>.c)."""
    elf = os.path.join(ROOT, "build", name + ".elf")
    if os.path.exists(elf):
        os.remove(elf)                      # force a rebuild so flags always apply
    for o in glob.glob(os.path.join(ROOT, "build", "*.o")):
        os.remove(o)
    cmd = ["make", f"build/{name}.elf", f"EXTRA_CFLAGS={extra_cflags}"]
    r = subprocess.run(cmd, cwd=ROOT, env=_env(), capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("build failed:\n" + r.stdout + r.stderr)
    return elf


def run(elf, ms=100, args=()):
    """Run the firmware in the simulator; return parsed events."""
    cmd = [SIMTEST, elf, "-ms", str(ms), *args]
    r = subprocess.run(cmd, env=_env(), capture_output=True, text=True, timeout=600)
    if r.returncode != 0:
        raise RuntimeError("simtest failed:\n" + r.stdout + r.stderr)
    ev = {"U": [], "P": [], "V": [], "S": {}, "M": {}, "A": {}, "E": None}
    for line in r.stdout.splitlines():
        p = line.split()
        # simtest switches off simavr's echo of UART text, but to be safe skip anything that is
        # not one of simtest's records (a record letter + its exact number of fields)
        if not p or len(p) != RECORD_FIELDS.get(p[0]):
            continue
        if p[0] == "U":
            ev["U"].append((int(p[1]), int(p[2])))
        elif p[0] == "P":
            ev["P"].append((int(p[1]), p[2], int(p[3])))
        elif p[0] == "V":
            ev["V"].append(tuple(int(x) for x in p[1:5]))
        elif p[0] == "S":
            ev["S"][p[1]] = bytes.fromhex(p[2]) if p[2] != "MISSING" else None
        elif p[0] == "M":
            ev["M"][int(p[1], 16)] = bytes.fromhex(p[2])
        elif p[0] == "A":
            ev["A"][p[1]] = None if p[2] == "MISSING" else int(p[2], 16)
        elif p[0] == "E":
            ev["E"] = dict(cycle=int(p[1]), pc=int(p[2]), sp=int(p[3]), state=int(p[4]))
    return ev


class Report:
    """Collects PASS/FAIL lines and prints real numbers."""
    def __init__(self, title):
        self.title, self.fails = title, 0
        print(f"\n=== {title} ===")

    def check(self, ok, msg):
        print(("  PASS  " if ok else "  FAIL  ") + msg)
        if not ok:
            self.fails += 1
        return ok

    def info(self, msg):
        print("  info  " + msg)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)
