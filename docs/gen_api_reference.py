#!/usr/bin/env python3
"""Generate ``docs/api_reference.md`` from the docstrings of the built extension.

The script imports :mod:`arrangement_2d` (so the extension must be built --
``python setup.py build_ext --inplace``), walks its public names plus the two optional
submodules, and writes one Markdown section per class / function, converting the
reStructuredText markup used in the docstrings (field lists, ``::`` literal blocks,
``.. note::`` admonitions and simple tables) into Markdown.

Usage::

    python docs/gen_api_reference.py               # write docs/api_reference.md
    python docs/gen_api_reference.py -o -          # write to stdout
    python docs/gen_api_reference.py --check       # exit 1 if the file is out of date
"""

from __future__ import annotations

import argparse
import enum
import inspect
import os
import re
import sys
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

import arrangement_2d as a2                                             # noqa: E402

#: Top level names grouped into sections, in the order they should be documented.
#: Any public name that is not listed here still ends up in the "Other names" section,
#: so the reference can never silently lose a part of the API.
GROUPS = [
    ("Kinds and build information",
     ["Kind", "available_kinds", "kind_available", "point_location_strategies",
      "cgal_version", "build_info"]),
    ("Exact numbers",
     ["Algebraic", "SqrtExtension"]),
    ("Points and curves",
     ["Point", "Curve", "Segment", "LinearCurve", "Line", "Ray",
      "line_from_coefficients", "CircleSegment", "Circle", "CircularArc", "Polyline",
      "BezierCurve", "ConicArc", "conic_allow_hyperbolic", "GeodesicArc",
      "SphericalArc"]),
    ("Polygons",
     ["Polygon", "PolygonWithHoles"]),
    ("Arrangement",
     ["Arrangement", "Traits", "traits", "Observer", "OverlayCallbacks"]),
    ("DCEL handles",
     ["Vertex", "Halfedge", "Face", "CurveHandle"]),
    ("Boolean set operations",
     ["PolygonSet", "join", "intersection", "difference", "symmetric_difference",
      "complement", "do_intersect", "oriented_side", "is_valid_polygon",
      "orientation"]),
    ("Errors",
     ["CGALError", "PreconditionError", "PostconditionError", "CGALAssertionError",
      "CGALWarning", "InvalidHandleError", "KindMismatchError", "NotXMonotoneError",
      "NotRepresentableError", "UnsupportedError", "CallbackError"]),
]

#: Special methods that are worth mentioning (as a one-line summary per class).
DUNDERS = [
    "__len__", "__iter__", "__getitem__", "__contains__", "__call__", "__eq__",
    "__hash__", "__copy__", "__deepcopy__", "__or__", "__and__", "__sub__", "__xor__",
    "__invert__", "__ior__", "__iand__", "__isub__", "__ixor__", "__reduce__",
]


# ---------------------------------------------------------------------------
# reStructuredText -> Markdown
# ---------------------------------------------------------------------------

FIELD_RE = re.compile(r"^:(param|type|returns|return|rtype|raises|raise|ivar|var|cvar)"
                      r"(?:\s+([^:]+))?:\s*(.*)$")
ROLE_RE = re.compile(r":(?:class|func|meth|attr|mod|obj|data|exc|ref):`~?([^`]+)`")
TABLE_RULE_RE = re.compile(r"^=+(?:\s+=+)+\s*$")
SIGNATURE_RE = re.compile(r"^[A-Za-z_][\w.]*\(.*\)(\s*->.*)?$")

FIELD_LABELS = {
    "param": "**{name}** --",
    "type": "type of **{name}** --",
    "returns": "**Returns** --",
    "return": "**Returns** --",
    "rtype": "**Return type** --",
    "raises": "**Raises** `{name}` --",
    "raise": "**Raises** `{name}` --",
    "ivar": "**{name}** --",
    "var": "**{name}** --",
    "cvar": "**{name}** --",
}


def _inline(text: str) -> str:
    """reST inline markup -> Markdown (``x`` stays, roles become code spans)."""
    return ROLE_RE.sub(lambda m: "`%s`" % (m.group(1),), text)


