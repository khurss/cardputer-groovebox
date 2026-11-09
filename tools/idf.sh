#!/usr/bin/env bash
# Runs idf.py from the local ESP-IDF image. FatFs is copied into the project with exFAT on,
# because ESP-IDF hard-codes FF_FS_EXFAT to 0 and the user's 200 GB card is probably exFAT.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="${GB_IDF_IMAGE:-cbc2e2ec2afe}"
PROJECT="${1:?usage: tools/idf.sh <project-dir> <idf.py args...>}"
shift
exec docker run --rm -v "$ROOT:/work" -w "/work/$PROJECT" -u "$(id -u):$(id -g)" -e HOME=/tmp "$IMAGE" bash -c '
  set -e
  if [ ! -d components/fatfs ]; then
    mkdir -p components
    cp -r "$IDF_PATH/components/fatfs" components/fatfs
    sed -i "s/^#define FF_FS_EXFAT.*/#define FF_FS_EXFAT\t\t1/" components/fatfs/src/ffconf.h
  fi
  exec idf.py "$@"
' bash "$@"
