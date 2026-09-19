---
name: fork-change-publication
description: Prepare a reviewable scoped change from a dirty development checkout for an official repository and its fork, including exact staging, branch ancestry, source correspondence and a truthful PR body. Use when committing, pushing or drafting a fork contribution is requested. Not authorization to push, open or merge a PR, force-update branches, publish device secrets or sweep unrelated work into a commit.
---

# Scoped fork publication

## Inputs and permission

Obtain the actual Git root, intended official repository/base, development
fork, destination branch, requested publication actions and project Git rules.
Resolve remote roles from URLs, not names such as `origin`. Commit, push,
creating/editing a PR and merging are distinct actions; execute the subset
authorized in the current task. Drafting PR text does not authorize sending
it or merging it. Honor existing authorization without asking repeatedly.

## Execute

1. Inspect `git rev-parse --show-toplevel`, `git remote -v`,
   `git status --short`, current branch/HEAD and diff summary. Record unrelated
   dirty paths and staged changes; preserve them. Do not use a broad add/reset,
   clean or checkout to manufacture a clean workspace.
2. Verify the relevant remote refs when network access is available and
   authorized, using `git ls-remote` or a scoped fetch. Label cached refs if
   current verification is unavailable. Compare base ancestry, merge-base and
   patch/tree differences; a reference repository is not automatically the
   intended upstream.
3. If development ancestry is unsuitable but content is equivalent to the
   official base, prove it with tree/diff and patch equivalence before choosing
   a new branch from the base. Prefer an isolated worktree when preserving the
   current dirty state requires one. Carry the exact reviewed delta normally.
   Never fake lineage with an `ours` merge or overwrite shared history.
4. Define the publication path/hunk set from the requested change and actual
   build/source dependencies. Include canonical overlays and necessary assets;
   exclude credentials, signing keys, device backups, private raw evidence,
   unrelated experiments and generated outputs unless explicitly part of the
   deliverable. When source corresponds to a built artifact, compare the
   artifact's source manifest and Git checkout filters, not only commit labels.
5. Stage exact paths/hunks: `git add -- "$path"` for wholly owned files or
   reviewed hunks for mixed files. Check `git diff --cached --stat`, the full
   relevant staged diff and `git diff --cached --check`. Existing unrelated
   staged content must not leak into the new commit; use an isolated worktree
   if ownership cannot be safely separated.
6. Commit only when requested, with the final functional scope. Recheck the
   tree and remaining work. Push only the authorized destination with an
   explicit branch ref; never assume permission to force-push. Verify remote
   SHA/compare result after pushing. A local success is not remote publication.
7. Write a PR title/body describing the final behavior for a new reviewer:
   concrete problem → actual change → relevant validation → unresolved limits.
   Name tested artifact/source identities where material. Do not call host
   checks physical proof, draft source a merged change, or partial delivery a
   finished product. Keep conversational history, secrets and memory citations
   out of the PR text. If posting with a CLI is authorized, use a body file
   with real newlines (`--body-file`), not unsafe shell interpolation.

## Failure branches

- Wrong remote/base or unexpected ahead/behind: resolve before pushing; do not
  use a merge strategy merely to hide unexpected commits.
- Changes share a file with unrelated edits: stage reviewed hunks or carry
  the intended patch to an isolated worktree, preserving the original.
- CRLF/blob hash mismatch: inspect `.gitattributes` and filtered checkout
  bytes before deciding there is a functional/source difference.
- Permission/network failure: retain the prepared commit/body and exact failed
  action. Do not report a push/PR as complete without remote confirmation.
- User plans to merge: hand over the concrete title/body and branch comparison;
  do not merge on their behalf.

## Deliver and completion

Return the actual commit/branch/remote result for actions taken, the PR draft
or authorized PR URL, selected checks and preserved outstanding work. Save a
requested draft in the project's existing handoff location or a clearly named
temporary file, not a new publication framework. Completion is bounded by the
requested action; no commit, push or merge is implied by Skill invocation.