def _table(lines: list, start: int) -> tuple:
    """Convert a simple reST table starting at *start* into a Markdown table.

    :returns: ``(markdown_lines, index_after_the_table)``.
    """
    rule = lines[start]
    spans = [(m.start(), m.end()) for m in re.finditer(r"=+", rule)]

    def cells(line: str) -> list:
        out = []
        for i, (lo, hi) in enumerate(spans):
            hi = len(line) if i == len(spans) - 1 else hi
            out.append(line[lo:hi].strip() if lo < len(line) else "")
        return out

    body: list = []
    i = start + 1
    header = None
    while i < len(lines):
        line = lines[i]
        if TABLE_RULE_RE.match(line):
            following = lines[i + 1] if i + 1 < len(lines) else ""
            # A simple table has two rules (body only) or three (the middle one closes
            # the header).  They look identical, so the rule is a header separator
            # exactly when the table continues right after it.
            if (header is None and body
                    and following.strip() and not TABLE_RULE_RE.match(following)):
                header = body[-1]
                body = []
                i += 1
                continue
            i += 1
            break
        if line.strip():
            body.append(cells(line))
        i += 1
    if header is None:
        header = [""] * len(spans)
    out = ["| " + " | ".join(_inline(c) for c in header) + " |",
           "|" + "|".join(["---"] * len(spans)) + "|"]
    for row in body:
        out.append("| " + " | ".join(_inline(c) for c in row) + " |")
    return out, i


def rst_to_markdown(doc: str) -> str:
    """Convert one docstring into Markdown."""
    if not doc:
        return ""
    lines = doc.expandtabs().split("\n")
    out: list = []
    i = 0
    pending_field = False
    while i < len(lines):
        line = lines[i]

        # ---- simple table
        if TABLE_RULE_RE.match(line):
            block, i = _table(lines, i)
            out.append("")
            out.extend(block)
            out.append("")
            pending_field = False
            continue

        # ---- admonition
        admonition = re.match(r"^\.\.\s+(note|warning|important|seealso)::\s*(.*)$", line)
        if admonition:
            label = admonition.group(1).capitalize()
            body = [admonition.group(2).strip()] if admonition.group(2).strip() else []
            i += 1
            while i < len(lines) and (not lines[i].strip() or lines[i].startswith("   ")):
                if not lines[i].strip() and (i + 1 >= len(lines)
                                             or not lines[i + 1].startswith("   ")):
                    break
                body.append(lines[i].strip())
                i += 1
            out.append("")
            out.append("> **%s** %s" % (label, _inline(" ".join(body).strip())))
            out.append("")
            pending_field = False
            continue

        # ---- bare doctest block
        if line.lstrip().startswith(">>>"):
            indent = len(line) - len(line.lstrip())
            block = []
            while i < len(lines) and lines[i].strip():
                block.append(lines[i][indent:] if lines[i].startswith(" " * indent)
                             else lines[i].strip())
                i += 1
            out.append("")
            out.append("```python")
            out.extend(block)
            out.append("```")
            out.append("")
            pending_field = False
            continue

        # ---- literal block introduced by "::"
        if line.rstrip().endswith("::"):
            head = line.rstrip()[:-2].rstrip()
            if head:
                out.append(_inline(head) + ":")
            i += 1
            while i < len(lines) and not lines[i].strip():
                i += 1
            block: list = []
            while i < len(lines) and (not lines[i].strip() or lines[i].startswith(" ")):
                block.append(lines[i])
                i += 1
            while block and not block[-1].strip():
                block.pop()
            out.append("")
            out.append("```python")
            out.extend(textwrap.dedent("\n".join(block)).split("\n"))
            out.append("```")
            out.append("")
            pending_field = False
            continue

        # ---- field list (:param x: ..., :rtype: ...)
        field = FIELD_RE.match(line.strip())
        if field:
            kind, name, text = field.group(1), (field.group(2) or "").strip(), field.group(3)
            label = FIELD_LABELS[kind].format(name=name)
            if not pending_field:
                out.append("")
            out.append("- %s %s" % (label, _inline(text.strip())))
            pending_field = True
            i += 1
            continue

        if pending_field:
            if line.strip():
                out[-1] += " " + _inline(line.strip())
                i += 1
                continue
            pending_field = False

        out.append(_inline(line))
        i += 1

    text = "\n".join(out)
    return re.sub(r"\n{3,}", "\n\n", text).strip()


# ---------------------------------------------------------------------------
# introspection
# ---------------------------------------------------------------------------

def clean_doc(obj) -> str:
    """``inspect.getdoc`` without the signature line Cython's ``embedsignature`` adds."""
    doc = inspect.getdoc(obj) or ""
    lines = doc.split("\n")
    if lines and SIGNATURE_RE.match(lines[0].strip()):
        lines = lines[1:]
        while lines and not lines[0].strip():
            lines = lines[1:]
    return "\n".join(lines).strip()


#: ``from __future__ import annotations`` turns every annotation into a string, which
#: ``inspect.signature`` then renders quoted; the quotes are noise in a reference.
ANNOTATION_RE = re.compile(r"(:|->) '([^']*)'")


def unquote_annotations(sig: str) -> str:
    return ANNOTATION_RE.sub(r"\1 \2", sig)


