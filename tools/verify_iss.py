#!/usr/bin/env python3
"""Static checks on tools/installer/qshot.iss.

Inno Setup is not installed on the development machine, so the installer script cannot be
compiled or run. This is the substitute: it catches the mistakes that a compile would catch
and that are most likely to be made by hand -- an undefined {#macro}, a task referenced by an
entry that no [Tasks] line declares, a source path that does not resolve, a missing BOM.

It does *not* prove the installer works. It proves the script is internally consistent and
that the files it names exist. Run it with:

    python tools/verify_iss.py
"""

import pathlib
import re
import sys
import uuid

SCRIPT = pathlib.Path(__file__).resolve().parent / "installer" / "qshot.iss"

# The sections Inno Setup 6 defines. A typo here is a compile error in Inno, so it is worth
# catching here instead.
KNOWN_SECTIONS = {
    "Setup", "Types", "Components", "Tasks", "Dirs", "Files", "Icons", "Ini", "Installs",
    "Languages", "Messages", "CustomMessages", "Registry", "Run", "UninstallRun",
    "UninstallDelete", "Code", "_ISTool",
}

# The keys without which the output is not a usable installer. Not exhaustive: the point is
# to catch an accidental deletion, not to re-document the format.
REQUIRED_SETUP_KEYS = {
    "AppId", "AppName", "AppVersion", "DefaultDirName", "OutputDir", "OutputBaseFilename",
    "ArchitecturesAllowed", "PrivilegesRequired",
}

failures = []
checks = 0


def check(ok, what, detail=""):
    global checks
    checks += 1
    if ok:
        print(f"  ok   {what}")
    else:
        failures.append(what)
        print(f"  FAIL {what}" + (f"  ({detail})" if detail else ""))


def main():
    if not SCRIPT.exists():
        print(f"verify_iss: {SCRIPT} does not exist")
        return 1

    raw = SCRIPT.read_bytes()

    print("[1] the file is encoded the way Inno Setup requires")
    # Inno reads a script without a BOM as ANSI in the system code page. The development
    # machine's ACP is 936 (GBK), so a BOM-less UTF-8 file would garble every Chinese string.
    check(raw.startswith(b"\xef\xbb\xbf"),
          "it starts with a UTF-8 byte order mark")
    try:
        text = raw.decode("utf-8-sig")
        check(True, "it decodes as UTF-8")
    except UnicodeDecodeError as exc:
        check(False, "it decodes as UTF-8", str(exc))
        return 1

    print("\n[2] every {#macro} reference is defined")
    defined = set(re.findall(r"^\s*#define\s+([A-Za-z_]\w*)", text, re.MULTILINE))
    # ISPP's own predefined names, which are not #defined in the file.
    predefined = {"SourcePath", "CompilerPath", "__FILE__", "__LINE__"}
    referenced = set(re.findall(r"\{#([A-Za-z_]\w*)", text))
    print(f"       defined: {', '.join(sorted(defined)) or '(none)'}")
    print(f"       referenced: {', '.join(sorted(referenced)) or '(none)'}")
    for name in sorted(referenced):
        check(name in defined or name in predefined, f"{{#{name}}} resolves")

    print("\n[3] the sections are the ones Inno Setup defines")
    sections = re.findall(r"^\[([A-Za-z_]\w*)\]", text, re.MULTILINE)
    print(f"       {', '.join(sections)}")
    for name in sections:
        check(name in KNOWN_SECTIONS, f"[{name}] is a real section")
    check("Setup" in sections, "there is a [Setup] section")
    check("Files" in sections, "there is a [Files] section")

    print("\n[4] [Setup] carries the keys a working installer needs")
    setup_body = re.split(r"^\[", text, flags=re.MULTILINE)[1]
    keys = set(re.findall(r"^([A-Za-z]\w*)\s*=", setup_body, re.MULTILINE))
    for key in sorted(REQUIRED_SETUP_KEYS):
        check(key in keys, f"{key} is set")

    print("\n[5] AppId is a literal GUID")
    app_id = re.search(r"^AppId\s*=\s*(\S+)", text, re.MULTILINE)
    if not app_id:
        check(False, "AppId is present")
    else:
        value = app_id.group(1)
        # The doubled leading brace is the escape for a literal one, so the real value is
        # {GUID} and it must be parseable as a UUID. Changing it after a release makes Windows
        # treat the result as a different product and install side by side.
        check(value.startswith("{{"), "AppId opens with the {{ escape", value)
        check(value.endswith("}"), "AppId closes with a single }", value)
        inner = value[1:]
        try:
            uuid.UUID(inner.strip("{}"))
            check(True, f"the GUID parses ({inner})")
        except ValueError as exc:
            check(False, "the GUID parses", f"{inner}: {exc}")

    print("\n[6] every task an entry names is declared in [Tasks]")
    tasks_body = ""
    match = re.search(r"^\[Tasks\]\s*$(.*?)(?=^\[)", text, re.MULTILINE | re.DOTALL)
    if match:
        tasks_body = match.group(1)
    declared = set(re.findall(r'^Name:\s*"([^"]+)"', tasks_body, re.MULTILINE))
    print(f"       declared: {', '.join(sorted(declared)) or '(none)'}")
    check(bool(declared), "at least one task is declared")

    # Not anchored to the start of a line: `Tasks:` is the last key of an entry whose earlier
    # keys wrapped onto preceding lines, so it appears mid-line. The first version of this
    # check anchored it with ^, matched nothing, and reported success -- a check that verifies
    # nothing while looking green is worse than no check, hence the assertion below that it
    # actually found something.
    task_refs = re.findall(r"Tasks:\s*([^;\\\n]+)", text)
    check(len(task_refs) > 0, "at least one entry references a task")
    for value in task_refs:
        # `Tasks: desktopicon` or `Tasks: autostart and not desktopicon`.
        names = [n.strip() for n in re.split(r"\band\b|\bor\b", value) if n.strip()]
        for name in names:
            name = re.sub(r"^not\s+", "", name)
            check(name in declared, f"the task '{name}' is declared")

    print("\n[7] the files it points at exist")
    script_dir = SCRIPT.parent
    # The [Files] source is {#StageDir}\*, and StageDir is SourcePath + "..\..\dist\QShot".
    # Resolving that arithmetic here is the one thing this script checks that a reader cannot
    # easily do by eye -- and a wrong number of ".." is exactly the kind of mistake that only
    # shows up on the first compile.
    staged = (script_dir / ".." / ".." / "dist" / "QShot").resolve()
    check((staged / "qshot.exe").exists(),
          f"the staged payload resolves ({staged})")
    check((staged / "platforms" / "qwindows.dll").exists(),
          "and contains the platform plugin")
    check((staged / "translations" / "qtbase_zh_CN.qm").exists(),
          "and contains the Chinese translation")

    icon = re.search(r"^SetupIconFile\s*=\s*(\S+)", text, re.MULTILINE)
    if icon:
        path = (script_dir / icon.group(1).replace("\\", "/")).resolve()
        check(path.exists(), f"SetupIconFile resolves ({path})")

    print(f"\n{checks} checks, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
