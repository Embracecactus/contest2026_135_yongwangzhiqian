# Correlating UART with a physical action

This extends the existing Windows/WSL capture transport; it does not authorize
reset, flash, peripheral output or camera access. Obtain target mapping,
baud, duration and reviewed action from the project. Keep one port owner.

1. Use the established capture wrapper for its supported options. When live
   state gating is required, the existing `serial_capture.ps1` supports
   `-ReadyFile`, `-EchoToStdout`, `-OutputFile` and a bounded duration. A wrapper
   may not expose every option; inspect its current help before choosing the
   existing direct PowerShell entry. Do not add a second capture process.
2. Choose new output paths. The ready file must be distinct from the raw file
   and must belong to this invocation. Wait for ready before the dependent
   action. Ready means transport setup, not that the firmware is ready for a
   voice command, camera request or network operation.
3. Read live output for the required target state. Raw file writes may remain
   buffered while the process runs. Do not conclude that an empty live raw
   file proves dead UART, and do not reset the target solely on that evidence.
4. Trigger the action only after its actual firmware marker; apply a bounded
   wait. If that marker never occurs, report the dependent gate not executed.
   Host process success and target command success remain separate.
5. Pass multiple console commands as the transport's supported command array
   or repeated wrapper argument. The WSL wrapper encodes its command values
   for PowerShell; reuse it. Literal backslash-n in one string may reach the
   target as text rather than two commands. A firmware usage message after
   such input is not a successful status read. Avoid shell substitution and
   never embed credentials in logged command strings.
6. Correlate host action timestamps, player logs and firmware phases without
   subtracting unsynchronized clocks. Use paired state markers or explicitly
   report each time domain. A delayed unattributed event does not prove which
   acoustic source caused it.
7. Wait for completion and port release before another owner connects. Inspect
   final `serial.raw`, command result markers and failure lines. A zero exit
   status can coexist with a rejected target command or failed product turn.

Output the transport/session identity, final raw path, actual phase reached,
first failure and unexecuted dependent actions. Do not archive unrelated raw
logs or secrets with a reusable Skill. This document has source/static and
prior session evidence; each new target still needs its own live verification.