def signature_of(obj, name: str) -> str:
    """``name(args)`` for *obj*, falling back to ``name(...)``."""
    try:
        sig = str(inspect.signature(obj))
    except (TypeError, ValueError):
        return "%s(...)" % (name,)
    sig = re.sub(r"^\(self(?:,\s*)?", "(", sig)
    return unquote_annotations(name + sig)


def class_signature(cls, name: str) -> str:
    """``Name(args)`` for the constructor, or ``""`` when there is nothing to show.

    Cython's ``embedsignature`` writes the constructor signature as the first line of
    the *class* docstring (its ``__init__`` is a plain wrapper descriptor); a Python
    class carries it on the class object itself.  A signature that consists of nothing
    but ``*args, **kwargs`` carries no information (that is what the handle classes,
    which cannot be constructed from Python at all, look like), so it is dropped.
    """
    first = (cls.__doc__ or "").split("\n")[0].strip()
    if first.startswith(name + "(") and SIGNATURE_RE.match(first):
        args = first[len(name) + 1:first.rindex(")")].strip()
        named = [a for a in args.split(",") if a.strip() and not a.strip().startswith("*")]
        if named:
            return first
    for candidate in (getattr(cls, "__init__", None), cls):
        if candidate is None:
            continue
        try:
            sig = inspect.signature(candidate)
        except (TypeError, ValueError):
            continue
        kinds = [p.kind for n, p in sig.parameters.items() if n != "self"]
        if not kinds or all(k in (inspect.Parameter.VAR_POSITIONAL,
                                  inspect.Parameter.VAR_KEYWORD) for k in kinds):
            continue
        return unquote_annotations(name + re.sub(r"^\(self(?:,\s*)?", "(", str(sig)))
    return ""


def is_property(obj) -> bool:
    return isinstance(obj, property) or type(obj).__name__ in (
        "getset_descriptor", "member_descriptor", "cached_property")


def public_members(cls) -> tuple:
    """``(properties, methods, specials)`` of *cls*, each sorted by name."""
    properties, methods, specials = [], [], []
    for name in sorted(dir(cls)):
        try:
            attr = inspect.getattr_static(cls, name)
        except AttributeError:                                # pragma: no cover
            continue
        if name.startswith("_"):
            if name in DUNDERS and name in vars(cls):
                specials.append(name)
            continue
        if getattr(tuple, name, None) is attr or getattr(object, name, None) is attr:
            continue                                          # inherited plumbing
        if (inspect.getdoc(attr) or "").startswith("Alias for field number"):
            continue                                          # namedtuple field alias
        if is_property(attr):
            properties.append((name, getattr(cls, name, attr)))
        elif isinstance(attr, (staticmethod, classmethod)):
            methods.append((name, getattr(cls, name)))
        elif callable(attr) or inspect.isroutine(attr):
            methods.append((name, attr))
        else:
            properties.append((name, attr))
    return properties, methods, specials


def gh_anchor(text: str, seen: dict) -> str:
    """GitHub's heading anchor for *text*, disambiguated like GitHub does."""
    slug = re.sub(r"[^\w\s-]", "", text.strip().lower())
    slug = re.sub(r"\s+", "-", slug)
    count = seen.get(slug, 0)
    seen[slug] = count + 1
    return slug if count == 0 else "%s-%d" % (slug, count)


class Doc:
    """The Markdown document under construction (keeps the heading anchors unique)."""

    def __init__(self):
        self.lines: list = []
        self.seen: dict = {}
        self.anchors: dict = {}

    def add(self, *lines) -> None:
        self.lines.extend(lines)

    def heading(self, level: int, text: str, key=None) -> str:
        slug = gh_anchor(text, self.seen)
        if key is not None:
            self.anchors[key] = slug
        self.lines.append("%s %s" % ("#" * level, text))
        self.lines.append("")
        return slug

    def doc(self, obj, fallback: str = "") -> None:
        text = clean_doc(obj)
        self.lines.append(rst_to_markdown(text) if text else fallback)
        self.lines.append("")


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------

def render_enum(name: str, cls, doc: Doc) -> None:
    doc.heading(3, "`%s`" % (name,), key=name)
    doc.doc(cls)
    doc.add("| member | value |", "|---|---|")
    for member in cls:
        doc.add("| `%s.%s` | `%d` |" % (name, member.name, int(member)))
    doc.add("")


def render_exception(name: str, cls, doc: Doc) -> None:
    doc.heading(3, "`%s`" % (name,), key=name)
    doc.add("*Bases: %s*" % (", ".join("`%s`" % (b.__name__,) for b in cls.__bases__),),
            "")
    doc.doc(cls)


