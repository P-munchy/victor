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
import json
import errno

ASSET_REPO = 'cozmo-assets'
ASSET_REPO_SUBDIRS = ['animations', 'animationGroups', 'faceAnimations']
ASSET_DIR = 'lib/anki/products-cozmo-assets'
BACKUP_DIR = '/tmp/anim_assets_backup'

#set up default logger
UtilLog = logging.getLogger('cozmo.game.configure')
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

def _getGypArgs(format, outputFolder, gypFile):
  return ['--check', '--depth', '.', '-f', format, '--toplevel-dir', '../..', \
    '--generator-output', outputFolder, '--include', '../../project/gyp/build-variables.gypi', gypFile]

def _readConfigFile(configFile):
  configData = None
  if os.path.isfile(configFile):
    with open(configFile, mode="r") as fileObj:
      configData = json.load(fileObj)
  return configData

def _checkSubdirs(assetDir, subdirs=ASSET_REPO_SUBDIRS):
  for subdir in subdirs:
    subdir = os.path.join(assetDir, subdir)
    if not os.path.exists(subdir):
      raise RuntimeError('Asset directory not found [%s]' % subdir)

def _backupDir(dirPath, backupDir=BACKUP_DIR):
  try:
    os.makedirs(backupDir)
  except OSError, e:
    if e.errno != errno.EEXIST:
      UtilLog.info("Deleting %s" % dirPath)
      shutil.rmtree(dirPath)
      return None
  dirName = os.path.basename(dirPath)
  backupDir = os.path.join(backupDir, dirName)
  if os.path.exists(dirPath):
    if os.path.exists(backupDir):
      UtilLog.info("Removing existing %s backup directory" % backupDir)
      try:
        shutil.rmtree(backupDir)
      except OSError:
        UtilLog.info("Failed to remove existing %s backup directory" % backupDir)
    UtilLog.info("Moving %s to %s" % (dirPath, backupDir))
    try:
      os.rename(dirPath, backupDir)
    except OSError, e:
      UtilLog.info("Move/rename failed, so deleting %s" % dirPath)
      shutil.rmtree(dirPath)
      return None
  return backupDir

def _makeRoomForSymlink(dst, src):
  # This function needs to return something (not None) if we cannot or
  # should not create/update the symlink in the calling function.
  # It currently returns True if the symlink is already setup correctly
  # and returns False if the path already exists and failed to get
  # removed (so the symlink cannot be setup).

  if os.path.islink(dst):
    # we have an existing symlink that MAY need to be replaced
    existingLink = os.readlink(dst)
    UtilLog.info("We currently have %s -> %s" % (dst, existingLink))
    if existingLink == src:
      UtilLog.info("Symlink is already setup correctly")
      return True
    UtilLog.info("Removing %s to change that symlink (so it points at %s)" % (dst, src))
    try:
      os.remove(dst)
    except OSError:
      print("Delete %s so that symlink can be updated for animation data" % dst)
      return False
  else:
    # we have an existing directory that needs to be moved to make room for the symlink
    try:
      backupDir = _backupDir(dst)
    except OSError:
      if os.path.exists(dst):
        print("Move/delete %s so it can be replaced by a symlink for animation data" % dst)
        return False
      else:
        backupDir = None
    if backupDir:
      print("Existing %s directory was moved to %s (to be replaced by a symlink)"
            % (dst, backupDir))
    else:
      print("Deleted the existing %s directory (to be replaced by a symlink)" % dst)

def _setupSymlinks(sourceDir, destDir, subdirs=ASSET_REPO_SUBDIRS):
  for subdir in subdirs:
    src = os.path.join(sourceDir, subdir)
    dst = os.path.join(destDir, subdir)
    if not os.path.exists(src):
      UtilLog.error("Cannot symlink %s -> %s because %s doesn't exist" % (dst, src, src))
      continue
    if os.path.exists(dst) or os.path.islink(dst):
      ret = _makeRoomForSymlink(dst, src)
      if ret is not None:
        continue
    # make parent directory(ies) for the symlink
    try:
      os.makedirs(destDir)
    except OSError, e:
      if e.errno != errno.EEXIST:
        raise
    print("Symlinking %s -> %s" % (dst, src))
    os.symlink(src, dst)

