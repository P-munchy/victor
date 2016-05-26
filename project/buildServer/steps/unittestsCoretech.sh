#!/usr/bin/env bash
set -e
#set -x
TESTNAME=cti
PROJECTNAME=coretech-internal
PROJECTROOT=
# change dir to the project dir, no matter where script is executed from
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo "Entering directory \`${DIR}'"
cd $DIR

GIT=`which git`
if [ -z $GIT ];then
  echo git not found
  exit 1
fi
TOPLEVEL=`$GIT rev-parse --show-toplevel`
BUILDTOOLS=$TOPLEVEL/tools/build

# prepare
PROJECT=$TOPLEVEL/$PROJECTROOT/generated/mac
BUILD_TYPE="Debug"
DERIVED_DATA=$PROJECT/DerivedData
GTEST=$TOPLEVEL/lib/util/libs/framework/

echo "Entering directory \`$TOPLEVEL/coretech/project/gyp/'"

# build
xcodebuild \
-project $PROJECT/coretech/project/gyp/$PROJECTNAME.xcodeproj \
-target ${TESTNAME}UnitTest \
-sdk macosx \
-configuration $BUILD_TYPE  \
SYMROOT="$DERIVED_DATA" \
OBJROOT="$DERIVED_DATA" \
build 


set -o pipefail
set +e

# clean output
rm -rf $DERIVED_DATA/$BUILD_TYPE/${TESTNAME}GoogleTest*
rm -rf $DERIVED_DATA/$BUILD_TYPE/case*
rm -f $DERIVED_DATA/$BUILD_TYPE/*.txt

DUMP_OUTPUT=0
ARGS=""
while (( "$#" )); do

    if [[ "$1" == "-x" ]]; then
        DUMP_OUTPUT=1
    else
        if [[ "$ARGS" == "" ]]; then
            ARGS="--gtest_filter=$1"
        else
            ARGS="$ARGS $1"
        fi
    fi
    shift
done

if (( \! $DUMP_OUTPUT )); then
    ARGS="$ARGS --silent"
fi

# execute
$BUILDTOOLS/tools/ankibuild/multiTest.py \
--path $DERIVED_DATA/$BUILD_TYPE \
--gtest_path "$GTEST" \
--work_path "$DERIVED_DATA/$BUILD_TYPE/" \
--config_path "$DERIVED_DATA/$BUILD_TYPE/" \
--gtest_output "xml:$DERIVED_DATA/$BUILD_TYPE/${TESTNAME}GoogleTest_.xml" \
--executable ${TESTNAME}UnitTest \
--stdout_file \
--xml_dir "$DERIVED_DATA/$BUILD_TYPE" \
--xml_basename ${TESTNAME}GoogleTest_ \
$ARGS

EXIT_STATUS=$?
set -e

if (( $DUMP_OUTPUT )); then
    cat $DERIVED_DATA/$BUILD_TYPE/*.txt
fi

#tarball files together
cd $DERIVED_DATA/$BUILD_TYPE
tar czf ${TESTNAME}GoogleTest.tar.gz ${TESTNAME}GoogleTest_*

# exit
exit $EXIT_STATUS
