"""Exercise the production staging rule with a tiny executable, without Qt."""
import argparse
import os
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("work", type=pathlib.Path)
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--compiler")
    parser.add_argument("--make-program")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    source = args.work.resolve() / "source with spaces"
    build = args.work.resolve() / "build with spaces"
    source.mkdir(parents=True, exist_ok=True)
    (args.work / ".parked").touch()
    (source / "samples/nested").mkdir(parents=True, exist_ok=True)
    sample = source / "samples/nested/example.txt"
    sample.write_text("first\n")
    (source / "main.cpp").write_text("int main() { return 0; }\n")
    cmake = (root / "CMakeLists.txt").read_text()
    staging = cmake.split("# Bundled sample files,")[1].split("deploy_runtime_deps(")[0]
    (source / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.21)\nproject(staging LANGUAGES CXX)\n'
        'add_executable(automotive-format-explorer main.cpp)\n'
        'set_target_properties(automotive-format-explorer PROPERTIES '
        'RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/output with spaces/$<CONFIG>")\n'
        + "# Bundled sample files," + staging)
    configure = [args.cmake, "-S", str(source), "-B", str(build), "-G", args.generator]
    if args.compiler:
        configure.append("-DCMAKE_CXX_COMPILER=" + args.compiler)
        # GCC's child processes also need its runtime ahead of any Qt runtime.
        os.environ["PATH"] = str(pathlib.Path(args.compiler).parent) + os.pathsep + os.environ["PATH"]
    if args.make_program:
        configure.append("-DCMAKE_MAKE_PROGRAM=" + args.make_program)
    subprocess.run(configure, check=True)
    command = [args.cmake, "--build", str(build), "--config", "Debug", "--target", "automotive-format-explorer"]
    subprocess.run(command, check=True)
    executable = next(p for p in build.glob("output with spaces/**/*")
                      if p.is_file() and p.stem == "automotive-format-explorer")
    staged = executable.parent / "samples/nested/example.txt"
    before = executable.stat().st_mtime_ns
    assert staged.read_text() == "first\n"
    sample.write_text("changed\n")
    subprocess.run(command, check=True)
    assert staged.read_text() == "changed\n", "sample edit was not staged"
    assert executable.stat().st_mtime_ns == before, "sample edit relinked executable"
    staged_time = staged.stat().st_mtime_ns
    subprocess.run(command, check=True)
    assert staged.stat().st_mtime_ns == staged_time, "unchanged build rewrote sample"
    assert executable.stat().st_mtime_ns == before
    print("sample edit staged; executable unchanged; no-change build stable")


if __name__ == "__main__":
    main()
