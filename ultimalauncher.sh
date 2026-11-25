#!/bin/bash
# UltimaLauncher wrapper (não edite este arquivo à mão)
cd "$HOME/UltimaLauncher" || exit 1

unset QT_PLUGIN_PATH
unset QT_QPA_PLATFORM_PLUGIN_PATH

if [ -x "$HOME/UltimaLauncher/.venv/bin/python" ]; then
    PY="$HOME/UltimaLauncher/.venv/bin/python"
elif [ -x "$HOME/game-launcher/.venv/bin/python" ]; then
    PY="$HOME/game-launcher/.venv/bin/python"
else
    PY="$(command -v python3)"
fi

# A saída vai pra um arquivo temporário. Se o launcher fechar normal
# (código 0) ele é descartado; se der erro, vira um log novo em
# logs/ com data e hora no nome — nunca reescreve o anterior.
LOG_DIR="$HOME/UltimaLauncher/logs"
mkdir -p "$LOG_DIR"
TMP_LOG="$(mktemp "$LOG_DIR/.run.XXXXXX")"

"$PY" "$HOME/UltimaLauncher/launcher.py" "$@" > "$TMP_LOG" 2>&1
STATUS=$?

if [ "$STATUS" -ne 0 ]; then
    FINAL="$LOG_DIR/ultimalauncher_$(date +%Y-%m-%d_%H-%M-%S)_erro.log"
    N=2
    while [ -e "$FINAL" ]; do
        FINAL="$LOG_DIR/ultimalauncher_$(date +%Y-%m-%d_%H-%M-%S)_erro-$N.log"
        N=$((N + 1))
    done
    mv "$TMP_LOG" "$FINAL"
    # mantém só os 30 logs mais recentes
    ls -1t "$LOG_DIR"/ultimalauncher_*.log 2>/dev/null | tail -n +31 | xargs -r rm -f --
else
    rm -f "$TMP_LOG"
fi

exit "$STATUS"
