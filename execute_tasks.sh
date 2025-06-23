#!/bin/bash
# AiOS Phase 01 Task Executor – Improved Logging Version
# Usage: ./execute_tasks.sh aios/Phase01_Tasks.Txt

TASK_FILE="$1"
LOG_FILE="aios/logs/Phase01_Codex_Results.md"
mkdir -p $(dirname "$LOG_FILE")
echo "# AiOS Phase 01 Codex Execution Log" > "$LOG_FILE"
echo "" >> "$LOG_FILE"

# Read task entries block by block
awk '
BEGIN { RS="\n\n"; FS="\n" }
{
    task_number = ""; task_name = ""; files = ""; desc = ""; code = ""; test = ""; notes = "";
    for (i = 1; i <= NF; i++) {
        if ($i ~ /^Task Number[[:space:]]*:/) task_number = substr($i, index($i, ":") + 2)
        else if ($i ~ /^Task Name[[:space:]]*:/) task_name = substr($i, index($i, ":") + 2)
        else if ($i ~ /^Effected C Files[[:space:]]*:/) files = substr($i, index($i, ":") + 2)
        else if ($i ~ /^Task Description[[:space:]]*:/) desc = substr($i, index($i, ":") + 2)
        else if ($i ~ /^Tasks Codex Instructions[[:space:]]*:/) code = substr($i, index($i, ":") + 2)
        else if ($i ~ /^Task Suggested Test Method[[:space:]]*:/) test = substr($i, index($i, ":") + 2)
        else if ($i ~ /^Notes[[:space:]]*:/) notes = substr($i, index($i, ":") + 2)
    }

    if (task_number != "") {
        print "## Task " task_number " – " task_name >> "'$LOG_FILE'"
        print "**Effected C Files:**\n" files >> "'$LOG_FILE'"
        print "\n**Description:**\n" desc >> "'$LOG_FILE'"
        print "\n**Codex Instructions:**\n" code >> "'$LOG_FILE'"
        print "\n**Test Method:**\n" test >> "'$LOG_FILE'"
        if (notes != "") print "\n**Notes:**\n" notes >> "'$LOG_FILE'"
        print "\n---\n" >> "'$LOG_FILE'"
    }
}
' "$TASK_FILE"

echo "✅ Log file created: $LOG_FILE"
