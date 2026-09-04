"""The documentation is executable, and it is executed here.

* every ``python`` code block of ``README.md`` and ``docs/user_guide.md`` is run, in
  order, in one shared namespace -- a block with ``>>>`` prompts as a doctest (so the
  outputs printed in the guide are checked against reality), a plain block with
  :func:`exec`;
* ``docs/api_reference.md`` is checked to be in sync with the docstrings it is
  generated from.

A block that is a bare listing rather than runnable code is written as a ``text`` fence
in the sources and is therefore not picked up here.
"""

from __future__ import annotations

import doctest
import importlib.util
import re
import sys
import warnings
from pathlib import Path

import pytest

a2 = pytest.importorskip("arrangement_2d")

ROOT = Path(__file__).resolve().parent.parent
DOCS = [ROOT / "README.md", ROOT / "docs" / "user_guide.md"]
BLOCK_RE = re.compile(r"^```python\n(.*?)^```", re.S | re.M)


def _needs_matplotlib(block: str) -> bool:
    return "matplotlib" in block or "a2.plot." in block


def _blocks(path: Path) -> list:
    return BLOCK_RE.findall(path.read_text(encoding="utf-8"))


@pytest.mark.parametrize("path", DOCS, ids=lambda p: p.name)
def test_documentation_examples_run(path):
    have_mpl = a2.plot.has_matplotlib()
    if have_mpl:
        import matplotlib

        matplotlib.use("Agg")

    blocks = _blocks(path)
    assert blocks, "no python blocks found in %s" % (path,)

    namespace: dict = {}
    runner = doctest.DocTestRunner(optionflags=doctest.ELLIPSIS, verbose=False)
    parser = doctest.DocTestParser()
    failures: list = []

    with warnings.catch_warnings():
        # the plotting examples end with plt.show(), which the Agg backend used here
        # (rightly) says it cannot honour
        warnings.filterwarnings("ignore", message=".*non-interactive.*")
        for index, block in enumerate(blocks):
            if _needs_matplotlib(block) and not have_mpl:
                continue
            if ">>>" not in block:
                exec(compile(block, "<%s block %d>" % (path.name, index), "exec"),
                     namespace)
                continue
            report: list = []
            test = parser.get_doctest(block, namespace, "%s:%d" % (path.name, index),
                                      str(path), 0)
            result = runner.run(test, out=report.append, clear_globs=False)
            namespace.update(test.globs)
            if result.failed:
                failures.append("".join(report))

    assert not failures, "\n".join(failures)


def test_api_reference_is_up_to_date():
    """``docs/api_reference.md`` matches what the generator produces right now.

    Re-run ``python docs/gen_api_reference.py`` after changing a docstring.
    """
    script = ROOT / "docs" / "gen_api_reference.py"
    spec = importlib.util.spec_from_file_location("_gen_api_reference", script)
    module = importlib.util.module_from_spec(spec)
    sys.modules["_gen_api_reference"] = module
    try:
        spec.loader.exec_module(module)
        assert module.main(["--check"]) == 0, (
            "docs/api_reference.md is out of date; re-run docs/gen_api_reference.py")
    finally:
        del sys.modules["_gen_api_reference"]
