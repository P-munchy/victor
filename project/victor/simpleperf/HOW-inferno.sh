#!/usr/bin/env bash
set -eu

# Where is this script?
SCRIPTDIR=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))           

# What do we want to profile?
: ${ANKI_PROFILE_PROCNAME:="vic-engine"}

# How long do we capture? (in seconds)
: ${ANKI_PROFILE_DURATION:="10"}

# How often do we sample? (in usec)
: ${ANKI_PROFILE_FREQUENCY:="4000"}

# Where is symbol cache?
: ${ANKI_PROFILE_SYMBOLCACHE:="${SCRIPTDIR}/${ANKI_PROFILE_PROCNAME}/symbol_cache"}

# Where is top level?
: ${TOPLEVEL:="`git rev-parse --show-toplevel`"}

# Where is simpleperf?
: ${SIMPLEPERF:="${TOPLEVEL}/lib/util/tools/simpleperf"}

if [ ! -d ${ANKI_PROFILE_SYMBOLCACHE} ] ; then
  bash ${SCRIPTDIR}/make_symbol_cache.sh ${ANKI_PROFILE_SYMBOLCACHE}
fi

#
# Invoke inferno.sh to collect data and generate html report
#
${SIMPLEPERF}/inferno.sh -nc -nb \ 
  -np ${ANKI_PROFILE_PROCNAME} \ 
  -t ${ANKI_PROFILE_DURATION} \ 
  -f ${ANKI_PROFILE_FREQUENCY} \ 
  --symfs ${ANKI_PROFILE_SYMBOLCACHE} $@
