{
  'includes': [
    '../../lib/anki/cozmo-engine/coretech/project/gyp/opencv.gypi',
    '../../lib/anki/cozmo-engine/coretech/project/gyp/face-library.gypi'
  ],

  'variables': {

    'ctrlGameEngine_source': 'ctrlGameEngine.lst',
    'ctrlKeyboard_source': 'ctrlKeyboard.lst',
    'ctrlBuildServerTest_source': 'ctrlBuildServerTest.lst',    
    'csharp_source': 'csharp.lst',
    'buildMex': '<(build-mex)',
    
    # TODO: should this be passed in, or shared?
    'coretech_defines': [
      'ANKICORETECH_USE_MATLAB=0',
      # 'ANKICORETECH_USE_GTEST=1',
      'ANKICORETECH_USE_OPENCV=1',
      'ANKICORETECH_EMBEDDED_USE_MATLAB=0',
      'ANKICORETECH_EMBEDDED_USE_GTEST=1',
      'ANKICORETECH_EMBEDDED_USE_OPENCV=1',
    ],

    'sphinx_libs': [
      'libpocketSphinx.a',
      'libsphinxad.a',
      'libsphinxBase.a',
    ],

    'cte_lib_search_path_mac_debug': [
      '<(coretech_external_path)/pocketsphinx/pocketsphinx/generated/mac/DerivedData/Debug',
    ],

    'cte_lib_search_path_mac_release': [
      '<(coretech_external_path)/pocketsphinx/pocketsphinx/generated/mac/DerivedData/Release',
    ],

    'cte_lib_search_path_ios_debug': [
      '<(coretech_external_path)/pocketsphinx/pocketsphinx/generated/ios/DerivedData/Debug-iphoneos',
    ],

    'cte_lib_search_path_ios_release': [
      '<(coretech_external_path)/pocketsphinx/pocketsphinx/generated/ios/DerivedData/Release-iphoneos',
    ],

    'webots_includes': [
      '<(webots_path)/include/controller/cpp',
    ],
    
    'compiler_flags': [
      '-Wdeprecated-declarations',
      '-fdiagnostics-show-category=name',
      '-Wall',
      '-Woverloaded-virtual',
      '-Werror',
      # '-Wundef', # Disabled until define usage is refactored to code standards
      '-Wheader-guard',
      '-fsigned-char',
      '-fvisibility-inlines-hidden',
      '-fvisibility=default',
      '-Wshorten-64-to-32',
      '-Winit-self',
      '-Wconditional-uninitialized',
      # '-Wno-deprecated-register', # Disabled until this warning actually needs to be supressed
      '-Wformat',
      '-Werror=format-security',
      '-g',
    ],
    'compiler_c_flags' : [
      '-std=c11',
      '<@(compiler_flags)',
    ],
    'compiler_cpp_flags' : [
      '-std=c++11',
      '-stdlib=libc++',
      '<@(compiler_flags)'
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
      ['OS=="android"', {
        'target_archs%': ['armveabi-v7a'],
        'compiler_flags': [
          '--sysroot=<(ndk_root)/platforms/android-18/arch-arm',
          '-DANDROID=1',
          '-gcc-toolchain', '<(ndk_root)/toolchains/arm-linux-androideabi-4.8/prebuilt/darwin-x86_64',
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
          '-I<(ndk_root)/sources/cxx-stl/llvm-libc++/libcxx/include',
          '-I<(ndk_root)/sources/cxx-stl/llvm-libc++/../llvm-libc++abi/libcxxabi/include',
          '-I<(ndk_root)/sources/cxx-stl/llvm-libc++/../../android/support/include',
          '-I<(ndk_root)/platforms/android-18/arch-arm/usr/include',
        ],
        'linker_flags': [
            '--sysroot=<(ndk_root)/platforms/android-18/arch-arm',
            '-gcc-toolchain', '<(ndk_root)/toolchains/arm-linux-androideabi-4.8/prebuilt/darwin-x86_64',
            '-no-canonical-prefixes',
            '-target armv7-none-linux-androideabi',
            '-Wl,--fix-cortex-a8',
            '-Wl,--no-undefined',
            '-Wl,-z,noexecstack',
            '-Wl,-z,relro',
            '-Wl,-z,now',
            '-mthumb',
            '-L<(ndk_root)/platforms/android-18/arch-arm/usr/lib',
            '-L<(ndk_root)/sources/cxx-stl/llvm-libc++/libs/armeabi-v7a',
            '-lgcc',
            '-lc',
            '-lm',
            '-lc++_shared',
            '-lGLESv2',
            '-llog',
        ],
      },
      { # not android
        'SHARED_LIB_DIR': '', #bogus, just to make the mac / ios builds happy
      },
      ],
      ['OS=="ios"', {
        'compiler_flags': [
        '-fobjc-arc',
        ]
      }],
      ['OS=="ios" or OS=="mac"', {
        'linker_flags': [
          '-std=c++11',
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
      'CLANG_CXX_LANGUAGE_STANDARD':'c++11',
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
                  '<@(opencv_lib_search_path_ios_debug)',
                  '<(webots_path)/lib/',
                ],
              },
            }],
            ['OS=="mac"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                  '<@(cte_lib_search_path_mac_debug)',
                  '<@(opencv_lib_search_path_mac_debug)',
                  '<(webots_path)/lib/',
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
                  '<@(opencv_lib_search_path_ios_release)',
                  '<(webots_path)/lib/',
                ],
              },
            }],
            ['OS=="mac"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                  '<@(cte_lib_search_path_mac_release)',
                  '<@(opencv_lib_search_path_mac_release)',
                  '<(webots_path)/lib/',
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
                  '<@(opencv_lib_search_path_ios_release)',
                  '<(webots_path)/lib/',
                ],
              },
            }],
            ['OS=="mac"', {
              'xcode_settings': {
                'LIBRARY_SEARCH_PATHS': [
                  '<@(cte_lib_search_path_mac_release)',
                  '<@(opencv_lib_search_path_mac_release)',
                  '<(webots_path)/lib/',
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
    },
    'conditions': [    
      [
        "OS=='ios'",
        {
          'defines': [
            'ANKI_IOS_BUILD=1',
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
        'targets': [
          {
            'target_name': 'CSharpBinding',
            'type': 'static_library',
            'include_dirs': [
              '../../unit/CSharpBinding/src',
              '<@(opencv_includes)',
            ],
            'dependencies': [
              '<(cg-ce_gyp_path):cozmoEngine',
              '<(cg-cti_gyp_path):ctiCommon',
              '<(cg-cti_gyp_path):ctiMessaging',
              '<(cg-cti_gyp_path):ctiPlanning',
              '<(cg-cti_gyp_path):ctiVision',
              '<(cg-util_gyp_path):util',
              '<(cg-util_gyp_path):jsoncpp',
            ],
            'sources': [ '<!@(cat <(csharp_source))' ],
            'libraries': [
            ],
          }, # end CSharpBinding

          {
            # fake target to see all of the sources...
            'target_name': 'all_lib_targets',
            'type': 'none',
            'dependencies': [
              'CSharpBinding',
              '<(cg-ce_gyp_path):cozmoEngine',
              '<(cg-cti_gyp_path):ctiCommon',
              '<(cg-cti_gyp_path):ctiCommonRobot',
              '<(cg-cti_gyp_path):ctiMessaging',
              '<(cg-cti_gyp_path):ctiMessagingRobot',
              '<(cg-cti_gyp_path):ctiPlanning',
              '<(cg-cti_gyp_path):ctiPlanningRobot',
              '<(cg-cti_gyp_path):ctiVision',
              '<(cg-cti_gyp_path):ctiVisionRobot',
              '<(cg-util_gyp_path):util',
              '<(cg-util_gyp_path):jsoncpp',
              '<(cg-audio_path):DriveAudioEngine',
            ]
          },
        ],
      },
    ],



    # MEX CRAP HERE
    # MEX CRAP HERE
    # MEX CRAP HERE
    # MEX CRAP HERE
    [
      "buildMex=='yes'",
      {
        'targets': [
          {
            'target_name': 'allMexTargets',
            'type': 'none',
            'dependencies': [
              '<(cg-mex_gyp_path):mexDetectFiducialMarkers',
              '<(cg-mex_gyp_path):mexUnique',
              '<(cg-mex_gyp_path):mexCameraCapture',
              '<(cg-mex_gyp_path):mexHist',
              '<(cg-mex_gyp_path):mexClosestIndex',
              '<(cg-mex_gyp_path):mexRegionprops',
              '<(cg-mex_gyp_path):mexRefineQuadrilateral',
            ],
          },
        ],
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
              'MACOSX_DEPLOYMENT_TARGET': '10.10',
              'LIBRARY_SEARCH_PATHS': [
                '<(face_library_lib_path)',
               ],
            },
        },

        'targets': [

        {
            #TODO: pass this in python configure.
            'target_name': 'AddAssetsToEngine',
            'type': 'none',
            'dependencies': [],
            'actions': [
            {
                'action_name': 'setup_dir_for_simlink',
                'inputs':[],
                'outputs':[],
                'action': [
                  'mkdir', '-p','<(cozmo_engine_path)/simulator/controllers/webotsCtrlGameEngine/resources',
                ],
            },
            {
                'action_name': 'create_symlink_resources_assets',
                'inputs': [],
                'outputs': [],
                'action': [
                  'ln',
                  '-s',
                  '-f',
                  '-n',
                  '<(cozmo_asset_path)',
                  '<(cozmo_engine_path)/simulator/controllers/webotsCtrlGameEngine/resources/assets',
                ],
              },
              {
                'action_name': 'create_symlink_resources_sound',
                'inputs': [],
                'outputs': [],
                'action': [
                  'ln',
                  '-s',
                  '-f',
                  '-n',
                  '<(externals_path)/cozmosoundbanks/GeneratedSoundBanks/Mac',
                  '<(cozmo_engine_path)/simulator/controllers/webotsCtrlGameEngine/resources/sound',
                ],
              },
            ]
          },

          # {
          #   'target_name': 'allUnitTests',
          #   'type': 'none',
          #   'dependencies': [
          #     '<(cg-ce_gyp_path):cozmoEngineUnitTest',
          #     '<(cg-cti_gyp_path):ctiUnitTest',
          #     '<(cg-util_gyp_path):UtilUnitTest',
          #   ],
          # },

          # {
          #   'target_name': 'allCoretechTools',
          #   'type': 'none',
          #   'dependencies': [
          #     '<(cg-cti_gyp_path):ctiPlanningStandalone',
          #   ],            
          # },
          #Build everything for BUILD_WORKSPACE
          # When game was a real target it was built automatically, now manually include what to build.
          {
            'target_name': 'All',
            'type': 'none',
            'dependencies': [
              'AddAssetsToEngine',
              '<(cg-ce_gyp_path):cozmoEngine',
              '<(cg-ce_gyp_path):robotClad',
              '<(cg-ce_gyp_path):cozmo_physics',
              '<(cg-ce_gyp_path):cozmoEngineUnitTest',
              '<(cg-ce_gyp_path):webotsControllers',
              '<(cg-cti_gyp_path):ctiCommon',
              '<(cg-cti_gyp_path):ctiCommonRobot',
              '<(cg-cti_gyp_path):ctiMessaging',
              '<(cg-cti_gyp_path):ctiMessagingRobot',
              '<(cg-cti_gyp_path):ctiPlanning',
              '<(cg-cti_gyp_path):ctiPlanningRobot',
              '<(cg-cti_gyp_path):ctiVision',
              '<(cg-cti_gyp_path):ctiVisionRobot',
              '<(cg-cti_gyp_path):ctiUnitTest',
              '<(cg-cti_gyp_path):ctiPlanningStandalone',
              '<(cg-util_gyp_path):util',
              '<(cg-util_gyp_path):jsoncpp',
              '<(cg-util_gyp_path):kazmath',
              '<(cg-util_gyp_path):UtilUnitTest',
              '<(cg-audio_path):DriveAudioEngine',
              #'<(cg-audio_path):CozmoFxPlugIn',
            ],
            'actions': []
          },

          {
            # fake target to see all of the sources...
            'target_name': 'all_lib_targets',
            'type': 'none',
            'dependencies': [
              '<(cg-ce_gyp_path):cozmoEngine',
              '<(cg-cti_gyp_path):ctiCommon',
              '<(cg-cti_gyp_path):ctiCommonRobot',
              '<(cg-cti_gyp_path):ctiMessaging',
              '<(cg-cti_gyp_path):ctiMessagingRobot',
              '<(cg-cti_gyp_path):ctiPlanning',
              '<(cg-cti_gyp_path):ctiPlanningRobot',
              '<(cg-cti_gyp_path):ctiVision',
              '<(cg-cti_gyp_path):ctiVisionRobot',
              '<(cg-util_gyp_path):util',
              '<(cg-util_gyp_path):jsoncpp',
              '<(cg-audio_path):DriveAudioEngine',
            ]
          },
          #Build server requires this for webots tests that require assets.
          {
            'target_name': 'webotsControllers',
            'type': 'none',
            'dependencies': [
              '<(cg-ce_gyp_path):webotsControllers',
            ]
          }


        ], # end targets
      },
    ] # end if mac


  ], #end conditions






  # CORE TARGETS HERE
  # CORE TARGETS HERE
  # CORE TARGETS HERE
  # CORE TARGETS HERE




  'targets': [

    

  ] # end targets

}
