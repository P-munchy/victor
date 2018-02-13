#!/usr/bin/env bash
set -eu

# Where is this script?
SCRIPTDIR=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))           

# What do we want to profile?
: ${ANKI_PROFILE_PROCNAME:="cozmoengined"}

# How long do we capture? (in seconds)
: ${ANKI_PROFILE_DURATION:="10"}

# How often do we sample? (in usec)
: ${ANKI_PROFILE_FREQUENCY:="4000"}

# Where is the symbol cache?
: ${ANKI_PROFILE_SYMBOLCACHE:="${SCRIPTDIR}/${ANKI_PROFILE_PROCNAME}/symbol_cache"}

# Where is the binary cache?
: ${ANKI_PROFILE_BINARYCACHE:="${SCRIPTDIR}/${ANKI_PROFILE_PROCNAME}/binary_cache"}

# Where is perf.data?
: ${ANKI_PROFILE_PERFDATA:="${SCRIPTDIR}/${ANKI_PROFILE_PROCNAME}/perf.data"}

# Where are the annotated files?
: ${ANKI_PROFILE_ANNOTATEDDIR:="${SCRIPTDIR}/${ANKI_PROFILE_PROCNAME}/annotated_files"}

# Where is top level?
: ${TOPLEVEL:="`git rev-parse --show-toplevel`"}

# Where is simpleperf?
: ${SIMPLEPERF:="${TOPLEVEL}/lib/util/tools/simpleperf"}

#
# If ANDROID_NDK is set, use it, else provide default location
#
if [ -z "${ANDROID_NDK+x}" ]; then
  ANDROID_NDK=`${TOPLEVEL}/tools/build/tools/ankibuild/android.py`
fi

#
# Create symbol cache
#
if [ ! -d ${ANKI_PROFILE_SYMBOLCACHE} ] ; then
  bash ${SCRIPTDIR}/make_symbol_cache.sh ${ANKI_PROFILE_SYMBOLCACHE}
fi

#
# Run app_profiler.py to start profiling.
# When it finishes it will pull a `perf.data` file off the robot.
#
# Use '-nc' because we don't need to recompile JNI.
# Use '-nb' because we use the symbol cache instead of binaries from the device.
# Use '-np' and '-r' to set collection parameters.
# Use '-lib' to fetch symbols from cache.
#
#PROFILER=${SIMPLEPERF}/app_profiler.py
#
#python ${PROFILER} -nc -nb \
#  -np ${ANKI_PROFILE_PROCNAME} \
#  -r "-e cpu-cycles:u -f ${ANKI_PROFILE_FREQUENCY} --duration ${ANKI_PROFILE_DURATION} --call-graph fp" \
#  -lib ${ANKI_PROFILE_SYMBOLCACHE} \
#  -bin ${ANKI_PROFILE_BINARYCACHE} \
#  -o ${ANKI_PROFILE_PERFDATA}
#
#
# To view perf.data, run 
#  simpleperf report --symfs symbol_cache
# which will print performance stuff to console. 
#
python ${SIMPLEPERF}/annotate.py \
  -o ${ANKI_PROFILE_ANNOTATEDDIR} \
  -i ${ANKI_PROFILE_PERFDATA} \
  -s ${TOPLEVEL} \
  --symfs ${ANKI_PROFILE_SYMBOLCACHE} \
  --addr2line ${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-addr2line \
