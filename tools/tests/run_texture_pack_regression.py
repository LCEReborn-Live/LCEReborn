"""Native Windows integration check using an existing Ninja/clang-cl game build.

Usage: python tools/tests/run_texture_pack_regression.py --content-dir <prepared assets>
Build LCEReborn first. The runner uses isolated saves and logs under build/;
it does not change the game executable or the player's profiles.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--content-dir", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=root / "build/game-ninja")
    parser.add_argument("--resource-dir", type=Path, default=root / "resources/pc")
    parser.add_argument("--packs-dir", type=Path, default=root / "packs")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this runner requires a native Windows Ninja/clang-cl build")
    build = args.build_dir.resolve()
    out = build.parent / "texture-pack-regression"
    out.mkdir(exist_ok=True)
    ninja = (build / "build.ninja").read_text()
    cache = (build / "CMakeCache.txt").read_text()
    compiler = Path(re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.+)$", cache, re.M).group(1))

    def run(command, log):
        result = subprocess.run(command, cwd=build, capture_output=True, text=True)
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            print((result.stdout + result.stderr)[-5000:])
            raise RuntimeError("command failed; see " + str(log))

    # Reuse the real entry point and its compiler flags so startup and the
    # renderer are initialised exactly as in the game, then run the assertions.
    key = r"CMakeFiles\LCEReborn.dir\source\platform\pc\Main.cpp.obj"
    block = re.search(r"build " + re.escape(key) + r": [^\n]+\n(?:  [^\n]*\n)*", ninja).group(0)
    flags = dict(re.findall(r"^  (\w+) = (.*)$", block, re.M))
    source = (root / "source/platform/pc/Main.cpp").read_text()
    include = (root / "tools/tests/texture_pack_runtime_check.hpp").as_posix()
    source = source.replace("static const Logger sLog", '#include "' + include + '"\n\nstatic const Logger sLog', 1)
    source = source.replace("    Minecraft::main();",
                            "    Minecraft::main();\n    gApp.InitGameSettings();\n    return RunTexturePackRegression();", 1)
    cpp, obj, rsp = (out / name for name in ("main.cpp", "main.obj", "compile.rsp"))
    cpp.write_text(source, encoding="utf-8")
    rsp.write_text("/nologo -TP " + " ".join(flags[name] for name in ("DEFINES", "INCLUDES", "FLAGS"))
                   + f' /Fo"{obj}" -c -- "{cpp}"', encoding="utf-8")
    run([str(compiler), "@" + str(rsp)], out / "compile.log")

    block = re.search(r"build LCEReborn.exe: [^\n]+\n(?:  [^\n]*\n)*", ninja).group(0)
    objects = block.splitlines()[0].split("Release ", 1)[1].split(" |", 1)[0]
    libraries = re.search(r"^  LINK_LIBRARIES = (.+)$", block, re.M).group(1)
    link_flags = re.search(r"^  LINK_FLAGS = (.+)$", block, re.M).group(1)
    tools = next(parent for parent in compiler.parents if parent.name == "Tools")
    msvc = sorted((tools / "MSVC").iterdir())[-1] / "lib/x64"
    sdk_root = Path(os.environ.get("WindowsSdkDir", str(Path(os.environ["ProgramFiles(x86)"]) / "Windows Kits/10")))
    sdk = sorted((sdk_root / "Lib").iterdir())[-1]
    response = objects.replace(key, '"' + str(obj) + '"') + " " + libraries + " " + link_flags
    response += " " + " ".join('/libpath:"' + str(folder) + '"' for folder in (msvc, sdk / "ucrt/x64", sdk / "um/x64"))
    exe = out / "TexturePackRegression.exe"
    rsp = out / "link.rsp"
    rsp.write_text(response + f' /DEBUG /out:"{exe}" /pdb:"{exe.with_suffix(".pdb")}"', encoding="utf-8")
    run([str(compiler.parent / "lld-link.exe"), "@" + str(rsp)], out / "link.log")

    runtime = out / "runtime"
    runtime.mkdir(exist_ok=True)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    result = subprocess.run(
        [str(exe), "--username", "TextureRegression", "--content-dir", str(args.content_dir.resolve()),
         "--resource-dir", str(args.resource_dir.resolve()), "--packs-dir", str(args.packs_dir.resolve()),
         "--save-dir", str(runtime / "saves"), "--log-dir", str(runtime / "logs")],
        cwd=runtime, capture_output=True, text=True, timeout=120, startupinfo=startup)
    (out / "console.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
    print(result.stdout[-4000:] + result.stderr[-1000:])
    # FatalLoadError currently exits with status 0, so require the final
    # assertion marker as well as a successful process exit.
    completed = "PASS: Old Textures / 1.14 switching and original cave spider pixels" in result.stdout
    return 0 if result.returncode == 0 and completed and "/FATAL]" not in result.stdout else 1


if __name__ == "__main__":
    sys.exit(main())