def render_class(name: str, cls, doc: Doc) -> None:
    doc.heading(3, "`%s`" % (name,), key=name)
    constructor = class_signature(cls, name)
    if constructor:
        doc.add("```python", constructor, "```", "")
    doc.doc(cls)
    properties, methods, specials = public_members(cls)
    if properties:
        doc.heading(4, "Attributes of `%s`" % (name,))
        for attr_name, attr in properties:
            doc.heading(5, "`%s.%s`" % (name, attr_name))
            doc.doc(attr, "*(undocumented)*")
    if methods:
        doc.heading(4, "Methods of `%s`" % (name,))
        for meth_name, meth in methods:
            doc.heading(5, "`%s.%s`" % (name, meth_name))
            doc.add("```python", signature_of(meth, meth_name), "```", "")
            doc.doc(meth, "*(undocumented)*")
    if specials:
        doc.add("Special methods: %s" % (", ".join("`%s`" % (s,) for s in specials),), "")


def render_function(name: str, func, doc: Doc) -> None:
    doc.heading(3, "`%s`" % (name,), key=name)
    doc.add("```python", signature_of(func, name), "```", "")
    doc.doc(func, "*(undocumented)*")


def render_object(name: str, obj, doc: Doc) -> None:
    if isinstance(obj, type) and issubclass(obj, enum.Enum):
        render_enum(name, obj, doc)
    elif isinstance(obj, type) and issubclass(obj, BaseException):
        render_exception(name, obj, doc)
    elif isinstance(obj, type):
        render_class(name, obj, doc)
    elif callable(obj):
        render_function(name, obj, doc)
    else:                                                     # pragma: no cover
        doc.heading(3, "`%s`" % (name,), key=name)
        doc.add("`%r`" % (obj,), "")


def render_submodule(module, title: str, doc: Doc) -> None:
    doc.heading(2, title, key=title)
    doc.doc(module)
    for name in getattr(module, "__all__", []):
        render_object(name, getattr(module, name), doc)


def build() -> str:
    doc = Doc()

    documented = set()
    sections: list = []
    for title, names in GROUPS:
        present = [n for n in names if hasattr(a2, n)]
        documented.update(present)
        sections.append((title, present))
    leftovers = [n for n in a2.__all__ if n not in documented]
    if leftovers:
        sections.append(("Other names", leftovers))

    # The heading anchors have to be assigned in document order, so the body is
    # rendered first and the table of contents is built from the anchors it recorded.
    preamble = Doc()
    preamble.heading(1, "`arrangement_2d` API reference")
    preamble.add("*Generated by `docs/gen_api_reference.py` from the docstrings of "
                 "arrangement_2d %s (built against CGAL %s). Do not edit by hand.*"
                 % (a2.__version__, a2.cgal_version()), "")
    preamble.doc(a2)
    preamble.heading(2, "Contents")
    doc.seen = preamble.seen

    for title, names in sections:
        doc.heading(2, title, key=title)
        for name in names:
            render_object(name, getattr(a2, name), doc)
    submodules = [(a2.regions, "Module `arrangement_2d.regions`"),
                  (a2.plot, "Module `arrangement_2d.plot`")]
    for module, title in submodules:
        render_submodule(module, title, doc)

    toc: list = []
    for title, names in sections:
        toc.append("- [%s](#%s)" % (title, doc.anchors[title]))
        for name in names:
            toc.append("    - [`%s`](#%s)" % (name, doc.anchors[name]))
    for module, title in submodules:
        toc.append("- [%s](#%s)" % (title, doc.anchors[title]))
        for name in getattr(module, "__all__", []):
            toc.append("    - [`%s`](#%s)" % (name, doc.anchors[name]))
    toc.append("")

    text = "\n".join(preamble.lines + toc + doc.lines)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.rstrip() + "\n"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("-o", "--output", default=os.path.join(HERE, "api_reference.md"),
                        help="output file ('-' for stdout)")
    parser.add_argument("--check", action="store_true",
                        help="only check that the output file is up to date")
    args = parser.parse_args(argv)

    text = build()
    if args.check:
        try:
            with open(args.output, "r", encoding="utf-8") as fh:
                current = fh.read()
        except FileNotFoundError:
            print("%s does not exist" % (args.output,), file=sys.stderr)
            return 1
        if current != text:
            print("%s is out of date; re-run docs/gen_api_reference.py"
                  % (args.output,), file=sys.stderr)
            return 1
        return 0
    if args.output == "-":
        sys.stdout.write(text)
        return 0
    with open(args.output, "w", encoding="utf-8") as fh:
        fh.write(text)
    print("wrote %s (%d lines)" % (args.output, text.count("\n") + 1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
