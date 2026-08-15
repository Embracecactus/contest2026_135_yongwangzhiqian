# BK7258 release and evidence checklist

This checklist is a release boundary for the BK7258 platform-v2 work.  P9a
shadow results are metadata evidence only; they do not authorize P9b cutover,
signing, Flash, hardware execution, or a remote mutation.

## Contest fork workflow

- [ ] Confirm the intended changes and repository scope on the contest fork.
- [ ] Run the bounded framework and targeted checks from the phase record.
- [ ] Review `git diff --check`, then commit with an explicit, phase-scoped
      message.
- [ ] Push the branch to the contributor's own contest fork only after owner
      approval.
- [ ] Open the pull request from that fork and record its URL and review
      result in the owner-approved handoff.  Do not push from this checklist.

## Public NuttX changes

- [ ] Keep public NuttX/common changes separate from this BK7258 wrapper and
      board change.
- [ ] If a public change is required, prepare a separate NuttX fork branch and
      pull request targeted to `dev-ai-contest-2026`.
- [ ] Record the separate PR, review, and upstream ownership before using it
      as a dependency.  A BK7258 PR is not a substitute for that upstream PR.

## AI Coding session export

- [ ] Keep selected AI Coding sessions in local staging until the owner
      explicitly selects the sessions and destination under the existing
      repository `logs/` tree.
- [ ] Before export, scrub secrets, credentials, tokens, private keys,
      personal data, absolute host paths, restricted SDK bytes, and private
      mirror contents from both text and structured fields.
- [ ] Review the scrubbed diff and metadata for accidental identities or
      environment details; export only the minimum selected sessions.
- [ ] Do not export sessions, create new logs, or copy staging data as part of
      P9a.  This checklist records the later gate only.
