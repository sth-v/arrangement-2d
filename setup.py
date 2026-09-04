"""Build script for arrangement_2d (Cython + CGAL 6.x header-only core).

Environment variables (all optional):
  CGAL_INCLUDE_DIR     directory containing CGAL/version.h
  GMP_INCLUDE_DIR / GMP_LIB_DIR, MPFR_INCLUDE_DIR / MPFR_LIB_DIR, BOOST_INCLUDE_DIR
  ARR2D_PREFIX         a prefix (e.g. /opt/homebrew, $CONDA_PREFIX) whose include/ and lib/ are used
  ARR2D_OPT            optimisation flag (default -O2)
  ARR2D_DEBUG=1        build with -O0 -g
  ARR2D_NDEBUG=1       disable CGAL assertions/preconditions (faster, but CGAL misuse may crash instead of raising)
  ARR2D_JOBS           parallel compile jobs (default: cpu count)
  ARR2D_PROFILE=1      enable Cython line tracing / profiling
"""
from __future__ import annotations

import glob
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext as _build_ext

HERE = Path(__file__).resolve().parent
SRC = HERE / "src" / "arr2d"
INCLUDE = SRC / "include"


def _first_existing(paths, rel):
    for p in paths:
        if p and (Path(p) / rel).exists():
            return str(Path(p))
    return None


def _run(cmd):
    try:
        return subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True).strip()
    except Exception:
        return None


def discover():
    """Return (include_dirs, library_dirs, libraries)."""
    prefixes = []
    for var in ("ARR2D_PREFIX", "CONDA_PREFIX", "CGAL_DIR"):
        v = os.environ.get(var)
        if v:
            prefixes.append(v)
    if sys.platform == "darwin":
        brew = shutil.which("brew")
        if brew:
            bp = _run([brew, "--prefix"])
            if bp:
                prefixes.append(bp)
        prefixes += ["/opt/homebrew", "/usr/local"]
    prefixes += ["/usr/local", "/usr"]

    include_dirs = [str(INCLUDE)]
    library_dirs = []

    cgal_inc = os.environ.get("CGAL_INCLUDE_DIR") or _first_existing(
        [str(Path(p) / "include") for p in prefixes], "CGAL/version.h")
    if not cgal_inc:
        # cmake config may know
        raise SystemExit(
            "CGAL headers not found. Install CGAL >= 6.0 (e.g. `brew install cgal`, `conda install cgal-cpp`, "
            "`apt install libcgal-dev`) or set CGAL_INCLUDE_DIR.")
    include_dirs.append(cgal_inc)

    for name, hdr in (("GMP", "gmp.h"), ("MPFR", "mpfr.h"), ("BOOST", "boost/version.hpp")):
        inc = os.environ.get(f"{name}_INCLUDE_DIR") or _first_existing(
            [str(Path(p) / "include") for p in prefixes], hdr)
        if not inc:
            raise SystemExit(f"{name} headers ({hdr}) not found; set {name}_INCLUDE_DIR")
        if inc not in include_dirs:
            include_dirs.append(inc)
        if name != "BOOST":
            lib = os.environ.get(f"{name}_LIB_DIR") or _first_existing(
                [str(Path(p) / "lib") for p in prefixes] + [str(Path(p) / "lib64") for p in prefixes],
                f"lib{name.lower()}.dylib" if sys.platform == "darwin" else f"lib{name.lower()}.so")
            if lib and lib not in library_dirs:
                library_dirs.append(lib)

    return include_dirs, library_dirs, ["mpfr", "gmp"]


def compile_args():
    args = ["-std=c++17"]
    if os.environ.get("ARR2D_DEBUG") == "1":
        args += ["-O0", "-g"]
    else:
        args += [os.environ.get("ARR2D_OPT", "-O2")]
    args += ["-DCGAL_USE_CORE", "-DCGAL_USE_GMP", "-DCGAL_USE_MPFR"]
    if os.environ.get("ARR2D_NDEBUG") == "1":
        args += ["-DNDEBUG", "-DCGAL_NDEBUG"]
    else:
        # Python's CFLAGS define NDEBUG which would silently disable CGAL preconditions. Keep them on.
        args += ["-UNDEBUG"]
    args += ["-fvisibility=hidden", "-fvisibility-inlines-hidden", "-Wno-unused-parameter",
             "-Wno-deprecated-declarations", "-Wno-unused-local-typedef", "-Wno-unused-function",
             "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-Wno-unknown-warning-option",
             "-Wno-sign-compare"]
    if sys.platform == "darwin":
        args += ["-mmacosx-version-min=11.0", "-Wno-unused-command-line-argument"]
    return args


def link_args():
    args = []
    if sys.platform == "darwin":
        args += ["-mmacosx-version-min=11.0"]
    return args


include_dirs, library_dirs, libraries = discover()

cpp_sources = sorted(glob.glob(str(SRC / "src" / "*.cpp")))
pyx = str(HERE / "arrangement_2d" / "_core.pyx")

ext = Extension(
    "arrangement_2d._core",
    sources=[pyx] + cpp_sources,
    include_dirs=include_dirs,
    library_dirs=library_dirs,
    libraries=libraries,
    language="c++",
    extra_compile_args=compile_args(),
    extra_link_args=link_args(),
    define_macros=[("CYTHON_TRACE_NOGIL", "1")] if os.environ.get("ARR2D_PROFILE") == "1" else [],
)


class build_ext(_build_ext):
    def finalize_options(self):
        super().finalize_options()
        if not self.parallel:
            self.parallel = int(os.environ.get("ARR2D_JOBS", os.cpu_count() or 1))

    def build_extensions(self):
        # Silence Python's own -DNDEBUG duplicates etc.; compiler-specific tweaks could go here.
        super().build_extensions()


from Cython.Build import cythonize  # noqa: E402  (build requirement)

directives = {
    "language_level": "3",
    "embedsignature": True,
    "binding": True,
    "c_string_type": "str",
    "c_string_encoding": "utf8",
}
if os.environ.get("ARR2D_PROFILE") == "1":
    directives.update({"linetrace": True, "profile": True})

setup(
    ext_modules=cythonize([ext], compiler_directives=directives, nthreads=int(os.environ.get("ARR2D_JOBS", os.cpu_count() or 1)),
                          include_path=[str(HERE / "arrangement_2d")]),
    cmdclass={"build_ext": build_ext},
    package_data={"arrangement_2d": ["*.pyx", "*.pxd", "py.typed"]},
    zip_safe=False,
)
