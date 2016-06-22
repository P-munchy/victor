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
UtilLog = logging.getLogger('cozmo.engine.configure')
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
  parser.add_argument('--debug', '--verbose', '-d', dest='verbose', action='store_true',
                      help='prints debug output')
  parser.add_argument('--path', dest='pathExtension', action='store',
                      help='prepends to the environment PATH')
  parser.add_argument('--clean', '-c', dest='clean', action='store_true',
                      help='cleans all output folders')
  parser.add_argument('--mex', '-m', dest='mex', action='store_true',
                      help='builds mathlab\'s mex project')
  parser.add_argument('--withGyp', metavar='GYP_PATH', dest='gypPath', action='store', default=None,
                      help='Use gyp installation located at GYP_PATH')
  parser.add_argument('--with-clad', metavar='CLAD_PATH', dest='cladPath', action='store', default=None,
                        help='Use clad installation located at CLAD_PATH')  
  parser.add_argument('--buildTools', metavar='BUILD_TOOLS_PATH', dest='buildToolsPath', action='store', default=None,
                      help='Use build tools located at BUILD_TOOLS_PATH')
  parser.add_argument('--ankiUtil', metavar='ANKI_UTIL_PATH', dest='ankiUtilPath', action='store', default=None,
                      help='Use anki-util repo checked out at ANKI_UTIL_PATH')
  parser.add_argument('--webots', metavar='WEBOTS_PATH', dest='webotsPath', action='store', default=None,
                      help='Use webots aplication at WEBOTS_PATH')
  parser.add_argument('--coretechExternal', metavar='CORETECH_EXTERNAL_DIR', dest='coretechExternalPath', action='store', default=None,
                      help='Use coretech-external repo checked out at CORETECH_EXTERNAL_DIR')
  parser.add_argument('--coretechInternal', metavar='CORETECH_INTERNAL_DIR', dest='coretechInternalPath', action='store', default=None,
                      help='Use coretech-internal repo checked out at CORETECH_INTERNAL_DIR')
  parser.add_argument('--audio', metavar='AUDIO_PATH', dest='audioPath', action='store', default=None,
                      help='Use audio repo checked out at AUDIO_PATH')
  parser.add_argument('--bleCozmo', metavar='BLE_COZMO_PATH', dest='bleCozmoPath', action='store', default=None,
                      help='Use BLECozmo repo checked out at BLE_COZMO_PATH')
  parser.add_argument('--das', metavar='DAS_PATH', dest='dasPath', action='store', default=None,
                      help='Use das-client repo checked out at DAS_PATH')
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
    projectRoot = subprocess.check_output(['git', 'rev-parse', '--show-toplevel']).rstrip("\r\n")

  if not options.buildToolsPath:
    options.buildToolsPath = os.path.join(options.projectRoot, 'tools/build')
  if not os.path.exists(options.buildToolsPath):
    UtilLog.error('build tools not found [%s]' % (options.buildToolsPath) )
    return False

  sys.path.insert(0, os.path.join(options.buildToolsPath, 'tools/ankibuild'))
  import installBuildDeps
  import updateFileLists
  import util

  if not options.coretechInternalPath:
    options.coretechInternalPath = os.path.join(options.projectRoot, 'coretech')
  if not os.path.exists(options.coretechInternalPath):
    UtilLog.error('coretech-internal not found [%s]' % (options.coretechInternalPath) )
    return False
  coretechInternalProjectPath = os.path.join(options.coretechInternalPath, 'project/gyp/coretech-internal.gyp')

  if not options.ankiUtilPath:
    options.ankiUtilPath = os.path.join(options.projectRoot, 'lib/util')
  if not os.path.exists(options.ankiUtilPath):
    UtilLog.error('anki-util not found [%s]' % (options.ankiUtilPath) )
    return False
  ankiUtilProjectPath = os.path.join(options.ankiUtilPath, 'project/gyp/util.gyp')
  gtestPath = os.path.join(options.ankiUtilPath, 'libs/framework')

  if not options.webotsPath:
    options.webotsPath = '/Applications/Webots'
  if not os.path.exists(options.webotsPath):
    UtilLog.error('webots not found [%s]' % options.webotsPath)
    return False
  webotsPath = options.webotsPath

  if not options.audioPath:
    options.audioPath = os.path.join(options.projectRoot, 'lib/audio')
  if not os.path.exists(options.audioPath):
    UtilLog.error('audio path not found [%s]' % options.audioPath)
    return False
  audioProjectPath = options.audioPath
  audioProjectGypPath = os.path.join(audioProjectPath, 'gyp/audioengine.gyp')

  if not options.bleCozmoPath:
    options.bleCozmoPath = os.path.join(options.projectRoot, 'lib/BLECozmo')
  if not os.path.exists(options.bleCozmoPath):
    UtilLog.error('BLECozmo path not found [%s]' % options.bleCozmoPath)
    return False
  bleCozmoProjectPath = os.path.join(options.bleCozmoPath, 'project/gyp/BLECozmo.gyp')

  if not options.dasPath:
    options.dasPath = os.path.join(options.projectRoot, 'lib/das-client')
  if not os.path.exists(options.dasPath):
    UtilLog.error('das-client not found [%s]' % (options.dasPath) )
    return False
  dasProjectPath = os.path.join(options.dasPath, 'gyp/das-client.gyp')

  # do not check for coretech external, and gyp if we are only updating list files
  if not options.updateListsOnly:

    if not options.coretechExternalPath or not os.path.exists(options.coretechExternalPath):
      UtilLog.error('coretech-external not found [%s]' % (options.coretechExternalPath) )
      return False
    coretechExternalPath = options.coretechExternalPath

    gypPath = os.path.join(options.projectRoot, 'tools/gyp')
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
        UtilLog.error("Wrong version of gyp")
    sys.stdout = oldStdOut
    gypVersion = stdOutCapture.getvalue();
    #subprocess.check_output([gypLocation, '--version']).rstrip("\r\n")
    if ('ANKI' not in gypVersion):
      UtilLog.error('wrong version of gyp found')
      gypHelp()
      return False

  if options.clean:
    shutil.rmtree(os.path.join(projectRoot, 'generated', folder), True)
    return True

  #run engine clad's make
  if (subprocess.call(['make', '--silent', 'cleanCppListFiles', 'all'], cwd=os.path.join(projectRoot, 'clad')) != 0):
    UtilLog.error("error compiling clad files")
    return False

  #run robot clad's make
  if (subprocess.call(['make', '--silent'], cwd=os.path.join(projectRoot, 'robot/clad')) != 0):
    UtilLog.error("error compiling clad files")
    return False
    
  #generate AnkiLogStringTables.json
  if (subprocess.call(['make', '--silent', 'app'], cwd=os.path.join(projectRoot, 'robot')) != 0):
      UtilLog.error("Error generating AnkiLogStringTables.json")
      return False

  # update file lists
  generator = updateFileLists.FileListGenerator(options)
  generator.processFolder(['basestation/src/anki/cozmo', 'basestation/include/anki/cozmo', 'include', 'resources'],
   ['project/gyp/cozmoEngine.lst'])
  generator.processFolder(['cozmoAPI/src/anki/cozmo', 'cozmoAPI/include' ], ['project/gyp/cozmoAPI.lst'])
  generator.processFolder(['basestation/test', 'robot/test'], ['project/gyp/cozmoEngine-test.lst'])
  generator.processFolder(['robot/sim_hal', 'robot/supervisor/src', 'simulator/src/robot', 'simulator/controllers/webotsCtrlRobot'],
   ['project/gyp/ctrlRobot.lst'], ['reliableSequenceId.c'])
  generator.processFolder(['robot/generated/clad/robot'], ['project/gyp/robotGeneratedClad.lst'])
  generator.processFolder(['simulator/controllers/webotsCtrlViz'], ['project/gyp/ctrlViz.lst'])
  generator.processFolder(['simulator/controllers/webotsCtrlKeyboard', 'simulator/src/game'], ['project/gyp/ctrlKeyboard.lst'])
  generator.processFolder(['simulator/controllers/webotsCtrlBuildServerTest', 'simulator/src/game'], ['project/gyp/ctrlBuildServerTest.lst'])
  generator.processFolder(['simulator/controllers/webotsCtrlGameEngine'], ['project/gyp/ctrlGameEngine.lst'])
  generator.processFolder(['clad/src', 'clad/vizSrc', 'robot/clad/src'], ['project/gyp/clad.lst'])
  webotsPhysicsPath = os.path.join(projectRoot, 'generated/webots/src/plugins/physics/')
  # copy the webots' physics.c into the generated folder
  util.File.mkdir_p(webotsPhysicsPath)
  util.File.cp(os.path.join(webotsPath, 'resources/projects/plugins/physics/physics.c'), webotsPhysicsPath)
  generator.processFolder(['simulator/plugins/physics/cozmo_physics', webotsPhysicsPath], ['project/gyp/pluginPhysics.lst'],['libcozmo_physics.dylib'])
  # this is too big of a scope, we need to manualy maintain ctrlLightCube.lst for now
  # generator.processFolder(['simulator/controllers/block_controller', 'robot/sim_hal/', 'robot/supervisor/src/'], ['project/gyp/ctrlLightCube.lst'])
  
  if options.updateListsOnly:
    if (subprocess.call([os.path.join(options.coretechInternalPath, 'project/gyp/configure.py'),
     '--updateListsOnly', '--ankiUtil', options.ankiUtilPath, '--buildTools', options.buildToolsPath,
     '--with-clad', options.cladPath ]) != 0):
      UtilLog.error("error executing submodule configure")
      return False
    return True

  # update subprojects
  for platform in options.platforms:
    # TODO: we should pass in our own options with any additional overides..
    # subproject might need to know about the build-tools location, --verbose, and other args...
    if (subprocess.call([os.path.join(options.coretechInternalPath, 'project/gyp/configure.py'),
     '--platform', platform, '--ankiUtil', options.ankiUtilPath, '--updateListsOnly', '--buildTools', options.buildToolsPath,
     '--with-clad', options.cladPath ]) != 0):
      UtilLog.error("error executing submodule configure")
      return False

  configurePath = os.path.join(projectRoot, 'project/gyp')
  coretechInternalConfigurePath = os.path.join(options.coretechInternalPath, 'project/gyp')
  gypFile = 'cozmoEngine.gyp'
  # gypify path names
  gtestPath = os.path.relpath(gtestPath, configurePath)
  ctiGtestPath = os.path.relpath(gtestPath, coretechInternalConfigurePath)
  ankiUtilProjectPath = os.path.relpath(ankiUtilProjectPath, configurePath)
  ctiAnkiUtilProjectPath = os.path.relpath(ankiUtilProjectPath, coretechInternalConfigurePath)
  coretechInternalProjectPath = os.path.relpath(coretechInternalProjectPath, configurePath)
  audioProjectGypPath = os.path.relpath(audioProjectGypPath, configurePath)
  #audioProjectPath = os.path.relpath(options.audioPath, configurePath)
  audioProjectPath = os.path.abspath(os.path.join(configurePath, options.audioPath))
  bleCozmoProjectPath = os.path.relpath(bleCozmoProjectPath, configurePath)
  dasProjectPath = os.path.relpath(dasProjectPath, configurePath)
  # paths relative to gyp file
  clad_dir_rel = os.path.relpath(options.cladPath, os.path.join(options.ankiUtilPath, 'project/gyp/'))

  # mac
  if 'mac' in options.platforms:
      os.environ['GYP_DEFINES'] = """
                                  OS=mac
                                  ndk_root=INVALID
                                  audio_library_type=static_library
                                  audio_library_build=profile
                                  kazmath_library_type=static_library
                                  jsoncpp_library_type=static_library
                                  util_library_type=static_library
                                  worldviz_library_type=static_library
                                  das_library_type=static_library
                                  arch_group={0}
                                  output_location={1}
                                  coretech_external_path={2}
                                  webots_path={3}
                                  cti-gtest_path={4}
                                  cti-util_gyp_path={5}
                                  cozmo_engine_path={6}
                                  cti-cozmo_engine_path={6}
                                  ce-gtest_path={7}
                                  ce-util_gyp_path={8}
                                  ce-cti_gyp_path={9}
                                  ce-audio_path={10}
                                  cg-audio_path={11}
                                  ce-ble_cozmo_path={12}
                                  ce-das_path={13}
                                  clad_dir={14}
                                  """.format(
                                    options.arch, 
                                    os.path.join(options.projectRoot, 'generated/mac'),
                                    coretechExternalPath, 
                                    webotsPath,
                                    ctiGtestPath, 
                                    ctiAnkiUtilProjectPath,
                                    projectRoot,
                                    gtestPath, 
                                    ankiUtilProjectPath, 
                                    coretechInternalProjectPath,
                                    audioProjectGypPath,
                                    audioProjectGypPath,
                                    bleCozmoProjectPath,
                                    dasProjectPath,
                                    clad_dir_rel,
                                  )
      gypArgs = ['--check', '--depth', '.', '-f', 'xcode', '--toplevel-dir', '../..', '--generator-output', '../../generated/mac', gypFile]
      gyp.main(gypArgs)
      # mac
      if options.mex:
        gypArgs = ['--check', '--depth', '.', '-f', 'xcode', '--toplevel-dir', '../..', '--generator-output', '../../generated/mac', 'cozmoEngineMex.gyp']
        gyp.main(gypArgs)
      



  # ios
  if 'ios' in options.platforms:
    os.environ['GYP_DEFINES'] = """
                                OS=ios
                                audio_library_type=static_library
                                audio_library_build=profile
                                kazmath_library_type=static_library
                                jsoncpp_library_type=static_library
                                util_library_type=static_library
                                worldviz_library_type=static_library
                                das_library_type=static_library
                                arch_group={0}
                                output_location={1}
                                coretech_external_path={2}
                                webots_path={3}
                                cti-gtest_path={4}
                                cti-util_gyp_path={5}
                                cti-cozmo_engine_path={6}
                                ce-gtest_path={7}
                                ce-util_gyp_path={8}
                                ce-cti_gyp_path={9}
                                ce-audio_path={10}
                                cg-audio_path={11}
                                ce-ble_cozmo_path={12}
                                ce-das_path={13}
                                clad_dir={14}
                                """.format(
                                  options.arch, 
                                  os.path.join(options.projectRoot, 'generated/ios'),
                                  coretechExternalPath, 
                                  webotsPath,
                                  ctiGtestPath, 
                                  ctiAnkiUtilProjectPath,
                                  projectRoot,
                                  gtestPath, 
                                  ankiUtilProjectPath, 
                                  coretechInternalProjectPath,
                                  audioProjectGypPath,
                                  audioProjectGypPath,
                                  bleCozmoProjectPath,
                                  dasProjectPath,
                                  clad_dir_rel,
                                )
    gypArgs = ['--check', '--depth', '.', '-f', 'xcode', '--toplevel-dir', '../..', '--generator-output', '../../generated/ios', gypFile]
    gyp.main(gypArgs)


  # mex
  if 'mex' in options.platforms:
      gypFile = 'cozmoEngineMex.gyp'
      os.environ['GYP_DEFINES'] = """
                                  OS=mac
                                  ndk_root=INVALID
                                  audio_library_type=static_library
                                  audio_library_build=profile
                                  kazmath_library_type=static_library
                                  jsoncpp_library_type=static_library
                                  util_library_type=static_library
                                  worldviz_library_type=static_library
                                  das_library_type=static_library
                                  arch_group={0}
                                  output_location={1}
                                  coretech_external_path={2}
                                  webots_path={3}
                                  cti-gtest_path={4}
                                  cti-util_gyp_path={5}
                                  cti-cozmo_engine_path={6}
                                  ce-gtest_path={7}
                                  ce-util_gyp_path={8}
                                  ce-cti_gyp_path={9}
                                  ce-audio_path={10}
                                  ce-ble_cozmo_path={11}
                                  ce-das_path={12}
                                  clad_dir={13}
                                  """.format(
                                    options.arch, 
                                    os.path.join(options.projectRoot, 'generated/mex'),
                                    coretechExternalPath, 
                                    webotsPath,
                                    ctiGtestPath, 
                                    ctiAnkiUtilProjectPath,
                                    projectRoot,
                                    gtestPath,
                                    ankiUtilProjectPath,
                                    coretechInternalProjectPath,
                                    audioProjectGypPath,
                                    bleCozmoProjectPath,
                                    dasProjectPath,
                                    clad_dir_rel,
                                  )
      gypArgs = ['--check', '--depth', '.', '-f', 'xcode', '--toplevel-dir', '../..', '--generator-output', '../../generated/mex', gypFile]
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
    ##################### GYP_DEFINES ####
    os.environ['GYP_DEFINES'] = """
                                audio_library_type=static_library
                                audio_library_build=profile
                                kazmath_library_type=static_library
                                jsoncpp_library_type=static_library
                                util_library_type=static_library
                                worldviz_library_type=static_library
                                das_library_type=shared_library
                                os_posix=1
                                OS=android
                                GYP_CROSSCOMPILE=1
                                target_arch=arm
                                clang=1
                                component=static_library
                                use_system_stlport=0
                                arch_group={0}
                                output_location={1}
                                coretech_external_path={2}
                                webots_path={3}
                                cti-gtest_path={4}
                                cti-util_gyp_path={5}
                                cti-cozmo_engine_path={6}
                                ce-gtest_path={7}
                                ce-util_gyp_path={8}
                                ce-cti_gyp_path={9}
                                ndk_root={10}
                                ce-audio_path={11}
                                cg-audio_path={12}
                                ce-ble_cozmo_path={13}
                                ce-das_path={14}
                                clad_dir={15}
                                """.format(
                                  options.arch, 
                                  os.path.join(options.projectRoot, 'generated/android'),
                                  coretechExternalPath, 
                                  webotsPath,
                                  ctiGtestPath, 
                                  ctiAnkiUtilProjectPath,
                                  projectRoot,
                                  gtestPath, 
                                  ankiUtilProjectPath, 
                                  coretechInternalProjectPath,
                                  ndk_root,
                                  audioProjectPath,
                                  audioProjectGypPath,
                                  bleCozmoProjectPath,
                                  dasProjectPath,
                                  clad_dir_rel,
                                )
    os.environ['CC_target'] = os.path.join(ndk_root, 'toolchains/llvm-3.5/prebuilt/darwin-x86_64/bin/clang')
    os.environ['CXX_target'] = os.path.join(ndk_root, 'toolchains/llvm-3.5/prebuilt/darwin-x86_64/bin/clang++')
    os.environ['AR_target'] = os.path.join(ndk_root, 'toolchains/arm-linux-androideabi-4.8/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-gcc-ar')
    os.environ['LD_target'] = os.path.join(ndk_root, 'toolchains/llvm-3.5/prebuilt/darwin-x86_64/bin/clang++')
    os.environ['NM_target'] = os.path.join(ndk_root, 'toolchains/arm-linux-androideabi-4.8/prebuilt/darwin-x86_64/arm-linux-androideabi/bin/nm')
    os.environ['READELF_target'] = os.path.join(ndk_root, 'toolchains/arm-linux-androideabi-4.8/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-readelf')
    gypArgs = ['--check', '--depth', '.', '-f', 'ninja-android', '--toplevel-dir', '../..', '--generator-output', 'generated/android', gypFile]
    gyp.main(gypArgs)



  # linux
  if 'linux' in options.platforms:
      print "***********************HERE-configure.py"
      os.environ['GYP_DEFINES'] = """
                                  OS=linux
                                  ndk_root=INVALID
                                  audio_library_type=static_library
                                  audio_library_build=profile
                                  kazmath_library_type=static_library
                                  jsoncpp_library_type=static_library
                                  util_library_type=static_library
                                  worldviz_library_type=static_library
                                  das_library_type=static_library
                                  arch_group={0}
                                  output_location={1}
                                  coretech_external_path={2}
                                  webots_path={3}
                                  cti-gtest_path={4}
                                  cti-util_gyp_path={5}
                                  cozmo_engine_path={6}
                                  cti-cozmo_engine_path={6}
                                  ce-gtest_path={7}
                                  ce-util_gyp_path={8}
                                  ce-cti_gyp_path={9}
                                  ce-audio_path={10}
                                  ce-ble_cozmo_path={11}
                                  ce-das_path={12}
                                  """.format(
                                    options.arch,
                                    os.path.join(options.projectRoot, 'generated/linux'),
                                    coretechExternalPath,
                                    webotsPath,
                                    ctiGtestPath,
                                    ctiAnkiUtilProjectPath,
                                    projectRoot,
                                    gtestPath,
                                    ankiUtilProjectPath,
                                    coretechInternalProjectPath,
                                    audioProjectGypPath,
                                    bleCozmoProjectPath,
                                    dasProjectPath,
                                  )
      os.environ['CC_target'] = '/usr/bin/clang'
      os.environ['CXX_target'] = '/usr/bin/clang++'
      os.environ['LD_target'] = '/usr/bin/clang++'
      gypArgs = ['--check', '--depth', '.', '-f', 'ninja', '--toplevel-dir', '../..', '--generator-output', '../../generated/linux', gypFile] 
      gyp.main(gypArgs)
      print "***********************HERE-configure.py2"

  # Configure Anki Audio project
  audio_config_script = os.path.join(audioProjectPath, 'configure.py')
  if (subprocess.call(audio_config_script) != 0):
    Logger.error('error Anki Audio project Configure')

  return True


if __name__ == '__main__':

  # go to the script dir, so that we can find the project dir
  # in the future replace this with command line param
  selfPath = os.path.dirname(os.path.realpath(__file__))
  os.chdir(selfPath)

  # find project root path
  projectRoot = subprocess.check_output(['git', 'rev-parse', '--show-toplevel']).rstrip("\r\n")
  args = sys.argv
  args.extend(['--projectRoot', projectRoot])
  if main(args):
    sys.exit(0)
  else:
    sys.exit(1)
