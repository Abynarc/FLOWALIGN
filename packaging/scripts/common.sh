#!/bin/sh
set -eu
FLOWALIGN_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
FLOWALIGN_VOLUME=${3:-/}
FLOWALIGN_ROOT=${FLOWALIGN_VOLUME%/}
FLOWALIGN_COMPONENT="$FLOWALIGN_ROOT/Library/Audio/Plug-Ins/Components/FLOWALIGN.component"
FLOWALIGN_BACKUPS="$FLOWALIGN_ROOT/Library/Application Support/FLOWMINNERS/FLOWALIGN/Backups"

backup_directory() {
    /bin/mkdir -p "$FLOWALIGN_BACKUPS"
    /usr/bin/mktemp -d "$FLOWALIGN_BACKUPS/$(/bin/date +%Y%m%d-%H%M%S)-XXXXXX"
}

console_home() {
    FLOWALIGN_CONSOLE_USER=$(/usr/bin/stat -f '%Su' /dev/console)
    case "$FLOWALIGN_CONSOLE_USER" in root|loginwindow|_mbsetupuser|'') return 1 ;; esac
    FLOWALIGN_USER_HOME=$(/usr/bin/dscl . -read "/Users/$FLOWALIGN_CONSOLE_USER" NFSHomeDirectory | /usr/bin/sed 's/^NFSHomeDirectory: //')
    case "$FLOWALIGN_USER_HOME" in /*) ;; *) return 1 ;; esac
}
