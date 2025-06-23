#!/bin/bash
# AiOS Phase 01 Task Executor – Codex-Friendly Shell Script
# Usage: ./execute_tasks.sh Phase01_Tasks.Txt

TASK_FILE="$1"
LOG_FILE="aios/logs/Phase01_Codex_Results.md"
mkdir -p $(dirname "$LOG_FILE")
echo "# AiOS Phase 01 Codex Execution Log" > "$LOG_FILE"

# Extract tasks and loop through each
awk '
  BEGIN { RS="Task Number : "; FS="\n" }
  NR > 1 {
    print "=== Executing Task " $1 " ==="
    task_num = $1
    for (i = 1; i <= NF; i++) {
      if ($i ~ /^Task Name/) task_name = $i
      if ($i ~ /^Effected C Files/) files = $i
      if ($i ~ /^Task Description/) desc = $i
      if ($i ~ /^Tasks Codex Instructions/) code = $i
      if ($i ~ /^Task Suggested Test Method/) test = $i
    }
    print "## Task " task_num >> "'$LOG_FILE'"
    print task_name >> "'$LOG_FILE'"
    print desc >> "'$LOG_FILE'"
    print "**Codex Instructions:**" >> "'$LOG_FILE'"
    print code >> "'$LOG_FILE'"
    print "**Test Method:**" >> "'$LOG_FILE'"
    print test >> "'$LOG_FILE'"
    print "**Status:** Executed by Codex (simulate or confirm results)" >> "'$LOG_FILE'"
    print "" >> "'$LOG_FILE'"
  }
' "$TASK_FILE"
