#!/bin/sh -f
#
# platform/config/bin/vic-crashuploader.sh
#
# Victor crash file uploader
#
# This script runs as a background to periodically check for written
# crash report files, upload them, and eventually delete them.
#
# Default configuration values may be overridden by environment.
# When run from vic-crashuploader.service, environment values may
# be set in /anki/etc/vic-crashuploader.env.
#

: ${VIC_CRASH_UPLOADER_KEEP_LATEST:=30}
: ${VIC_CRASH_FOLDER:="/data/data/com.anki.victor/cache/crashDumps"}
: ${VIC_CRASH_UPLOAD_URL:='https://anki.sp.backtrace.io:6098/post?format=minidump&token=ab2d0e343f1f7baa3c6ef5fd67f95c9fb7e9de9b74a5891c38df14d3d1821ffc'}
: ${VIC_CRASH_SCRAPE_PERIOD_SEC:=30}

UPLOADED_EXT=uploaded
mkdir -p $VIC_CRASH_FOLDER

while :
do
    cd $VIC_CRASH_FOLDER > /dev/null

    for i in ./*.dmp
    do
        # Ignore file if file size is zero, or file being held by any process
        # (These are the empty crash dump files that exist while victor is running)
        if [[ -s ${i} ]]; then
            PROCESSES_HOLDING=$(fuser ${i})
            if [[ -z $PROCESSES_HOLDING ]]; then
                echo "Uploading crash dump file ${i}"
                # todo:  Remove -k after we get this to route through anki.com instead of directly to the crash reporting service
                if curl -k --data-binary @$i ${VIC_CRASH_UPLOAD_URL} ; then
                    echo "Successfully uploaded "$i

                    # Rename the file so that we can keep it around for a while before deletion
                    mv ${i} ${i}.$UPLOADED_EXT
                else
                    echo "Failed to upload "$i
                fi
            fi
        fi
    done

    # Now delete all but the oldest N crash files that have already been uploaded
    uploaded_files_total=$(ls -1 *.$UPLOADED_EXT 2>/dev/null | wc -l)
    if (( $uploaded_files_total > $VIC_CRASH_UPLOADER_KEEP_LATEST )); then
        num_files_to_delete=$(( $uploaded_files_total - $VIC_CRASH_UPLOADER_KEEP_LATEST ))
        echo "Deleting "$num_files_to_delete" crash dump file(s), but keeping the newest "$VIC_CRASH_UPLOADER_KEEP_LATEST" crash dump files"
        ls -1tr *.$UPLOADED_EXT | head -n$num_files_to_delete | xargs rm -f --
    fi

    sleep ${VIC_CRASH_SCRAPE_PERIOD_SEC}
done
