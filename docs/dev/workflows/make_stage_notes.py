#!/usr/bin/env python3
"""Turn a workflow journal (journal.jsonl) into a Markdown notes file.

usage: make_stage_notes.py <journal.jsonl> <title> [<out.md>]
Each agent result must be an object with summary / files_written / verified /
interface_change_requests / open_issues / conventions fields (our REPORT schema).
"""
import json
import sys


def main():
    journal, title = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else None
    parts = [f"# {title}\n", "Auto-generated from the workflow journal by make_stage_notes.py.\n"]
    for line in open(journal):
        try:
            rec = json.loads(line)
        except Exception:
            continue
        res = rec.get("result")
        if not isinstance(res, dict) or "summary" not in res:
            continue
        files = res.get("files_written") or []
        import os
        label = rec.get("label") or rec.get("agentLabel") or rec.get("name") or (
            ", ".join(os.path.basename(f) for f in files[:3]) + (" ..." if len(files) > 3 else "")) or "agent"
        parts.append(f"\n## {label}\n")
        parts.append("**Files:** " + ", ".join(f"`{f}`" for f in files) + "\n")
        parts.append("\n### Summary\n\n" + str(res.get("summary", "")).strip() + "\n")
        parts.append("\n### Verified\n\n" + str(res.get("verified", "")).strip() + "\n")
        conv = res.get("conventions")
        if isinstance(conv, list):
            conv = "\n".join(f"- {c}" for c in conv)
        parts.append("\n### Conventions\n\n" + str(conv or "").strip() + "\n")
        icr = res.get("interface_change_requests") or []
        parts.append("\n### Interface change requests\n\n" + ("\n".join(f"- {x}" for x in icr) or "- none") + "\n")
        oi = res.get("open_issues") or []
        parts.append("\n### Open issues\n\n" + ("\n".join(f"- {x}" for x in oi) or "- none") + "\n")
    text = "\n".join(parts)
    if out:
        open(out, "w").write(text)
        print(f"wrote {out} ({len(text)} bytes)")
    else:
        print(text)


if __name__ == "__main__":
    main()
