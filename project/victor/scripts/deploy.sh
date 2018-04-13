#!/bin/bash

set -e
set -u

# Go to directory of this script
SCRIPT_PATH=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))
SCRIPT_NAME=$(basename ${0})
GIT=`which git`
if [ -z $GIT ]
then
    echo git not found
    exit 1
fi
TOPLEVEL=`$GIT rev-parse --show-toplevel`

source ${SCRIPT_PATH}/victor_env.sh

# Settings can be overridden through environment
: ${VERBOSE:=0}
: ${FORCE_RSYNC_BIN:=0}
: ${ANKI_BUILD_TYPE:="Debug"}
: ${INSTALL_ROOT:="/anki"}
: ${DEVICE_RSYNC_BIN_DIR:="/usr/bin"}

function usage() {
  echo "$SCRIPT_NAME [OPTIONS]"
  echo "options:"
  echo "  -h                      print this message"
  echo "  -v                      print verbose output"
  echo "  -r                      force-install rsync binary on robot"
  echo "  -c CONFIGURATION        build configuration {Debug,Release}"
  echo "  -s ANKI_ROBOT_HOST      hostname or ip address of robot"
  echo ""
  echo "environment variables:"
  echo '  $ANKI_ROBOT_HOST        hostname or ip address of robot'
  echo '  $ANKI_BUILD_TYPE        build configuration {Debug,Release}'
  echo '  $INSTALL_ROOT           root dir of installed files on target'
  echo '  $STAGING_DIR            directory that holds staged artifacts before deploy to robot'
}

function logv() {
  if [ $VERBOSE -eq 1 ]; then
    echo -n "[$SCRIPT_NAME] "
    echo $*;
  fi
}

while getopts "hvrc:s:" opt; do
  case $opt in
    h)
      usage && exit 0
      ;;
    v)
      VERBOSE=1
      ;;
    r)
      FORCE_RSYNC_BIN=1
      ;;
    c)
      ANKI_BUILD_TYPE="${OPTARG}"
      ;;
    s)
      ANKI_ROBOT_HOST="${OPTARG}"
      ;;
    *)
      usage && exit 1
      ;;
  esac
done

robot_set_host

if [ -z "${ANKI_ROBOT_HOST+x}" ]; then
  echo "ERROR: unspecified robot target. Pass the '-s' flag or set ANKI_ROBOT_HOST"
  usage
  exit 1
fi

# echo "ANKI_BUILD_TYPE: ${ANKI_BUILD_TYPE}"
echo "ANKI_ROBOT_HOST: ${ANKI_ROBOT_HOST}"
echo "   INSTALL_ROOT: ${INSTALL_ROOT}"

: ${PLATFORM_NAME:="android"}
: ${LIB_INSTALL_PATH:="${INSTALL_ROOT}/lib"}
: ${BIN_INSTALL_PATH:="${INSTALL_ROOT}/bin"}
: ${RSYNC_BIN_DIR="${TOPLEVEL}/tools/rsync"}
: ${STAGING_DIR:="${TOPLEVEL}/_build/staging/${ANKI_BUILD_TYPE}"}

# Remount rootfs read-write if necessary
MOUNT_STATE=$(\
    robot_sh "grep ' / ext4.*\sro[\s,]' /proc/mounts > /dev/null 2>&1 && echo ro || echo rw"\
)
[[ "$MOUNT_STATE" == "ro" ]] && logv "remount rw /" && robot_sh "/bin/mount -o remount,rw /"

set +e
( # TRY deploy
logv "start deploy"

set -e

logv "create target dirs"
robot_sh mkdir -p "${INSTALL_ROOT}"
robot_sh mkdir -p "${INSTALL_ROOT}/etc"
robot_sh mkdir -p "${LIB_INSTALL_PATH}"
robot_sh mkdir -p "${BIN_INSTALL_PATH}"

# install rsync binary and config if needed
logv "install rsync if necessary"
set +e
robot_sh [ -f "$DEVICE_RSYNC_BIN_DIR/rsync.bin" ]
if [ $? -ne 0 ] || [ $FORCE_RSYNC_BIN -eq 1 ]; then
  echo "loading rsync to device"
  robot_cp ${RSYNC_BIN_DIR}/rsync.bin ${DEVICE_RSYNC_BIN_DIR}/rsync.bin
fi
set -e

#
# Stop any victor services. If services are allowed to run during deployment, exe and shared library 
# files can't be released.  This may tie up enough disk space to prevent deployment of replacement files.
# 
logv "stop victor services"
robot_sh "/bin/systemctl stop victor.target"

pushd ${STAGING_DIR} > /dev/null 2>&1

#
# Use --inplace to avoid consuming temp space & minimize number of writes
# Use --delete to purge files that are no longer present in build tree
#
logv "rsync"
rsync -rlptD -uzvP \
  --inplace \
  --delete \
  --rsync-path=${DEVICE_RSYNC_BIN_DIR}/rsync.bin \
  -e ssh \
  ./anki/ root@${ANKI_ROBOT_HOST}:/${INSTALL_ROOT}/

logv "finish deploy"
) # End TRY deploy

DEPLOY_RESULT=$?
set -e

if [ $DEPLOY_RESULT -eq 0 ]; then
  logv "deploy succeeded"
else
  logv "deploy FAILED"
fi

# Remount rootfs read-write
[[ "$MOUNT_STATE" == "ro" ]] && logv "remount ro /" &&  robot_sh "/bin/mount -o remount,ro /"

popd > /dev/null 2>&1

exit $DEPLOY_RESULT