def checkCozmoAssetDir(options, assetRepo=ASSET_REPO, assetDir=ASSET_DIR):
  if not options.cozmoAssetPath:
    options.cozmoAssetPath = os.path.join(options.projectRoot, assetDir)
  if not os.path.exists(options.cozmoAssetPath):
    raise RuntimeError('Asset directory not found [%s]' % options.cozmoAssetPath)

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
  parser.add_argument('--das', metavar='DAS_PATH', dest='dasPath', action='store', default=None,
                      help='Use das-client repo checked out at DAS_PATH')
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
  parser.add_argument('--cozmoEngine', metavar='COZMO_ENGINE_PATH', dest='cozmoEnginePath', action='store', default=None,
                      help='Use cozmo-engine repo checked out at COZMO_ENGINE_PATH')
  parser.add_argument('--cozmoAssets', metavar='COZMO_ASSET_PATH', dest='cozmoAssetPath', action='store', default=None,
                      help='Use cozmo-asset repo checked out at COZMO_ASSET_PATH')
  parser.add_argument('--projectRoot', dest='projectRoot', action='store', default=None,
                      help='project location, assumed to be same as git repo root')
  parser.add_argument('--updateListsOnly', dest='updateListsOnly', action='store_true', default=False,
                      help='forces configure to only update .lst files and not run gyp project generation')
  parser.add_argument('--externals', metavar='EXTERNALS_DIR', dest='externalsPath', action='store', default=None,
                      help='Use repos checked out at EXTERNALS_DIR')

  # options controlling gyp output
  parser.add_argument('--arch', action='store',
                      choices=('universal', 'standard'),
                      default='universal',
                      help="Target set of architectures")
  parser.add_argument('--platform', action='append', dest='platforms',
                      choices=('ios', 'mac', 'android'),
                      help="Generate output for a specific platform")
  (options, args) = parser.parse_known_args(scriptArgs)

  if options.verbose:
    UtilLog.setLevel(logging.DEBUG)

  if options.platforms is None:
    options.platforms = ['ios', 'mac', 'android']

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

  try:
    checkCozmoAssetDir(options)
  except (RuntimeError, OSError), e:
    UtilLog.error(str(e))
    return False

  if not options.cozmoEnginePath:
    options.cozmoEnginePath = options.projectRoot
  if not os.path.exists(options.cozmoEnginePath):
    UtilLog.error('cozmo-engine not found [%s]' % (options.cozmoEnginePath) )
    return False
  cozmoEngineProjectPath = os.path.join(options.cozmoEnginePath, 'project/gyp/cozmoEngine.gyp')

  if not options.coretechInternalPath:
    options.coretechInternalPath = os.path.join(options.projectRoot, 'coretech')
  if not os.path.exists(options.coretechInternalPath):
    UtilLog.error('coretech-internal not found [%s]' % (options.coretechInternalPath) )
    return False
  coretechInternalProjectPath = os.path.join(options.coretechInternalPath, 'project/gyp/coretech-internal.gyp')

  if not options.buildToolsPath:
    options.buildToolsPath = os.path.join(options.projectRoot, 'tools/build')
  if not os.path.exists(options.buildToolsPath):
    UtilLog.error('build tools not found [%s]' % (options.buildToolsPath) )
    return False

  if not options.dasPath:
    options.dasPath = os.path.join(options.projectRoot, 'lib/das-client')
  if not os.path.exists(options.dasPath):
    UtilLog.error('das-client not found [%s]' % (options.dasPath) )
    return False
  dasProjectPath = os.path.join(options.dasPath, 'gyp/das-client.gyp')

  if not options.audioPath:
    options.audioPath = os.path.join(options.projectRoot, 'lib/audio')
  if not os.path.exists(options.audioPath):
    UtilLog.error('audio path not found [%s]' % options.audioPath)
    return False
  audioProjectPath = options.audioPath
  audioGeneratedCladPath=os.path.join(projectRoot, 'generated', 'clad', 'engine')
  audioProjectGypPath = os.path.join(audioProjectPath, 'gyp/audioEngine.gyp')

  if not options.bleCozmoPath:
    options.bleCozmoPath = os.path.join(options.projectRoot, 'lib/BLECozmo')
  if not os.path.exists(options.bleCozmoPath):
    UtilLog.error('BLECozmo path not found [%s]' % options.bleCozmoPath)
    return False
  bleCozmoProjectPath = os.path.join(options.bleCozmoPath, 'project/gyp/BLECozmo.gyp')


  sys.path.insert(0, os.path.join(options.buildToolsPath, 'tools/ankibuild'))
  import installBuildDeps
  import updateFileLists
  import generateUnityMeta

  if not options.ankiUtilPath:
    options.ankiUtilPath = os.path.join(options.projectRoot, 'lib/util')
  if not os.path.exists(options.ankiUtilPath):
    UtilLog.error('anki-util not found [%s]' % (options.ankiUtilPath) )
    return False
  ankiUtilProjectPath = os.path.join(options.ankiUtilPath, 'project/gyp/util.gyp')
  gtestPath = os.path.join(options.ankiUtilPath, 'libs/framework')

  if not options.webotsPath:
    options.webotsPath = '/Applications/Webots.app'
  if not os.path.exists(options.webotsPath):
    UtilLog.error('webots not found [%s]' % options.webotsPath)
    return False
  webotsPath = options.webotsPath

  # do not check for coretech external, and gyp if we are only updating list files
  if not options.updateListsOnly:

    if not options.coretechExternalPath or not os.path.exists(options.coretechExternalPath):
      UtilLog.error('coretech-external not found [%s]' % (options.coretechExternalPath) )
      return False
    coretechExternalPath = options.coretechExternalPath

    if not options.externalsPath or not os.path.exists(options.externalsPath):
        UtilLog.error('EXTERNALS directory not found [%s]' % (options.externalsPath) )
        return False
    externalsPath = options.externalsPath
    _checkSubdirs(os.path.join(externalsPath, ASSET_REPO))

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

  #run clad's make
  unityGeneratedPath=os.path.join(projectRoot, 'unity/Cozmo/Assets/Scripts/Generated/')
  if (subprocess.call(['make', '--silent', 'OUTPUT_DIR_CSHARP=' + unityGeneratedPath, 'csharp'],
    cwd=os.path.join(options.cozmoEnginePath, 'clad')) != 0):
    UtilLog.error("error compiling clad files")
    return False

  #generate unity's metafiles
  if (generateUnityMeta.generateMetaFiles(unityGeneratedPath, options.verbose)):
    UtilLog.error("error generating unity meta files")
    return False

  # update file lists
  generator = updateFileLists.FileListGenerator(options)
  generator.processFolder([options.cozmoAssetPath, os.path.join(externalsPath, ASSET_REPO)],
                          ['project/gyp/assets.lst'], exclude_patterns=["*.svn*"])

  if options.updateListsOnly:
    # TODO: remove dependency on abspath.
    # there is a bug due to 'os.chdir' and user passed rel path
    if (subprocess.call([os.path.join(options.cozmoEnginePath, 'project/gyp/configure_engine.py'),
     '--updateListsOnly', '--buildTools', options.buildToolsPath, '--ankiUtil', options.ankiUtilPath, '--with-clad', options.cladPath]) != 0):
      UtilLog.error("error executing cozmo-engine configure")
      return False
    return True

  # update subprojects
  for platform in options.platforms:
    # TODO: we should pass in our own options with any additional overides..
    # subproject might need to know about the build-tools location, --verbose, and other args...
    if (subprocess.call([os.path.join(options.cozmoEnginePath, 'project/gyp/configure_engine.py'),
     '--platform', platform, '--buildTools', options.buildToolsPath, '--ankiUtil', options.ankiUtilPath, '--updateListsOnly',
     '--with-clad', options.cladPath]) != 0):
      UtilLog.error("error executing submodule configure")
      return False

  configurePath = os.path.join(projectRoot, 'project/gyp')
  cozmoEngineConfigurePath = os.path.join(options.cozmoEnginePath, 'project/gyp')
  coretechInternalConfigurePath = os.path.join(options.coretechInternalPath, 'project/gyp')
  gypFile = 'cozmoGame.gyp'
  # gypify path names
  cgGtestPath = os.path.relpath(gtestPath, configurePath)
  ceGtestPath = os.path.relpath(gtestPath, cozmoEngineConfigurePath)
  ctiGtestPath = os.path.relpath(gtestPath, coretechInternalConfigurePath)
  cgAnkiUtilProjectPath = os.path.relpath(ankiUtilProjectPath, configurePath)
  ceAnkiUtilProjectPath = os.path.relpath(ankiUtilProjectPath, cozmoEngineConfigurePath)
  ctiAnkiUtilProjectPath = os.path.relpath(ankiUtilProjectPath, coretechInternalConfigurePath)
  audioAnkiUtilProjectPath = ctiAnkiUtilProjectPath
  cgCoretechInternalProjectPath = os.path.relpath(coretechInternalProjectPath, configurePath)
  ceCoretechInternalProjectPath = os.path.relpath(coretechInternalProjectPath, cozmoEngineConfigurePath)
  cgCozmoEngineProjectPath = os.path.relpath(os.path.join(options.cozmoEnginePath, 'project/gyp/cozmoEngine.gyp'), configurePath)
  cgMexProjectPath = os.path.relpath(os.path.join(options.cozmoEnginePath, 'project/gyp/cozmoEngineMex.gyp'), configurePath)
  cozmoConfigPath = os.path.join(options.cozmoEnginePath, 'resources')
  audioProjectPath = os.path.abspath(os.path.join(cozmoEngineConfigurePath, audioProjectPath))
  cgAudioProjectGypPath = os.path.relpath(os.path.join(cozmoEngineConfigurePath, audioProjectGypPath))
  ceAudioProjectGypPath = cgAudioProjectGypPath
  #DNW: Add another GYP path here.
  cgDasProjectPath = os.path.relpath(dasProjectPath, configurePath)
  ceDasProjectPath = os.path.relpath(dasProjectPath, cozmoEngineConfigurePath)
  cgBLECozmoProjectPath = os.path.relpath(bleCozmoProjectPath, configurePath)
  ceBLECozmoProjectPath = os.path.relpath(bleCozmoProjectPath, cozmoEngineConfigurePath)
        
  buildMex = 'no'
  if options.mex:
    buildMex = 'yes'

  if subprocess.call(['mkdir', '-p',
    os.path.join(projectRoot, 'generated/resources')]) != 0 :
    UtilLog.error("error creating generated/resources")
    return False

  for fwFolder in ['firmware', 'old_firmware']:
    if subprocess.call(['ln', '-s', '-f', '-n',
      os.path.join(externalsPath, fwFolder, 'release'),
      os.path.join(projectRoot, 'resources/config/basestation', fwFolder)]) != 0 :
      UtilLog.error("error symlinking firmware resources")
      return False

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
                                  audioutil_library_type=static_library
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
                                  cg-gtest_path={10}
                                  cg-util_gyp_path={11}
                                  cg-cti_gyp_path={12}
                                  cg-ce_gyp_path={13}
                                  cg-mex_gyp_path={14}
                                  build-mex={15}
                                  cozmo_asset_path={16}
                                  ce-audio_path={17}
                                  cg-audio_path={18}
                                  externals_path={19}
                                  cg-das_path={20}
                                  ce-das_path={21}
                                  cg-ble_cozmo_path={22}
                                  ce-ble_cozmo_path={23}
                                  clad_dir={24}
				  util_gyp_path={25}
                                  generated_clad_path={26}
                                  """.format(
                                    options.arch,
                                    os.path.join(options.projectRoot, 'generated/mac'),
                                    coretechExternalPath,
                                    webotsPath,
                                    ctiGtestPath,
                                    ctiAnkiUtilProjectPath,
                                    options.cozmoEnginePath,
                                    ceGtestPath,
                                    ceAnkiUtilProjectPath,
                                    ceCoretechInternalProjectPath,
                                    cgGtestPath,
                                    cgAnkiUtilProjectPath,
                                    cgCoretechInternalProjectPath,
                                    cgCozmoEngineProjectPath,
                                    cgMexProjectPath,
                                    buildMex,
                                    options.cozmoAssetPath,
                                    ceAudioProjectGypPath,
                                    cgAudioProjectGypPath,
                                    externalsPath,
                                    cgDasProjectPath,
                                    ceDasProjectPath,
                                    cgBLECozmoProjectPath,
                                    ceBLECozmoProjectPath,
                                    clad_dir_rel,
                                    audioAnkiUtilProjectPath,
                                    audioGeneratedCladPath,
                                  )
      gypArgs = _getGypArgs('xcode', '../../generated/mac', gypFile)
      gyp.main(gypArgs)




  # ios
  if 'ios' in options.platforms:
    os.environ['GYP_DEFINES'] = """
                                audio_library_type=static_library
                                audio_library_build=profile
                                bs_library_type=static_library
                                de_library_type=static_library
                                kazmath_library_type=static_library
                                jsoncpp_library_type=static_library
                                util_library_type=static_library
                                audioutil_library_type=static_library
                                worldviz_library_type=static_library
                                das_library_type=static_library
                                basestation_target_name=Basestation
                                driveengine_target_name=DriveEngine
                                OS=ios
                                cg-mex_gyp_path=blah
                                cozmo_asset_path=blah
                                cozmo_config_path=blah
                                build-mex=no
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
                                cg-gtest_path={10}
                                cg-util_gyp_path={11}
                                cg-cti_gyp_path={12}
                                cg-ce_gyp_path={13}
                                ce-audio_path={14}
                                cg-audio_path={15}
                                externals_path={16}
                                cg-das_path={17}
                                ce-das_path={18}
                                cg-ble_cozmo_path={19}
                                ce-ble_cozmo_path={20}
                                clad_dir={21}
                                util_gyp_path={22}
                                generated_clad_path={23}
                                """.format(
                                  options.arch,
                                  os.path.join(options.projectRoot, 'generated/ios'),
                                  coretechExternalPath,
                                  webotsPath,
                                  ctiGtestPath,
                                  ctiAnkiUtilProjectPath,
                                  options.cozmoEnginePath,
                                  ceGtestPath,
                                  ceAnkiUtilProjectPath,
                                  ceCoretechInternalProjectPath,
                                  cgGtestPath,
                                  cgAnkiUtilProjectPath,
                                  cgCoretechInternalProjectPath,
                                  cgCozmoEngineProjectPath,
                                  ceAudioProjectGypPath,
                                  cgAudioProjectGypPath,
                                  externalsPath,
                                  cgDasProjectPath,
                                  ceDasProjectPath,
                                  cgBLECozmoProjectPath,
                                  ceBLECozmoProjectPath,
                                  clad_dir_rel,
                                  audioAnkiUtilProjectPath,
                                  audioGeneratedCladPath,
                                )
    gypArgs = _getGypArgs('xcode', '../../generated/ios', gypFile)
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
                                audioutil_library_type=static_library
                                worldviz_library_type=static_library
                                das_library_type=static_library
                                os_posix=1
                                OS=android
                                GYP_CROSSCOMPILE=1
                                target_arch=arm
                                clang=1
                                component=static_library
                                cg-mex_gyp_path=blah
                                cozmo_asset_path=blah
                                cozmo_config_path=blah
                                build-mex=no
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
                                cg-gtest_path={10}
                                cg-util_gyp_path={11}
                                cg-cti_gyp_path={12}
                                cg-ce_gyp_path={13}
                                ndk_root={14}
                                ce-audio_path={15}
                                cg-audio_path={16}
                                externals_path={17}
                                cg-das_path={18}
                                ce-das_path={19}
                                cg-ble_cozmo_path={20}
                                ce-ble_cozmo_path={21}
                                clad_dir={22}
                                util_gyp_path={23}
                                generated_clad_path={24}
                                """.format(
                                  options.arch,
                                  os.path.join(options.projectRoot, 'generated/android'),
                                  coretechExternalPath,
                                  webotsPath,
                                  ctiGtestPath,
                                  ctiAnkiUtilProjectPath,
                                  options.cozmoEnginePath,
                                  ceGtestPath,
                                  ceAnkiUtilProjectPath,
                                  ceCoretechInternalProjectPath,
                                  cgGtestPath,
                                  cgAnkiUtilProjectPath,
                                  cgCoretechInternalProjectPath,
                                  cgCozmoEngineProjectPath,
                                  ndk_root,
                                  ceAudioProjectGypPath,
                                  cgAudioProjectGypPath,
                                  externalsPath,
                                  cgDasProjectPath,
                                  ceDasProjectPath,
                                  cgBLECozmoProjectPath,
                                  ceBLECozmoProjectPath,
                                  clad_dir_rel,
				  audioAnkiUtilProjectPath,
                                  audioGeneratedCladPath,
                                )
    os.environ['CC_target'] = os.path.join(ndk_root, 'toolchains/llvm/prebuilt/darwin-x86_64/bin/clang')
    os.environ['CXX_target'] = os.path.join(ndk_root, 'toolchains/llvm/prebuilt/darwin-x86_64/bin/clang++')
    os.environ['AR_target'] = os.path.join(ndk_root, 'toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-gcc-ar')
    os.environ['LD_target'] = os.path.join(ndk_root, 'toolchains/llvm/prebuilt/darwin-x86_64/bin/clang++')
    os.environ['NM_target'] = os.path.join(ndk_root, 'toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/arm-linux-androideabi/bin/nm')
    os.environ['READELF_target'] = os.path.join(ndk_root, 'toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-readelf')
    gypArgs = _getGypArgs('ninja-android', 'generated/android', gypFile)
    gyp.main(gypArgs)

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
