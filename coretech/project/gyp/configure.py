#!/usr/bin/python

import os
import os.path
import sys
import re
import subprocess
import argparse
import StringIO
import shutil
import logging

#set up default logger
UtilLog = logging.getLogger('coretech.internal.configure')
stdout_handler = logging.StreamHandler()
formatter = logging.Formatter('%(name)s - %(message)s')
stdout_handler.setFormatter(formatter)
UtilLog.addHandler(stdout_handler)

def gypHelp():
  print "to install gyp:"
  print "cd ~/your_workspace/"
  print "git clone git@github.com:anki/anki-gyp.git"
  print "echo PATH=$HOME/your_workspace/gyp:$PATH >> ~/.bash_profile"
  print ". ~/.bash_profile"

def main(scriptArgs):
  version = '1.0'
  parser = argparse.ArgumentParser(description='runs gyp to generate projects', version=version)
  parser.add_argument('--debug', '--verbose', dest='verbose', action='store_true',
                      help='prints debug output')
  parser.add_argument('--path', dest='pathExtension', action='store',
                      help='prepends to the environment PATH')
  parser.add_argument('--clean', '-c', dest='clean', action='store_true',
                      help='cleans all output folders')
  parser.add_argument('--withGyp', metavar='GYP_PATH', dest='gypPath', action='store', default=None,
                      help='Use gyp installation located at GYP_PATH')
  parser.add_argument('--with-clad', metavar='CLAD_PATH', dest='cladPath', action='store', default=None,
                      help='Use clad installation located at CLAD_PATH')
  parser.add_argument('--buildTools', metavar='BUILD_TOOLS_PATH', dest='buildToolsPath', action='store', default=None,
                      help='Use build tools located at BUILD_TOOLS_PATH')
  parser.add_argument('--ankiUtil', metavar='ANKI_UTIL_PATH', dest='ankiUtilPath', action='store', default=None,
                      help='Use anki-util repo checked out at ANKI_UTIL_PATH')
  parser.add_argument('--coretechExternal', metavar='CORETECH_EXTERNAL_DIR', dest='coretechExternalPath', action='store', default=None,
                      help='Use coretech-external repo checked out at CORETECH_EXTERNAL_DIR')
  parser.add_argument('--projectRoot', dest='projectRoot', action='store', default=None,
                      help='project location, assumed to be same as git repo root')
  parser.add_argument('--updateListsOnly', dest='updateListsOnly', action='store_true', default=False,
                      help='forces configure to only update .lst files and not run gyp project generation')

  # options controlling gyp output
  parser.add_argument('--arch', action='store',
                      choices=('universal', 'standard'),
                      default='universal',
                      help="Target set of architectures")
  parser.add_argument('--platform', action='append', dest='platforms',
                      choices=('ios', 'mac', 'android', 'linux'),
                      help="Generate output for a specific platform")
  (options, args) = parser.parse_known_args(scriptArgs)

  if options.verbose:
    UtilLog.setLevel(logging.DEBUG)

  if options.platforms is None:
    options.platforms = ['ios', 'mac', 'android', 'linux']

  if (options.pathExtension):
    os.environ['PATH'] = options.pathExtension + ':' + os.environ['PATH']

  # go to the script dir, so that we can find the project dir
  # in the future replace this with command line param
  selfPath = os.path.dirname(os.path.realpath(__file__))
  os.chdir(selfPath)

  # find project root path
  if options.projectRoot:
    projectRoot = options.projectRoot
  else:
    projectRoot = os.path.join(subprocess.check_output(['git', 'rev-parse', '--show-toplevel']).rstrip("\r\n"), 'coretech')

  if not options.buildToolsPath or not os.path.exists(options.buildToolsPath):
    UtilLog.error('build tools not found [%s]' % (options.buildToolsPath) )
    return False

  sys.path.insert(0, os.path.join(options.buildToolsPath, 'tools/ankibuild'))
  import installBuildDeps
  import updateFileLists
  import util

  if not options.ankiUtilPath or not os.path.exists(options.ankiUtilPath):
    UtilLog.error('anki-util not found [%s]' % (options.ankiUtilPath) )
    return False
  ankiUtilProjectPath = os.path.join(options.ankiUtilPath, 'project/gyp/util.gyp')
  gtestPath = os.path.join(options.ankiUtilPath, 'libs/framework')

  # do not check for coretech external, and gyp if we are only updating list files
  if not options.updateListsOnly:

    if not options.coretechExternalPath or not os.path.exists(options.coretechExternalPath):
      UtilLog.error('coretech-external not found [%s]' % (options.coretechExternalPath) )
      return False
    coretechExternalPath = options.coretechExternalPath

    gypPath = os.path.join(options.buildToolsPath, 'gyp')
    if (options.gypPath is not None):
      gypPath = options.gypPath

    # import gyp
    sys.path.insert(0, os.path.join(gypPath, 'pylib'))
    import gyp

    #check gyp version
    stdOutCapture = StringIO.StringIO()
    oldStdOut = sys.stdout
    sys.stdout = stdOutCapture
    try:
        gyp.main(['--version'])
    except:
        print("Wrong version of gyp")
    sys.stdout = oldStdOut
    gypVersion = stdOutCapture.getvalue();
    if ('ANKI' not in gypVersion):
      print 'wrong version of gyp found'
      gypHelp()
      return False

  if options.clean:
    shutil.rmtree(os.path.join(projectRoot, 'generated', folder), True)
    return True

  # update file lists
  options.projectRoot = projectRoot
  generator = updateFileLists.FileListGenerator(options)
  generator.processFolder(['common/basestation/src', 'common/include', 'common/shared/src'], ['project/gyp/common.lst'])
  generator.processFolder(['common/basestation/test'], ['project/gyp/common-test.lst'])
  generator.processFolder(['common/shared/test'], ['project/gyp/common-shared-test.lst'])
  generator.processFolder(['common/robot/src', 'common/shared/src'], ['project/gyp/common-robot.lst'])
  generator.processFolder(['common/clad/src'], ['project/gyp/common-clad.lst'])
  generator.processFolder(['vision/basestation/src', 'vision/include'], ['project/gyp/vision.lst'])
  generator.processFolder(['vision/basestation/test'], ['project/gyp/vision-test.lst'])
  generator.processFolder(['vision/clad/src'], ['project/gyp/vision-clad.lst'])
  generator.processFolder(['vision/robot/src'], ['project/gyp/vision-robot.lst'])
  generator.processFolder(['planning/basestation/src', 'planning/include', 'planning/shared/src'], ['project/gyp/planning.lst'])
  generator.processFolder(['planning/basestation/test'], ['project/gyp/planning-standalone.lst'])
  generator.processFolder(['planning/basestation/test'], ['project/gyp/planning-test.lst'])
  generator.processFolder(['planning/shared/src'], ['project/gyp/planning-robot.lst'])
  generator.processFolder(['messaging/include', 'messaging/shared/src'], ['project/gyp/messaging.lst'])
  generator.processFolder(['messaging/shared/src'], ['project/gyp/messaging-robot.lst'])
  
  if options.updateListsOnly:
    # TODO: remove dependency on abspath. 
    # there is a bug due to 'os.chdir' and user passed rel path
    if (subprocess.call([os.path.abspath(os.path.join(options.ankiUtilPath, 'project/gyp/configure.py')),
     '--updateListsOnly', '--buildTools', options.buildToolsPath, '--with-clad', options.cladPath,
     '--projectRoot', options.ankiUtilPath ]) != 0):
      print "error executing anki-util configure"
      return False
    return True

  # update subprojects
  for platform in options.platforms:
    # TODO: we should pass in our own options with any additional overides..
    # subproject might need to know about the build-tools location, --verbose, and other args...
    # TODO: remove dependency on abspath. 
    # there is a bug due to 'os.chdir' and user passed rel path
    if (subprocess.call([os.path.abspath(os.path.join(options.ankiUtilPath, 'project/gyp/configure.py')),
     '--platform', platform, '--updateListsOnly', '--buildTools', options.buildToolsPath, '--with-clad', options.cladPath,
     '--projectRoot', options.ankiUtilPath ]) != 0):
      print "error executing anki-util configure"
      return False

  configurePath = os.path.join(projectRoot, 'project/gyp')
  gypFile = 'coretech-internal.gyp'

  # paths relative to gyp file
  clad_dir_rel = os.path.relpath(options.cladPath, os.path.join(options.ankiUtilPath, 'project/gyp/'))

  default_defines = {
    'kazmath_library_type': 'static_library',
    'jsoncpp_library_type': 'static_library',
    'util_library_type': 'static_library',
    'worldviz_library_type': 'static_library',
    'cpufeatures_library_type': 'static_library',
    'libwebp_library_type': 'static_library',
    'use_libwebp': 0,
    'arch_group': options.arch,
    'coretech_external_path': coretechExternalPath,
    'cti-gtest_path': gtestPath,
    'cti-util_gyp_path': ankiUtilProjectPath,
    'cti-cozmo_engine_path': subprocess.check_output(['git', 'rev-parse', '--show-toplevel']).rstrip("\r\n"),
    'clad_dir': clad_dir_rel,
    'android_toolchain': 'arm-linux-androideabi-4.9',
    'android_platform': 'android-18'
  }

  getGypArgs = util.Gyp.getArgFunction(['--check', '--depth', '.', '--toplevel-dir', '../..'])

  # mac
  if 'mac' in options.platforms:
    defines = default_defines.copy()
    defines.update({
      'OS': 'mac',
      'output_location': os.path.join(projectRoot, 'generated/mac'),
      'arch_group': 'standard',
      'ndk_root': 'INVALID'
    })
    os.environ['GYP_DEFINES'] = util.Gyp.getDefineString(defines)
    gypArgs = getGypArgs('xcode', '../../generated/mac', gypFile)
    gyp.main(gypArgs)



  # ios
  if 'ios' in options.platforms:
    defines = default_defines.copy()
    defines.update({
      'OS': 'ios',
      'output_location': os.path.join(projectRoot, 'generated/ios'),
    })
    os.environ['GYP_DEFINES'] = util.Gyp.getDefineString(defines)
    gypArgs = getGypArgs('xcode', '../../generated/ios', gypFile)
    gyp.main(gypArgs)

  # linux
  if 'linux' in options.platforms:
    print "***************************HERE-configure.py coretech"
    defines = default_defines.copy()
    defines.update({
      'OS': 'linux',
      'output_location': os.path.join(projectRoot, 'generated/linux'),
    })
    os.environ['GYP_DEFINES'] = util.Gyp.getDefineString(defines)
    gypArgs = getGypArgs('ninja', '../../generated/linux', gypFile)
    gyp.main(gypArgs)

  if 'android' in options.platforms:
    ### Install android build deps if necessary
    # TODO: We should only check for deps in configure.py, not actuall install anything
    # TODO: We should not install any deps here, only check that valid depdendencies exist
    deps = ['ninja']
    if not 'ANDROID_HOME' in os.environ:
      deps.append('android-sdk')

    if not 'ANDROID_NDK_ROOT' in os.environ:
      deps.append('android-ndk')

    if len(deps) > 0:
      # Install Android build dependencies
      options.deps = deps
      installer = installBuildDeps.DependencyInstaller(options);
      if not installer.install():
        UtilLog.error("Failed to verify build tool dependencies")
        return False

    for dep in deps:
      if dep == 'android-sdk':
        os.environ['ANDROID_HOME'] = os.path.join('/', 'usr', 'local', 'opt', dep)
      elif dep == 'android-ndk':
        os.environ['ANDROID_NDK_ROOT'] = os.path.join('/', 'usr', 'local', 'opt', dep)
    ### android deps installed

    ndk_root = os.environ['ANDROID_NDK_ROOT']

    os.environ['ANDROID_BUILD_TOP'] = configurePath

    defines = default_defines.copy()
    defines.update({
      'OS': 'android',
      'output_location': os.path.join(projectRoot, 'generated/android'),
      'das_library_type': 'shared_library',
      'os_posix': 1,
      'GYP_CROSSCOMPILE': 1,
      'target_arch': 'arm',
      'clang': 1,
      'component': 'static_library',
      'use_system_stlport': 0,
      'ndk_root': ndk_root
    })
    os.environ['GYP_DEFINES'] = util.Gyp.getDefineString(defines)

    toolchain = defines['android_toolchain']

    os.environ['CC_target'] = os.path.join(ndk_root, 'toolchains/llvm/prebuilt/darwin-x86_64/bin/clang')
    os.environ['CXX_target'] = os.path.join(ndk_root, 'toolchains/llvm/prebuilt/darwin-x86_64/bin/clang++')
    os.environ['AR_target'] = os.path.join(ndk_root, 'toolchains/%s/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-gcc-ar' % toolchain)
    os.environ['LD_target'] = os.path.join(ndk_root, 'toolchains/llvm/prebuilt/darwin-x86_64/bin/clang++')
    os.environ['NM_target'] = os.path.join(ndk_root, 'toolchains/%s/prebuilt/darwin-x86_64/arm-linux-androideabi/bin/nm' % toolchain)
    os.environ['READELF_target'] = os.path.join(ndk_root, 'toolchains/%s/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-readelf' % toolchain)
    gypArgs = getGypArgs('ninja-android', 'generated/android', gypFile)
    gyp.main(gypArgs)


  return True


if __name__ == '__main__':

  # find project root path
  args = sys.argv
  if main(args):
    sys.exit(0)
  else:
    sys.exit(1)
