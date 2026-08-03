<!-- PROJECT_MEMORY_START -->
## Project memory protocol

- At the start of long-running or resumed work, read `memory/INDEX.md` and `progress/CURRENT.md`, then only the linked context relevant to the task.
- Verify important memory claims against Git, code, tests, configuration, or deployed state before acting on them.
- Keep durable facts and decisions in `memory/`; keep active state, next actions, blockers, and evidence in `progress/`.
- Treat the primary/coordinating agent as the single writer for `progress/CURRENT.md` unless ownership is explicitly delegated.
- After material requirements, implementation, verification, commit, deployment, rollback, or blocker changes, update the relevant memory checkpoint when authorized.
- Never store secrets, credentials, tokens, private keys, personal data, or raw production records in project memory.
- Do not commit, push, deploy, delete, or overwrite user work merely because the memory protocol exists; normal authorization rules still apply.
<!-- PROJECT_MEMORY_END -->
