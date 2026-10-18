# Walkthroughs

Follow-along tutorials. Each one is a sequence of copy-pasteable commands with
the real output beside them, so you can work through it at your own pace and
check yourself at every step.

The output in these files is captured from actual runs of this repository. If a
command prints something different for you, that's a real difference worth
chasing, not a typo in the docs.

| # | Walkthrough | What you need |
|---|---|---|
| 1 | [Run the virtual PCB](01-virtual-pcb.md) — build a board out of Linux processes, drive a DAC across a socket, read it back on the ADC, then unplug the chip mid-session | Linux or WSL, no hardware |
| 2 | [Fault injection](02-fault-injection.md) — cut a trace on a running board, pull and mis-strap chips, and check the firmware says which readings are real; then 28 faults in about 23 seconds | Walkthrough 1's build |
| 3 | [Fix it layer by layer](03-fix-it-layer-by-layer.md) — go back to before the fix, watch a cut trace read as a confident 0 mV, then apply the real fix one commit at a time until it reads `---` | Walkthrough 1's toolchain |

Start with the [README prerequisites](../README.md#prerequisites) if you haven't
installed the Zephyr toolchain yet.
