{
  'includes': [
    '../../coretech/project/gyp/face-library.gypi',
    '../../coretech/project/gyp/opencv.gypi',
    'text-to-speech.gypi',
    'voice-recognition.gypi',
  ],

  'variables': {

    'engine2_source': 'cozmoEngine2.lst',
    'androidHAL_source': 'androidHAL.lst',
    'cozmoAnim_source': 'cozmoAnim.lst',
    'clad_vision_source': '../../generated/clad/vision.lst',
    'clad_common_source': '../../generated/clad/common.lst',
    'clad_engine_source': '../../generated/clad/engine.lst',
    'clad_robot_source': '../../generated/clad/robot.lst',
    'clad_viz_source': '../../generated/clad/viz.lst',
    'engine_test_source': 'cozmoEngine-test.lst',
    'ctrlShared_source': 'ctrlShared.lst',
    'ctrlLightCube_source': 'ctrlLightCube.lst',
    'ctrlRobot2_source': 'ctrlRobot2.lst',
    'ctrlViz_source': 'ctrlViz.lst',
    'ctrlGameEngine2_source': 'ctrlGameEngine2.lst',
    'ctrlAnim_source': 'ctrlAnim.lst',
    'ctrlKeyboard_source': 'ctrlKeyboard.lst',
    'ctrlBuildServerTest_source': 'ctrlBuildServerTest.lst',
    'ctrlDevLog_source': 'ctrlDevLog.lst',
    'clad_source': 'clad.lst',
    'pluginPhysics_source': 'pluginPhysics.lst',
    'robot_generated_clad_source': 'robotGeneratedClad.lst',
    'audio_path_root_android': '<(ce-audio_path)/wwise/versions/current/libs/android/<(audio_library_build)',

    # TODO: should this be passed in, or shared?
    'coretech_defines': [
      'ANKICORETECH_USE_MATLAB=0',
      # 'ANKICORETECH_USE_GTEST=1',
      'ANKICORETECH_USE_OPENCV=1',
      'ANKICORETECH_EMBEDDED_USE_MATLAB=0',
      'ANKICORETECH_EMBEDDED_USE_GTEST=1',
      'ANKICORETECH_EMBEDDED_USE_OPENCV=1',
    ],

    'okao_lib_search_path': [
      '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a',
    ],

    'flatbuffers_include': [
      '<(coretech_external_path)/flatbuffers/include',
    ],

    'flatbuffers_libs': [
      'libflatbuffers.a',
    ],

    'flatbuffers_libs_android': [
      '<(coretech_external_path)/flatbuffers/android/armeabi-v7a/libflatbuffers.a',
    ],

    'flatbuffers_lib_search_path_mac': [
      '<(coretech_external_path)/flatbuffers/ios/Release',
    ],

    'flatbuffers_lib_search_path_ios': [
      '<(coretech_external_path)/flatbuffers/ios/Release',
    ],

    'routing_http_server_libs': [
      'librouting_http_server.a',
    ],

    'routing_http_server_include': [
      '<(coretech_external_path)/routing_http_server/generated/include',
    ],

    'cte_lib_search_path_mac_debug': [
      '<(coretech_external_path)/routing_http_server/generated/mac/DerivedData/Release', # NOTE WE USE RELEASE HERE INTENTIONALLY
      '<(coretech_external_path)/libarchive/project/mac/DerivedData/Debug',
    ],

    'cte_lib_search_path_mac_release': [
      '<(coretech_external_path)/routing_http_server/generated/mac/DerivedData/Release', # NOTE WE USE RELEASE HERE INTENTIONALLY
      '<(coretech_external_path)/libarchive/project/mac/DerivedData/Release',
    ],

    'cte_lib_search_path_ios_debug': [
      '<(coretech_external_path)/routing_http_server/generated/ios/DerivedData/Release-iphoneos', # NOTE WE USE RELEASE HERE INTENTIONALLY
      '<(coretech_external_path)/libarchive/project/mac/DerivedData/Debug-iphoneos',
    ],

    'cte_lib_search_path_ios_release': [
      '<(coretech_external_path)/routing_http_server/generated/ios/DerivedData/Release-iphoneos', # NOTE WE USE RELEASE HERE INTENTIONALLY
      '<(coretech_external_path)/libarchive/project/mac/DerivedData/Release-iphoneos',
    ],

    # Begin libarchive related variables
    'libarchive_libs': [
      'libarchive.a',
    ],

    'libarchive_include': [
      '<(coretech_external_path)/libarchive/project/include',
    ],

    # Make sure these are always _after_ our opencv_includes!
    'webots_includes': [
      '<(webots_path)/include/controller/cpp',
      '<(webots_path)/include/ode',
      '<(webots_path)/include',
    ],

    'das_include': [
      '../../lib/das-client/include',
      '../../lib/das-client/ios',
    ],

    'compiler_flags': [
      '-DJSONCPP_USING_SECURE_MEMORY=0',
      '-Wno-deprecated-declarations', # Suppressed until system() usage is removed
      '-fdiagnostics-show-category=name',
      '-Wall',
      '-Woverloaded-virtual',
      '-Werror',
      '-Wundef',
      '-Wheader-guard',
      '-fsigned-char',
      '-fvisibility-inlines-hidden',
      '-fvisibility=default',
      '-Wshorten-64-to-32',
      '-Winit-self',
      '-Wconditional-uninitialized',
      # '-Wno-deprecated-register', # Disabled until this warning actually needs to be suppressed
      '-Wformat',
      '-Werror=format-security',
      '-g',
    ],
    'compiler_c_flags' : [
      '-std=c11',
      '<@(compiler_flags)',
    ],
    'compiler_cpp_flags' : [
      '-std=c++14',
      '-stdlib=libc++',
      '<@(compiler_flags)',
    ],
    'linker_flags' : [
        '-g',
    ],

    # Set default ARCHS based on platform
    # We need an a new variables scope so that vars
    # can be overridden before they are used for conditionals.
    # see: http://src.chromium.org/svn/trunk/src/build/common.gypi
    'variables': {
      'arch_group%': 'universal',
    },

    # Copy overridden vars into this scope
    'arch_group%': '<(arch_group)',
    'conditions': [
      ['OS=="ios" and arch_group=="universal"', {
        'target_archs%': ['armv7', 'arm64'],
      }],
      ['OS=="ios" and arch_group=="standard"', {
        'target_archs%': ['armv7']
      }],
      ['OS=="mac" and arch_group=="universal"', {
        'target_archs%': ['$(ARCHS_STANDARD_32_64_BIT)']
      }],
      ['OS=="mac" and arch_group=="standard"', {
        'target_archs%': ['$(ARCHS_STANDARD)']
      }],
      ['OS=="linux"', {
        'target_archs%': ['x64'],
      }],
      ['OS=="android"', {
        'target_archs%': ['armveabi-v7a'],
        'target_cpu': ['arm'],
        'compiler_flags': [
          '--sysroot=<(ndk_root)/platforms/android-24/arch-arm',
          '-DANDROID=1',
          '-D__ARM_NEON=1',
          '-gcc-toolchain', '<(ndk_root)/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64',
          '-fpic',
          '-ffunction-sections',
          '-funwind-tables',
          '-fstack-protector',
          '-no-canonical-prefixes',
          '-fno-integrated-as',
          '-target', 'armv7-none-linux-androideabi',
          '-march=armv7-a',
          '-mfloat-abi=softfp',
          '-mfpu=vfpv3-d16',
          '-mthumb',
          '-fomit-frame-pointer',
          '-fno-strict-aliasing',
          '-I<(ndk_root)/sources/cxx-stl/llvm-libc++/include',
          '-I<(ndk_root)/sources/cxx-stl/llvm-libc++abi/include',
          '-I<(ndk_root)/sources/android/support/include',
          '-I<(ndk_root)/platforms/android-24/arch-arm/usr/include',
        ],
        'linker_flags': [
            '--sysroot=<(ndk_root)/platforms/android-24/arch-arm',
            '-gcc-toolchain', '<(ndk_root)/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64',
            '-no-canonical-prefixes',
            '-target armv7-none-linux-androideabi',
            '-Wl,--fix-cortex-a8',
            '-Wl,--no-undefined',
            '-Wl,-z,noexecstack',
            '-Wl,-z,relro',
            '-Wl,-z,now',
            '-mthumb',
            '-L<(ndk_root)/platforms/android-24/arch-arm/usr/lib',
            '-L<(ndk_root)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a',
            # '-L<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a',
            # '-L<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a',
            '-lgcc',
            '-lc',
            '-lm',
            '-lc++_shared',
            '-lGLESv2',
            '-llog',
            '-lz',
        ],
      },
      { # not android
        'SHARED_LIB_DIR': '', #bogus, just to make the mac / ios builds happy
      },
      ],
      ['OS=="ios" or OS=="mac"', {
        'compiler_flags': [
          '-fobjc-arc',
        ],
      }],
      ['OS=="ios" or OS=="mac" or OS=="linux"', {
        'linker_flags': [
          '-std=c++14',
          '-stdlib=libc++',
          '-lpthread',
        ]
      }],
    ],
  },

  'target_defaults': {
    'cflags': ['<@(compiler_c_flags)'],
    'cflags_cc': ['<@(compiler_cpp_flags)'],
    'ldflags': ['<@(linker_flags)'],
    'defines': ['<@(coretech_defines)'],
    'xcode_settings': {
      'OTHER_CFLAGS': ['<@(compiler_c_flags)'],
      'OTHER_CPLUSPLUSFLAGS': ['<@(compiler_cpp_flags)'],
      'ALWAYS_SEARCH_USER_PATHS': 'NO',
      # 'FRAMEWORK_SEARCH_PATHS':'../../libs/framework/',
      'CLANG_CXX_LANGUAGE_STANDARD':'c++14',
      'CLANG_CXX_LIBRARY':'libc++',
      'DEBUG_INFORMATION_FORMAT': 'dwarf',
      'GCC_DEBUGGING_SYMBOLS': 'full',
      #'GCC_PREFIX_HEADER': '../../source/anki/basestation/basestation.pch',
      #'GCC_PRECOMPILE_PREFIX_HEADER': 'YES',
      'GCC_ENABLE_SYMBOL_SEPARATION': 'YES',
      # 'GENERATE_MASTER_OBJECT_FILE': 'YES',
    },
    'configurations': {
      'Debug': {
          'conditions': [
            ['OS=="ios"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                    '<@(cte_lib_search_path_ios_debug)',
                    '<@(opencv_lib_search_path_debug)',
                    '<(webots_path)/lib/',
                    '<@(flatbuffers_lib_search_path_ios)',
                    '<@(text_to_speech_library_dirs)',
                    '<@(voice_recog_library_lib_path)',
                ],
                'FRAMEWORK_SEARCH_PATHS': [
                  '../../lib/HockeySDK-iOS/HockeySDK.embeddedframework',
                ],
              },
            }],
            ['OS=="mac"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                    '<@(cte_lib_search_path_mac_debug)',
                    '<@(opencv_lib_search_path_debug)',
                    '<(webots_path)/lib/',
                    '<@(flatbuffers_lib_search_path_mac)',
                    '<@(text_to_speech_library_dirs)',
                    '<@(voice_recog_library_lib_path)',
                ],
              },
            }],
          ],
          'cflags': ['-O0'],
          'cflags_cc': ['-O0'],
          'xcode_settings': {
            'OTHER_CFLAGS': ['-O0'],
            'OTHER_CPLUSPLUSFLAGS': ['-O0'],
            'OTHER_LDFLAGS': ['<@(linker_flags)'],
          },
          'defines': [
            '_LIBCPP_DEBUG=0',
            'DEBUG=1',
          ],
      },
      'Profile': {
          'conditions': [
            ['OS=="ios"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                    '<@(cte_lib_search_path_ios_release)',
                    '<@(opencv_lib_search_path_release)',
                    '<(webots_path)/lib/',
                    '<@(flatbuffers_lib_search_path_ios)',
                    '<@(text_to_speech_library_dirs)',
                    '<@(voice_recog_library_lib_path)',
                ],
                'FRAMEWORK_SEARCH_PATHS': [
                  '../../lib/HockeySDK-iOS/HockeySDK.embeddedframework',
                ],
              },
            }],
            ['OS=="mac"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                    '<@(cte_lib_search_path_mac_release)',
                    '<@(opencv_lib_search_path_release)',
                    '<(webots_path)/lib/',
                    '<@(flatbuffers_lib_search_path_mac)',
                    '<@(text_to_speech_library_dirs)',
                    '<@(voice_recog_library_lib_path)',
                ],
              },
            }],
          ],
          'cflags': ['-Os'],
          'cflags_cc': ['-Os'],
          'xcode_settings': {
            'OTHER_CFLAGS': ['-Os'],
            'OTHER_CPLUSPLUSFLAGS': ['-Os'],
            'OTHER_LDFLAGS': ['<@(linker_flags)'],
          },
          'defines': [
            'NDEBUG=1',
            'PROFILE=1',
          ],
      },
      'Release': {
          'conditions': [
            ['OS=="ios"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                    '<@(cte_lib_search_path_ios_release)',
                    '<@(opencv_lib_search_path_release)',
                    '<(webots_path)/lib/',
                    '<@(flatbuffers_lib_search_path_ios)',
                    '<@(text_to_speech_library_dirs)',
                    '<@(voice_recog_library_lib_path)',
                ],
                 'FRAMEWORK_SEARCH_PATHS': [
                  '../../lib/HockeySDK-iOS/HockeySDK.embeddedframework',
                ],
              },
            }],
            ['OS=="mac"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                    '<@(cte_lib_search_path_mac_release)',
                    '<@(opencv_lib_search_path_release)',
                    '<(webots_path)/lib/',
                    '<@(flatbuffers_lib_search_path_mac)',
                    '<@(text_to_speech_library_dirs)',
                    '<@(voice_recog_library_lib_path)',
                ],
              },
            }],
          ],
          'cflags': ['-Os'],
          'cflags_cc': ['-Os'],
          'xcode_settings': {
            'OTHER_CFLAGS': ['-Os'],
            'OTHER_CPLUSPLUSFLAGS': ['-Os'],
            'OTHER_LDFLAGS': ['<@(linker_flags)'],
          },
          'defines': [
            'NDEBUG=1',
            'RELEASE=1',
          ],
      },
      'Shipping': {
          'conditions': [
            ['OS=="ios"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                    '<@(cte_lib_search_path_ios_release)',
                    '<@(opencv_lib_search_path_release)',
                    '<(webots_path)/lib/',
                    '<@(text_to_speech_library_dirs)',
                    '<@(flatbuffers_lib_search_path_ios)',
                    '<@(voice_recog_library_lib_path)',
                ],
                 'FRAMEWORK_SEARCH_PATHS': [
                  '../../lib/HockeySDK-iOS/HockeySDK.embeddedframework',
                ],
             },
            }],
            ['OS=="mac"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                    '<@(cte_lib_search_path_mac_release)',
                    '<@(opencv_lib_search_path_release)',
                    '<(webots_path)/lib/',
                    '<@(text_to_speech_library_dirs)',
                    '<@(voice_recog_library_lib_path)',
                ],
              },
            }],
          ],
          'cflags': ['-Os'],
          'cflags_cc': ['-Os'],
          'xcode_settings': {
            'OTHER_CFLAGS': ['-Os'],
            'OTHER_CPLUSPLUSFLAGS': ['-Os'],
            'OTHER_LDFLAGS': ['<@(linker_flags)'],
          },
          'defines': [
            'NDEBUG=1',
            'SHIPPING=1',
          ],
      },
    },
    'conditions': [
      [
        "OS=='ios'",
        {
          'defines': [
            'USE_DAS=1',
          ],
        },
      ],
      [
        "OS=='android'",
        {
          'defines': [
            'USE_DAS=1',
          ],
        },
      ],
      [
        "OS=='mac'",
        {
          'defines': [
            'USE_DAS=0',
          ],
        },
      ],
    ],
  },

  'conditions': [
    [
      "OS=='android'",
      {
        'defines': [
          'ANDROID=1',
          '__ARM_NEON=1',
        ],
      },
    ],
    [
      "OS=='ios'",
      {
        'xcode_settings': {
          'SDKROOT': 'iphoneos',
          'VALID_ARCHS' : ['<@(target_archs)']
          #'TARGETED_DEVICE_FAMILY': '1,2',
          #'CODE_SIGN_IDENTITY': 'iPhone Developer',
          #'IPHONEOS_DEPLOYMENT_TARGET': '5.1',
        },
      },
    ],



    # UNITTEST CRAP HERE
    # UNITTEST CRAP HERE
    # UNITTEST CRAP HERE
    # UNITTEST CRAP HERE

    [
      "OS=='mac'",
      {
        'target_defaults': {
          'variables': {
            'mac_target_archs': [ '$(ARCHS_STANDARD)' ]
          },
          'xcode_settings': {
            'ARCHS': [ '>@(mac_target_archs)' ],
            'SDKROOT': 'macosx',
            'MACOSX_DEPLOYMENT_TARGET': '<(macosx_deployment_target)',
            'LIBRARY_SEARCH_PATHS': [
              '<(face_library_lib_path)',
            ],
          },
        },


        'targets': [

          {
            'target_name': 'cozmoEngine2_resources',
            'type': 'none',
            'actions': [
              {
                'action_name': 'cozmoEngine2_resources_create_symlink',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/config',
                  '--link_name', '<(PRODUCT_DIR)/resources/config',
                  '--create_folder', '<(PRODUCT_DIR)/resources'
                ],
              },
            ],
          },

          {
            'target_name': 'cozmo_physics',
            'type': 'shared_library',
            'include_dirs': [
              '../../include',
              '<@(opencv_includes)',
              '<@(webots_includes)', # After opencv!
            ],
            'dependencies': [
              'cozmoEngine2',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiMessaging',
              '<(ce-util_gyp_path):util',
            ],
            'sources': [
              '<!@(cat <(pluginPhysics_source))',
            ],
            'defines': [
              'MACOS',
            ],
            'cflags': [
              '-Wno-undef'
            ],
            'xcode_settings': {
              'OTHER_CFLAGS': ['-Wno-undef'],
            },
            'libraries': [
              'libCppController.dylib',
              'libode.dylib',
              '<@(opencv_libs)',
              '$(SDKROOT)/System/Library/Frameworks/OpenGL.framework',
              '$(SDKROOT)/System/Library/Frameworks/GLUT.framework',
              '$(SDKROOT)/System/Library/Frameworks/Foundation.framework',
            ],
          }, # end cozmo_physics

          {
            'target_name': 'webotsCtrlLightCube',
            'type': 'executable',
            'include_dirs': [
              '../../robot/include',
              '../../include',
              '../..',
              '<@(webots_includes)',
            ],
            'dependencies': [
              'robotClad',
              '<(ce-util_gyp_path):util',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiCommonRobot',
            ],
            'sources': [
              '<!@(cat <(ctrlLightCube_source))',
              '<!@(cat <(ctrlShared_source))'
            ],
            'defines': [
              'COZMO_ROBOT',
              'SIMULATOR'
            ],
            'libraries': [
              'libCppController.dylib',
              '$(SDKROOT)/System/Library/Frameworks/Foundation.framework',
            ],
          }, # end controller Block

          {
            'target_name': 'webotsCtrlViz',
            'type': 'executable',
            'include_dirs': [
              '<@(opencv_includes)',
              '<@(webots_includes)', # After opencv!
              '<@(flatbuffers_include)',
              '../../include',
              '../../robot/include',
            ],
            'dependencies': [
              'cozmoEngine2',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiMessaging',
              '<(ce-util_gyp_path):util',
              '<(ce-util_gyp_path):kazmath',
            ],
            'sources': [
              '<!@(cat <(ctrlViz_source))',
              '<!@(cat <(ctrlShared_source))'
              ],
            'defines': [
              # 'COZMO_ROBOT',
              # 'SIMULATOR'
            ],
            'libraries': [
              'libCppController.dylib',
              '<@(opencv_libs)',
              '$(SDKROOT)/System/Library/Frameworks/Security.framework',      # Why is this needed for Viz?
              '$(SDKROOT)/System/Library/Frameworks/AppKit.framework',        # Why is this needed for Viz?
              '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',  # Why is this needed for Viz?
              '$(SDKROOT)/System/Library/Frameworks/CoreAudio.framework',     # Why is this needed for Viz?
              '$(SDKROOT)/System/Library/Frameworks/AudioUnit.framework',     # Why is this needed for Viz?
              '<@(flatbuffers_libs)',
              '<@(routing_http_server_libs)',                                 # Why is this needed for Viz?
            ],
            'conditions': [
              # For some reason, need to link directly against FacioMetric libs
              # when using them for recognition, which also means they have to be
              # present (symlinked) in the executable dir
              ['face_library == "faciometric"', {
                'libraries': [
                  '<@(face_library_libs)',
                ],
                'actions' : [
                  {
                    'action_name': 'create_symlink_webotsCtrlViz_faciometricLibs',
                    'inputs': [ ],
                      'outputs': [ ],
                      'action': [
                        '../../tools/build/tools/ankibuild/symlink.py',
                        '--link_target', '<(face_library_lib_path)',
                        '--link_name', '../../simulator/controllers/webotsCtrlViz/'
                      ],
                  },
                ], # actions
              }], # conditions
            ],
          }, # end controller viz

          {
            'target_name': 'webotsCtrlRobot2',
            'type': 'executable',
            'include_dirs': [
              '<@(opencv_includes)',
              '<@(webots_includes)', # After opencv!
              '../../robot2/hal/include',
              '../../robot2/hal/sim/include', 
              '../../robot/include',
              '../../robot/generated',
              '../../robot/supervisor/src',
              '../../include',
              '../..',
            ],
            'dependencies': [
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiCommonRobot',
              '<(ce-cti_gyp_path):ctiVisionRobot',
              '<(ce-cti_gyp_path):ctiMessagingRobot',
              '<(ce-cti_gyp_path):ctiPlanningRobot',
              '<(ce-util_gyp_path):util',
              'robotClad',
            ],
            'sources': [
              '<!@(cat <(ctrlRobot2_source))',
              '<!@(cat <(ctrlShared_source))'
              ],
            'defines': [
              'COZMO_ROBOT',
              'COZMO_V2',
              'SIMULATOR',
              '_DEBUG'
            ],
            'libraries': [
              'libCppController.dylib',
              '<@(opencv_libs)',
              '$(SDKROOT)/System/Library/Frameworks/AppKit.framework',
            ],
            'actions': [
              {
                    'action_name': 'Robot pre-build steps',
                    'inputs': [],
                    'outputs': [],
                    'action': [
                        'make', '-C', '../../robot/', 'sim', 'BUILD_TYPE=SIMULATOR'
                    ],
                }
            ]
          }, # end controller Robot 2

          {
            'target_name': 'webotsCtrlGameEngine2',
            'type': 'executable',
            'include_dirs': [
              '<@(opencv_includes)',
              '<@(webots_includes)', # After opencv!
              '<@(flatbuffers_include)',
              '<@(text_to_speech_include_dirs)',
              '<@(routing_http_server_include)',
              '<@(voice_recog_library_includes)',
            ],
            'dependencies': [
              'cozmoEngine2',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiCommonRobot',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiVisionRobot',
              '<(ce-util_gyp_path):util',
              '<(ce-util_gyp_path):kazmath',
              '<(ce-util_gyp_path):jsoncpp',
            ],
            'sources': [
              '<!@(cat <(ctrlGameEngine2_source))',
              '<!@(cat <(ctrlShared_source))'
              ],
            'defines': [
              'COZMO_V2',
              'SIMULATOR'
            ],
            'libraries': [
              'libCppController.dylib',
              '$(SDKROOT)/System/Library/Frameworks/Security.framework',
              '$(SDKROOT)/System/Library/Frameworks/Cocoa.framework',
              '$(SDKROOT)/System/Library/Frameworks/AppKit.framework',
              '$(SDKROOT)/System/Library/Frameworks/QTKit.framework',
              '$(SDKROOT)/System/Library/Frameworks/QuartzCore.framework',
              '$(SDKROOT)/System/Library/Frameworks/OpenAL.framework',
              '$(SDKROOT)/System/Library/Frameworks/CoreBluetooth.framework',
              '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',
              '<@(flatbuffers_libs)',
              '<@(text_to_speech_libraries)',
              '<@(opencv_libs)',
              '<@(face_library_libs)',
              '<@(routing_http_server_libs)',
              '<@(voice_recog_library_libs)',
            ],

            #Force linked due to objective-C categories.
            'xcode_settings': {
              'OTHER_LDFLAGS': ['-force_load <(coretech_external_path)/routing_http_server/generated/mac/DerivedData/Release/librouting_http_server.a'],
            },


            'actions': [
              {
                'action_name': 'create_symlink_webotsCtrlEnginefaceLibraryLibs',
                'inputs': [ ],
                'outputs': [ ],

                'conditions': [
                  ['face_library=="faciometric"', {
                    'action': [
                      '../../tools/build/tools/ankibuild/symlink.py',
                      '--link_target', '<(face_library_lib_path)',
                      '--link_name', '../../simulator/controllers/webotsCtrlGameEngine2/'
                    ],
                  }],
                  ['face_library=="facesdk"', {
                    'action': [
                      '../../tools/build/tools/ankibuild/symlink.py',
                      '--link_target', '<(face_library_lib_path)/libfsdk.dylib',
                      '--link_name', '../../simulator/controllers/webotsCtrlGameEngine2/'
                    ],
                  }],
                  ['face_library=="opencv" or face_library=="okao"', {
                    'action': [
                    'echo',
                    'dummyOpenCVGameAction',
                    ],
                  }],
                ], # conditions
              },
            ], # actions

            'conditions': [
              [
                'OS=="ios" or OS=="mac"',
                {
                  'libraries': [
                    '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',
                    '$(SDKROOT)/System/Library/Frameworks/CoreAudio.framework',
                    '$(SDKROOT)/System/Library/Frameworks/AudioUnit.framework',
                  ],
                },
              ],
            ],
          }, # end controller Game Engine 2


          {
            'target_name': 'webotsCtrlAnim',
            'type': 'executable',
            'include_dirs': [
              '<@(opencv_includes)',
              '<@(webots_includes)', # After opencv!
              '<@(flatbuffers_include)',
              '<@(text_to_speech_include_dirs)',
              '<@(routing_http_server_include)',
            ],
            'dependencies': [
              'cozmoAnim',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiCommonRobot',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiVisionRobot',
              '<(ce-util_gyp_path):util',
              '<(ce-util_gyp_path):kazmath',
              '<(ce-util_gyp_path):jsoncpp',
            ],
            'sources': [
              '<!@(cat <(ctrlAnim_source))',
              '<!@(cat <(ctrlShared_source))'
              ],
            'defines': [
              'COZMO_V2',
              'SIMULATOR'
            ],
            'libraries': [
              'libCppController.dylib',
              '$(SDKROOT)/System/Library/Frameworks/Security.framework',
              '$(SDKROOT)/System/Library/Frameworks/Cocoa.framework',
              '$(SDKROOT)/System/Library/Frameworks/AppKit.framework',
              '$(SDKROOT)/System/Library/Frameworks/QTKit.framework',
              '$(SDKROOT)/System/Library/Frameworks/QuartzCore.framework',
              '$(SDKROOT)/System/Library/Frameworks/OpenAL.framework',
              '$(SDKROOT)/System/Library/Frameworks/CoreBluetooth.framework',
              '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',
              '<@(flatbuffers_libs)',
              '<@(text_to_speech_libraries)',
              '<@(opencv_libs)',
              '<@(face_library_libs)',
              '<@(routing_http_server_libs)',
            ],

            #Force linked due to objective-C categories.
            'xcode_settings': {
              'OTHER_LDFLAGS': ['-force_load <(coretech_external_path)/routing_http_server/generated/Mac/DerivedData/Release/librouting_http_server.a'],
            },

            'conditions': [
              [
                'OS=="mac"',
                {
                  'libraries': [
                    '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',
                    '$(SDKROOT)/System/Library/Frameworks/CoreAudio.framework',
                    '$(SDKROOT)/System/Library/Frameworks/AudioUnit.framework',
                  ],
                },
              ],
            ],
          }, # end controller Anim


          {
            'target_name': 'webotsCtrlKeyboard',
            'type': 'executable',
            'include_dirs': [
              '<(cti-cozmo_engine_path)/simulator/include',
              '<@(opencv_includes)',
              '<@(webots_includes)', # After opencv!
              '<@(flatbuffers_include)',
              '../../coretech/generated/clad/vision',
            ],
            'dependencies': [
              'cozmoEngine2',
              '<(ce-util_gyp_path):util',
              '<(ce-util_gyp_path):kazmath',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiMessaging',
              '<(ce-cti_gyp_path):ctiPlanning',
            ],
            'sources': [
              '<!@(cat <(ctrlKeyboard_source))',
              '<!@(cat <(ctrlShared_source))'
              ],
            'libraries': [
              'libCppController.dylib',
              '<@(flatbuffers_libs)',
              '<@(opencv_libs)',
              '$(SDKROOT)/System/Library/Frameworks/Security.framework',      # Why is this needed for KB controller?
              '$(SDKROOT)/System/Library/Frameworks/AppKit.framework',        # Why is this needed for KB controller?
              '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',  # Why is this needed for KB controller?
              '$(SDKROOT)/System/Library/Frameworks/CoreAudio.framework',     # Why is this needed for KB controller?
              '$(SDKROOT)/System/Library/Frameworks/AudioUnit.framework',     # Why is this needed for KB controller?
              '<@(routing_http_server_libs)',                                 # Why is this needed for KB controller?
            ],
            'conditions': [
              # For some reason, need to link directly against FacioMetric libs
              # when using them for recognition, which also means they have to be
              # present (symlinked) in the executable dir
              ['face_library == "faciometric"', {
                'libraries': [
                  '<@(face_library_libs)',
                ],
                'actions' : [
                  {
                    'action_name': 'create_symlink_webotsCtrlKeyboard_faciometricLibs',
                    'inputs': [ ],
                    'outputs': [ ],
                    'action': [
                      '../../tools/build/tools/ankibuild/symlink.py',
                      '--link_target', '<(face_library_lib_path)',
                      '--link_name', '../../simulator/controllers/webotsCtrlKeyboard/'
                    ],
                  },
                ], # actions
              }], # conditions
            ],
          }, # end controller Keyboard

          {
            'target_name': 'webotsCtrlBuildServerTest',
            'type': 'executable',
            'include_dirs': [
              '<(cti-cozmo_engine_path)/simulator/include',
              '<@(opencv_includes)',
              '<@(webots_includes)', # After opencv!
              '<@(flatbuffers_include)',
              '../../coretech/generated/clad/vision',
            ],
            'dependencies': [
              'cozmoEngine2',
              '<(ce-util_gyp_path):util',
              '<(ce-util_gyp_path):kazmath',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiMessaging',
              '<(ce-cti_gyp_path):ctiPlanning',
            ],
            'sources': [
              '<!@(cat <(ctrlBuildServerTest_source))',
              '<!@(cat <(ctrlShared_source))'
            ],
            'libraries': [
              'libCppController.dylib',
              '<@(flatbuffers_libs)',
              '<@(opencv_libs)',
              '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',
              '$(SDKROOT)/System/Library/Frameworks/CoreAudio.framework',
              '$(SDKROOT)/System/Library/Frameworks/AppKit.framework',
            ],
          }, # end controller Keyboard

          {
            'target_name': 'webotsCtrlDevLog',
            'type': 'executable',
            'include_dirs': [
              '<(cti-cozmo_engine_path)/simulator/include',
              '<@(opencv_includes)',
              '<@(webots_includes)', # After opencv!
              '../../coretech/generated/clad/vision',
            ],
            'dependencies': [
              'cozmoEngine2',
              '<(ce-util_gyp_path):util',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiMessaging',
              '<(ce-cti_gyp_path):ctiPlanning',
            ],
            'sources': [ '<!@(cat <(ctrlDevLog_source))' ],
            'libraries': [
              'libCppController.dylib',
              '<@(opencv_libs)',
              '$(SDKROOT)/System/Library/Frameworks/Foundation.framework',
            ],
          }, # end controller DevLog

          {
            'target_name': 'webotsControllers',
            'type': 'none',
            'dependencies': [
              'webotsCtrlKeyboard',
              'webotsCtrlBuildServerTest',
              'webotsCtrlGameEngine2',
              'webotsCtrlAnim',
              'webotsCtrlRobot2',
              'webotsCtrlViz',
              'webotsCtrlLightCube',
              'webotsCtrlDevLog',
              'cozmo_physics',
            ],

            # Create symlinks to controller binaries
            # For some reason this is necessary in order to be able to attach to their processes from Xcode.
            'actions': [

              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # webotsCtrlKeyboard
              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # controller binary
              {
                'action_name': 'create_symlink_webotsCtrlKeyboard',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(PRODUCT_DIR)/webotsCtrlKeyboard',
                  '--link_name', '../../simulator/controllers/webotsCtrlKeyboard/webotsCtrlKeyboard'
                ],
              },
              # create symlink to config, so that webotsCtrlKeyboard can also load json configuration files
              # shared with other controllers
              {
                'action_name': 'create_symlink_resources_configs_webotsCtrlKeyboard',
                'inputs':[], 'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/config',
                  '--link_name', '../../simulator/controllers/webotsCtrlKeyboard/resources/config',
                  '--create_folder', '../../simulator/controllers/webotsCtrlKeyboard/resources'
                ],
              },

              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # webotsCtrlBuildServerTest
              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # controller binary
              {
                'action_name': 'create_symlink_webotsCtrlBuildServerTest',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(PRODUCT_DIR)/webotsCtrlBuildServerTest',
                  '--link_name', '../../simulator/controllers/webotsCtrlBuildServerTest/webotsCtrlBuildServerTest'
                ],
              },

              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # webotsCtrlGameEngine2
              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              {
                'action_name': 'create_symlink_webotsCtrlGameEngine2',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(PRODUCT_DIR)/webotsCtrlGameEngine2',
                  '--link_name', '../../simulator/controllers/webotsCtrlGameEngine2/webotsCtrlGameEngine2'
                ],
              },
              {
                'action_name': 'create_symlink_resources_configs2',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/config',
                  '--link_name', '../../simulator/controllers/webotsCtrlGameEngine2/resources/config',
                  '--create_folder', '../../simulator/controllers/webotsCtrlGameEngine2/resources'
                ],
              },
              {
                'action_name': 'create_symlink_resources_test2',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/test',
                  '--link_name', '../../simulator/controllers/webotsCtrlGameEngine2/resources/test'
                ],
              },
              {
                'action_name': 'create_symlink_resources_tts2',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '../../generated/<(OS)/resources/tts',
                  '--link_name', '../../simulator/controllers/webotsCtrlGameEngine2/resources/tts'
                ],
              },



              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # webotsCtrlAnim
              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              {
                'action_name': 'create_symlink_webotsCtrlAnim',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(PRODUCT_DIR)/webotsCtrlAnim',
                  '--link_name', '../../simulator/controllers/webotsCtrlAnim/webotsCtrlAnim'
                ],
              },
              {
                'action_name': 'create_symlink_resources_configs_webotsCtrlAnim',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/config',
                  '--link_name', '../../simulator/controllers/webotsCtrlAnim/resources/config',
                  '--create_folder', '../../simulator/controllers/webotsCtrlAnim/resources'
                ],
              },
              {
                'action_name': 'create_symlink_resources_test_webotsCtrlAnim',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/test',
                  '--link_name', '../../simulator/controllers/webotsCtrlAnim/resources/test'
                ],
              },



            # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
            # webotsCtrlRobot2
            # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
            # controller binary
            {
              'action_name': 'create_symlink_webotsCtrlRobot2',
              'inputs':[],
              'outputs':[],
              'action': [
                '../../tools/build/tools/ankibuild/symlink.py',
                '--link_target', '<(PRODUCT_DIR)/webotsCtrlRobot2',
                '--link_name', '../../simulator/controllers/webotsCtrlRobot2/webotsCtrlRobot2'
              ],
            },
            # create symlink to config, so that webotsCtrlRobot2 can also load json configuration files
            # shared with other controllers
            {
              'action_name': 'create_symlink_resources_configs_webotsCtrlRobot2',
              'inputs':[], 'outputs':[],
              'action': [
                '../../tools/build/tools/ankibuild/symlink.py',
                '--link_target', '<(cozmo_engine_path)/resources/config',
                '--link_name', '../../simulator/controllers/webotsCtrlRobot2/resources/config',
                '--create_folder', '../../simulator/controllers/webotsCtrlRobot2/resources'
              ],
            },


              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # webotsCtrlViz
              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              {
                'action_name': 'create_symlink_webotsCtrlViz',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(PRODUCT_DIR)/webotsCtrlViz',
                  '--link_name', '../../simulator/controllers/webotsCtrlViz/webotsCtrlViz'
                ],
              },
              # create symlink to config, so that webotsCtrlViz can also load json configuration files
              # shared with other controllers
              {
                'action_name': 'create_symlink_resources_configs_webotsCtrlViz',
                'inputs':[], 'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/config',
                  '--link_name', '../../simulator/controllers/webotsCtrlViz/resources/config',
                  '--create_folder', '../../simulator/controllers/webotsCtrlViz/resources'
                ],
              },

              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # webotsCtrlLightCube
              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              {
                'action_name': 'create_symlink_webotsCtrlLightCube',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(PRODUCT_DIR)/webotsCtrlLightCube',
                  '--link_name', '../../simulator/controllers/webotsCtrlLightCube/webotsCtrlLightCube'
                ],
              },
              # create symlink to config, so that webotsCtrlLightCube can also load json configuration files
              # shared with other controllers
              {
                'action_name': 'create_symlink_resources_configs_webotsCtrlLightCube',
                'inputs':[], 'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/config',
                  '--link_name', '../../simulator/controllers/webotsCtrlLightCube/resources/config',
                  '--create_folder', '../../simulator/controllers/webotsCtrlLightCube/resources'
                ],
              },

              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # webotsPluginPhysics
              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              {
                'action_name': 'create_symlink_webotsPluginPhysics',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(PRODUCT_DIR)/libcozmo_physics.dylib',
                  '--link_name', '../../simulator/plugins/physics/cozmo_physics/libcozmo_physics.dylib'
                ],
              },

              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              # webotsCtrlDevLog
              # - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
              {
                'action_name': 'create_symlink_webotsCtrlDevLog',
                'inputs':[],
                'outputs':[],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(PRODUCT_DIR)/webotsCtrlDevLog',
                  '--link_name', '../../simulator/controllers/webotsCtrlDevLog/webotsCtrlDevLog'
                ],
              }
            ], # actions

          }, # end webotsControllers


          {
            'target_name': 'cozmoEngineUnitTest',
            'type': 'executable',
            'include_dirs': [
              '../../test/engine',
              '../../robot/include',
              '<@(opencv_includes)',
              '<@(flatbuffers_include)',
              '../../coretech/generated/clad/vision',
              '<@(webots_includes)',
            ],
            'dependencies': [
              'cozmoEngine2',
              'cozmoEngine2_resources',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiCommonRobot',
              '<(ce-cti_gyp_path):ctiMessaging',
              '<(ce-cti_gyp_path):ctiPlanning',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiVisionRobot',
              '<(ce-util_gyp_path):jsoncpp',
              '<(ce-util_gyp_path):kazmath',
              '<(ce-util_gyp_path):util',
              '<(cg-audio_path):AudioEngine',
            ],
            'defines': [
              'SIMULATOR'
            ], 
            'sources': [ '<!@(cat <(engine_test_source))' ],
            'sources/': [
              ['exclude', 'run_pc_embeddedTests.cpp'],
              ['exclude', 'run_m4_embeddedTests.cpp'],
              ['exclude', 'resaveBlockImages.m'],
            ],
            'cflags': [
              '-Wno-undef'
            ],
            'cflags_cc': [
              '-Wno-undef'
            ],
            'xcode_settings': {
              'OTHER_CFLAGS': ['-Wno-undef'],
              'OTHER_CPLUSPLUSFLAGS': ['-Wno-undef'],
              'FRAMEWORK_SEARCH_PATHS':'<(ce-gtest_path)',
            },
            'libraries': [
              '<(ce-gtest_path)/gtest.framework',
              '$(SDKROOT)/System/Library/Frameworks/Security.framework',
              '$(SDKROOT)/System/Library/Frameworks/Cocoa.framework',
              '$(SDKROOT)/System/Library/Frameworks/AppKit.framework',
              '$(SDKROOT)/System/Library/Frameworks/QTKit.framework',
              '$(SDKROOT)/System/Library/Frameworks/QuartzCore.framework',
              '<@(flatbuffers_libs)',
              '<@(opencv_libs)',
              '<@(face_library_libs)',
              'libCppController.dylib',
            ],

            'conditions': [
              [
                'OS=="ios" or OS=="mac"',
                {
                  'libraries': [
                    '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',
                    '$(SDKROOT)/System/Library/Frameworks/CoreAudio.framework',
                    '$(SDKROOT)/System/Library/Frameworks/AudioUnit.framework',
                  ],
                },
              ],
            ],
            'actions': [

              # These have empty inputs and outputs and are instead in the action
              # so gyp doesn't think that they're dupes
              {
                'action_name': 'create_symlink_resources_test',
                'inputs': [],
                'outputs': [],
                'action': [
                  '../../tools/build/tools/ankibuild/symlink.py',
                  '--link_target', '<(cozmo_engine_path)/resources/test',
                  '--link_name', '<(PRODUCT_DIR)/resources/test'
                ],
              },

  	          {
                'action_name': 'create_symlink_engineUnitTestfaceLibraryLibs',
                'inputs':[],
                'outputs':[],
                'conditions': [
                  ['face_library=="faciometric"', {
                    'action': [
                      '../../tools/build/tools/ankibuild/symlink.py',
                      '--link_target', '<(face_library_lib_path)',
                      '--link_name', '<(PRODUCT_DIR)/'
                    ],
                  }],
                  ['face_library=="facesdk"', {
                    'action': [
                      '../../tools/build/tools/ankibuild/symlink.py',
                      '--link_target', '<(face_library_lib_path)/libfsdk.dylib',
                      '--link_name', '<(PRODUCT_DIR)/'
                    ],
                  }],
                  ['face_library=="opencv" or face_library=="okao"', {
                    'action': [
                    'echo',
                    'dummyOpenCVEngineAction',
                    ],
                  }],
                ], # conditions
              },

            ], #end actions
          }, # end unittest target


          {
            'target_name': 'cozmoEngine2',
            'sources': [
              '<!@(cat <(engine2_source))',
              '<!@(cat <(clad_engine_source))',
              '<!@(cat <(clad_common_source))',
              '<!@(cat <(clad_vision_source))',
              '<!@(cat <(clad_robot_source))',
              '<!@(cat <(clad_viz_source))',
              '<!@(cat <(clad_source))',
            ],
            'sources/': [
              ['exclude', 'bleRobotManager.mm'],
              ['exclude', 'bleComms.mm'],
            ],
            'include_dirs': [
              '../..',
              '../../robot/include',
              '../../generated/clad/engine',
              '../../coretech/generated/clad/vision',
              '<@(opencv_includes)',
              '<@(flatbuffers_include)',
              '<@(text_to_speech_include_dirs)',
              '<@(routing_http_server_include)',
              '../../generated/clad/game',
              '<@(libarchive_include)',
              '<@(das_include)',
              '<@(voice_recog_library_includes)',
            ],
            'direct_dependent_settings': {
              'include_dirs': [
                '../..',
                '../../androidHAL',
                '../../include',
                '../../robot/include',
                '../../generated/clad/engine',
                '../../generated/clad/game',
              ],
              'defines': [
                'COZMO_BASESTATION',
                'COZMO_V2',
              ],
            },
            'defines': [
              'COZMO_BASESTATION',
              'COZMO_V2',
            ],
            'dependencies': [
              '<(ce-util_gyp_path):util',
              '<(ce-util_gyp_path):audioUtil',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiMessaging',
              '<(ce-cti_gyp_path):ctiPlanning',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiCommonRobot',
              '<(ce-cti_gyp_path):ctiVisionRobot',
              '<(cg-audio_path):AudioEngine',
              '<(ce-ble_cozmo_path):BLECozmo',
              '<(ce-das_path):DAS',
              'androidHAL'
            ],
            'conditions': [
              [
                'OS=="mac"',
                {
                  'type': 'static_library',
                  'defines': [
                    'SIMULATOR'
                  ],
                  'direct_dependent_settings': {
                    'defines': [
                      'SIMULATOR'
                    ]
                  },
                  'sources/': [
                    ['exclude', '.androidHAL/src/anki/cozmo/.'],
                    ['exclude', '_android\\.'],
                    ['exclude', '(linux|android)'],
                    ['exclude', '../../engine/cozmoAPI/csharp-binding/ios'],
                  ],
                  'include_dirs' : [
                    '<@(webots_includes)', # After opencv!
                  ],
                  'libraries': [
                    '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',
                    '$(SDKROOT)/System/Library/Frameworks/CoreAudio.framework',
                    '$(SDKROOT)/System/Library/Frameworks/AudioUnit.framework',
                    '<@(flatbuffers_libs)',
                    '<@(text_to_speech_libraries)',
                    '<@(routing_http_server_libs)',
                    '<@(libarchive_libs)',
                    '<@(voice_recog_library_libs)',
                  ],

                  'actions': [
                  {
                    'action_name': 'create_symlink_resources_tts2',
                    'inputs':['<(text_to_speech_resources_tts)'],
                    'outputs':['../../generated/<(OS)/resources/tts'],
                    'action': [
                      '../../tools/build/tools/ankibuild/symlink.py',
                      '--link_target', '<(_inputs)',
                      '--link_name', '<(_outputs)',
                      '--create_folder', '../../generated/<(OS)/resources'
                    ],
                  }, # end action
                ], # end actions
              }], # end condition
        
            ] # end conditions

          }, # end engine2 target


          {
            'target_name': 'androidHAL',
            'type': 'static_library',
            'sources': [
              '<!@(cat <(androidHAL_source))'
            ],
            'include_dirs': [
              '../..',
              '../../robot/include',
              '../../generated/clad/engine',
            ],
            'dependencies': [
              '<(ce-util_gyp_path):util',
              '<(ce-cti_gyp_path):ctiCommon',
              '<(ce-cti_gyp_path):ctiVision',
              '<(ce-cti_gyp_path):ctiCommonRobot',
              '<(ce-cti_gyp_path):ctiVisionRobot',
            ],
            'defines': [
              'COZMO_BASESTATION',
              'COZMO_V2',
            ],
            'conditions': [
              [
                'OS=="mac"',
                {
                  'defines': [
                    'SIMULATOR'
                  ],
                  'direct_dependent_settings': {
                    'defines': [
                      'SIMULATOR'
                    ]
                  },
                  'sources/': [
                    ['exclude', 'android/'],
                    ['exclude', '_android\\.'],
                  ],
                  'include_dirs' : [
                    '<@(opencv_includes)',
                    '<@(webots_includes)', # After opencv!
                  ],
              },
              'OS=="android"',
              {
                  'sources/': [
                    ['exclude', 'mac/'],
                    ['exclude', '_mac\\.'],
                  ],
                  'include_dirs': [
                    '../../androidHAL/android/camera',
                  ],
                  'libraries': [
                    '-llog',
                    '-lOpenSLES',
                    '-landroid',
                    '-lcamera2ndk',
                    '-lmediandk',
                  ]
              }
            ]
          ],
          },
            

        ], # end targets
      },
    ], # end if mac

  ], #end conditions






  # CORE TARGETS HERE
  # CORE TARGETS HERE
  # CORE TARGETS HERE
  # CORE TARGETS HERE

  'targets': [
    {
      'target_name': 'cozmoAnim',
      'sources': [
      '<!@(cat <(cozmoAnim_source))',
      #'<!@(cat <(clad_engine_source))',
      #'<!@(cat <(clad_robot_source))',
      '<!@(cat <(clad_common_source))',
      '<!@(cat <(clad_vision_source))',
      #'<!@(cat <(clad_viz_source))',
      #'<!@(cat <(clad_source))',
      ],
      'include_dirs': [
      '../../animProcess/src',
      '../../robot/include',
      '../../robot/generated/clad/robot',     # For clad
      #'../../generated/clad/engine',
      '../../coretech/generated/clad/vision',
      '<@(opencv_includes)',
      '<@(flatbuffers_include)',
      '<@(text_to_speech_include_dirs)',
      '<@(routing_http_server_include)',
      '<@(das_include)',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
        '../../animProcess/src',
        '../../robot/include',
        '../../robot/generated/clad/robot',  # for clad
        #'../../generated/clad/engine',
        ],
        'defines': [
          'COZMO_BASESTATION',
          'COZMO_V2',
        ],
      },
      'defines': [
        'COZMO_BASESTATION',
        'COZMO_V2',
      ],
      'dependencies': [
      '<(ce-util_gyp_path):util',
      '<(ce-util_gyp_path):audioUtil',
      '<(ce-cti_gyp_path):ctiCommon',
      #'<(ce-cti_gyp_path):ctiMessaging',
      '<(ce-cti_gyp_path):ctiVision',
      
      #'<(ce-cti_gyp_path):ctiCommonRobot',
      #'<(ce-cti_gyp_path):ctiVisionRobot',
      '<(ce-cti_gyp_path):ctiMessagingRobot',

      '<(cg-audio_path):AudioEngine',
      '<(ce-das_path):DAS',
      'robotClad'
      ],
      'conditions': [
      [
      'OS=="mac"',
      {
        'type': 'static_library',
        'defines': [
        'SIMULATOR'
        ],
        'direct_dependent_settings': {
          'defines': [
          'SIMULATOR'
          ]
        },
        'sources/': [
        ['exclude', '_android\\.'],
        ['exclude', '(linux)'],
        ['exclude', 'main.cpp']
        ],
        'include_dirs' : [
          '<@(webots_includes)', # After opencv!
        ],
        'libraries': [
        '$(SDKROOT)/System/Library/Frameworks/AudioToolbox.framework',
        '$(SDKROOT)/System/Library/Frameworks/CoreAudio.framework',
        '$(SDKROOT)/System/Library/Frameworks/AudioUnit.framework',
        '<@(flatbuffers_libs)',
        '<@(text_to_speech_libraries)',
        '<@(routing_http_server_libs)',
        ],
      },
      'OS=="android"',
      {
        #'type': 'shared_library',
        'type': 'executable',
        'library_dirs':
        [
        #these are empty?!?!
        #'<(opencv_lib_search_path_debug)',
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a',
        ],
        
        'libraries': [ # why is this in #if android? shouldn't mac and ios have same libs to link against?
        
        # Necessary for ctiVision?
        '-Wl,--whole-archive',
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkao.a',      # Common
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoCo.a',    #
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoPc.a',    # Property Estimation (for Smile/Gaze/Blink?)
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoPt.a',    # Face Parts Detection
        '-Wl,--no-whole-archive',
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoDt.a',    # Face Detection
        #'<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoPt.a',    # Face Parts Detection (Had to move this to --whole-archive section for some reason)
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoEx.a',    # Facial Expression estimation
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoFr.a',    # Face Recognition
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOmcvPd.a',    # Pet Detection
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoSm.a',    # Smile Estimation
        '<(coretech_external_path)/okaoVision/lib/Android/armeabi-v7a/libeOkaoGb.a',    # Gaze & Blink Estimation

        '<(coretech_external_path)/libarchive/project/android/DerivedData/libarchive.a',
        '<(crash_path)/Breakpad/libs/armeabi-v7a/libbreakpad_client.a',   # Google Breakpad
        # does not work with ninja?!?!
        #'<@(face_library_libs)',
        # '<@(opencv_libs)',
        '<@(flatbuffers_libs_android)',
        '<@(text_to_speech_libraries)',
        '<(coretech_external_path)/build/opencv-android/o4a/3rdparty/lib/armeabi-v7a/libIlmImf.a',
        '<(coretech_external_path)/build/opencv-android/o4a/3rdparty/lib/armeabi-v7a/liblibjasper.a',
        #'<(coretech_external_path)/build/opencv-android/o4a/3rdparty/lib/armeabi-v7a/liblibjpeg.a',
        '<(coretech_external_path)/libjpeg-turbo/android_armv7_libs/libturbojpeg.a',
        '<(coretech_external_path)/build/opencv-android/o4a/3rdparty/lib/armeabi-v7a/liblibpng.a',
        '<(coretech_external_path)/build/opencv-android/o4a/3rdparty/lib/armeabi-v7a/liblibtiff.a',
        '<(coretech_external_path)/build/opencv-android/o4a/3rdparty/lib/armeabi-v7a/liblibwebp.a',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libtbb.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_calib3d.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_core.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_features2d.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_flann.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_highgui.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_imgcodecs.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_imgproc.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_ml.so',
        '<(coretech_external_path)/build/opencv-android/OpenCV-android-sdk/sdk/native/libs/armeabi-v7a/libopencv_videoio.so',
        
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libCommunicationCentral.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkStreamMgr.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkMusicEngine.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkSoundEngine.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkMemoryMgr.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkDelayFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkRoomVerbFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkSilenceSource.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkCompressorFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkPeakLimiterFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkParametricEQFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkHarmonizerFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkMeterFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkFlangerFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkGainFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkToneSource.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkVorbisDecoder.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkTimeStretchFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkSineSource.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkExpanderFX.a',
        '<(ce-audio_path)/wwise/versions/current/libs/android/debug/libAkGuitarDistortionFX.a',
        '-llog',
        '-lOpenSLES',
        '-landroid',
        ],
        'sources/': [
          ['exclude', '(ios|linux|mac)']
        ],
        'include_dirs': [
          '<(crash_path)/Breakpad/include/breakpad',
          '<@(flatbuffers_include)',
          '../../include/anki/cozmo',
        ],
        'defines': [
          'USE_GOOGLE_BREAKPAD=1',
        ],
        # Compile as position-independent executable (PIE)
        'cflags': [
          '-fPIE'
        ],
        'cflags_cc': [
          '-fPIE'
        ],
        'ldflags': [
          '-fPIE',
          '-pie'
        ],
      }
      ],
      ] #'conditions'
      
    }, # end cozmoAnim target

    {
      'target_name': 'robotClad',
      'sources': [
        '<!@(cat <(robot_generated_clad_source))',
      ],
      'include_dirs': [
        '../../robot2/generated/clad/robot',
        '../../robot/generated/clad/robot',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '../../robot2/generated/clad/robot',
          '../../robot/generated/clad/robot',
        ],
      },
      'dependencies': [
      ],
      'type': 'static_library',
    },


  ] # end targets

}
