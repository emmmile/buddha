# 0005. Ideas and known issues live as files in `docs/`, not in an issue tracker

**Status:** Accepted
**Date:** 2026-10-02

## Context

The project has two remotes (GitHub for older PRs, GitLab for merge requests
now), and much of the work happens in agent sessions that read the
repository. Ideas and known problems came out of research sessions with
numbers, reasoning and links into the research docs. They need to be found by
the next session without access to a tracker.

Source: the user's decision when the docs were reorganized (this ADR's merge
request).

## Decision

Keep ideas in [`IDEAS.md`](../IDEAS.md) and known problems in
[`ISSUES.md`](../ISSUES.md), at the root of `docs/`, as living files. Each
entry says where it came from, what is known, and links the research behind
it. Don't open GitLab or GitHub issues for them.

## Consequences

- They are greppable, travel with the code and its history, and work the same
  from either remote or a plain clone.
- Changes to them are reviewed in merge requests like any other doc.
- There is no assignment, labelling or notification. Entries have to be kept
  current by hand: update the status when something is fixed or done, and
  move finished work into the relevant plan, research doc or ADR.
