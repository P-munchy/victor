{
  'variables' : {
    'opencv_version': '3.1.0',
    'opencv_android_path': 'build/opencv-android/sdk/native/jni/include',
   'conditions': [
      [
        'OS=="ios"',
        {
	   'opencv_includes': [
	      # '<(coretech_external_path)/opencv-<(opencv_version)/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/core/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/highgui/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/imgproc/include',
	      #'<(coretech_external_path)/opencv-<(opencv_version)/modules/contrib/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/calib3d/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/objdetect/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/video/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/features2d/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/flann/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/imgcodecs/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/videoio/include',
	    ],


          'opencv_libs': [
            'opencv2.framework'
          ],

          'opencv_lib_search_path_debug': [
            '<(coretech_external_path)/build/opencv-ios',
          ],

          'opencv_lib_search_path_release': [
            '<(coretech_external_path)/build/opencv-ios',
          ],
        },

         'OS=="android"',
        {
          'opencv_includes': [
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/core',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/highgui',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/imgproc',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/calib3d',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/objdetect',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/video',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/features2d',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/flann',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/imgcodecs',
	      '<(coretech_external_path)/build/opencv-android/sdk/native/jni/include/opencv2/videoio',
	  ],

	  'opencv_libs': [
            'libzlib.a',
            'liblibjpeg.a',
            'liblibpng.a',
            'liblibtiff.a',
            'liblibjasper.a',
            'libIlmImf.a',
            'libopencv_core.a',
            'libopencv_imgproc.a',
            'libopencv_highgui.a',
            'libopencv_calib3d.a',
            #'libopencv_contrib.a',
            'libopencv_objdetect.a',
            'libopencv_video.a',
            'libopencv_features2d.a',
            'libopencv_imgcodecs.a',
            'libopencv_videoio.a',
          ],

          'opencv_lib_search_path_debug': [
            '<(coretech_external_path)/build/opencv-android/sdk/native/libs/armeabi-v7a',
          ],

          'opencv_lib_search_path_release': [
            '<(coretech_external_path)/build/opencv-android/sdk/native/libs/armeabi-v7a',
          ],
        },

        'OS=="mac"',
        {
          'opencv_includes': [
	      # '<(coretech_external_path)/opencv-<(opencv_version)/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/core/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/highgui/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/imgproc/include',
	      #'<(coretech_external_path)/opencv-<(opencv_version)/modules/contrib/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/calib3d/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/objdetect/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/video/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/features2d/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/flann/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/imgcodecs/include',
	      '<(coretech_external_path)/opencv-<(opencv_version)/modules/videoio/include',
	    ],

	 'opencv_libs': [
            'libzlib.a',
            'liblibjpeg.a',
            'liblibpng.a',
            'liblibtiff.a',
            'liblibjasper.a',
            'libIlmImf.a',
            'libopencv_core.a',
            'libopencv_imgproc.a',
            'libopencv_highgui.a',
            'libopencv_calib3d.a',
            #'libopencv_contrib.a',
            'libopencv_objdetect.a',
            'libopencv_video.a',
            'libopencv_features2d.a',
            'libopencv_imgcodecs.a',
            'libopencv_videoio.a',
          ],

          'opencv_lib_search_path_debug': [
            '<(coretech_external_path)/build/opencv-<(opencv_version)/lib/Debug',
            '<(coretech_external_path)/build/opencv-<(opencv_version)/3rdparty/lib/Debug',
          ],

          'opencv_lib_search_path_release': [
            '<(coretech_external_path)/build/opencv-<(opencv_version)/lib/Release',
            '<(coretech_external_path)/build/opencv-<(opencv_version)/3rdparty/lib/Release',
          ],
        },
      ],
    ],

  }, # variables

}

