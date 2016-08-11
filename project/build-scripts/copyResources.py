#!/usr/bin/python

import os
import os.path
import sys
# import re
# import subprocess
import argparse
# import StringIO
# import shutil
import logging

class CopyResources(object):
  def __init__(self):
    self.log = logging.getLogger('cozmo.copyResources')
    stdout_handler = logging.StreamHandler()
    formatter = logging.Formatter('%(name)s - %(message)s')
    stdout_handler.setFormatter(formatter)
    self.log.addHandler(stdout_handler)
    self.options = None


  def getCleanOptions(self,scriptArgs):
    "alowes us to check for --clean without requiring the additional params"
    version = '1.0'
    parser = argparse.ArgumentParser(description='copies resources to the destination path', version=version)
    parser.add_argument('--debug', '--verbose', '-d', dest='verbose', action='store_true',
                        help='prints debug output')
    parser.add_argument('--path', dest='pathExtension', action='store',
                        help='prepends to the environment PATH')
    parser.add_argument('--clean', '-c', dest='clean', action='store_true',
                        help='cleans all output folders')
    parser.add_argument('--destination', dest='destinationPath', required=True,
                        action='store', default=None, help='Where to copy files to.')
    parser.add_argument('--buildToolsPath', dest='buildToolsPath', required=True,
                        action='store', default=None, help='where build tools are located')
    (self.options, args) = parser.parse_known_args(scriptArgs)

  def getOptions(self,scriptArgs):
    version = '1.0'
    parser = argparse.ArgumentParser(description='copies resources to the destination path', version=version)
    parser.add_argument('--debug', '--verbose', '-d', dest='verbose', action='store_true',
                        help='prints debug output')
    parser.add_argument('--path', dest='pathExtension', action='store',
                        help='prepends to the environment PATH')
    parser.add_argument('--clean', '-c', dest='clean', action='store_true',
                        help='cleans all output folders')
    parser.add_argument('--destination', dest='destinationPath', required=True,
                        action='store', default=None, help='Where to copy files to.')
    parser.add_argument('--buildToolsPath', dest='buildToolsPath', required=True,
                        action='store', default=None, help='where build tools are located')
    parser.add_argument('--externalAssetsPath', dest='externalAssetsPath', required=True,
                        action='store', default=None, help='where external assets are unpackaged at')
    parser.add_argument('--animationGroupsPath', dest='animationGroupPath', required=True,
                        action='store', default=None, help='where animation groups are located')
    parser.add_argument('--dailyGoalsPath', dest='dailyGoalsPath', required=True,
                        action='store', default=None, help='Where daily goals data is located')
    parser.add_argument('--rewardedActionsPath', dest='rewardedActionsPath', required=True,
                        action='store', default=None, help='Where rewarded actions data is located')
    parser.add_argument('--engineResourcesPath', dest='engineResourcesPath', required=True,
                        action='store', default=None, help='where engine resources are located')
    parser.add_argument('--unityAssetsPath', dest='unityAssetsPath', required=True,
                        action='store', default=None, help='where unity assets are located')
    parser.add_argument('--pocketSphinxPath', dest='pocketSphinxPath', required=True,
                        action='store', default=None, help='where pocket sphinx resources are located')
    parser.add_argument('--soundBanksPath', dest='soundBanksPath', required=True,
                        action='store', default=None, help='where sound banks are located')
    parser.add_argument('--copyMethod', dest='copyMethod', action='store', default='rsync',
                        choices=('rsync', 'copy'), required=True,
                        help="method to use to move the files over.")
    parser.add_argument('--platform', action='store', dest='platform', required=True,
                        choices=('ios', 'mac', 'android', 'linux'),
                        help="Generate output for a specific platform")
    (self.options, args) = parser.parse_known_args(scriptArgs)


  def run(self, scriptArgs):
    self.getCleanOptions(scriptArgs)

    if self.options.verbose:
      self.log.setLevel(logging.DEBUG)

    if (self.options.pathExtension):
      os.environ['PATH'] = self.options.pathExtension + ':' + os.environ['PATH']

    sys.path.insert(0, os.path.join(self.options.buildToolsPath, 'tools'))
    global ankibuild
    import ankibuild.util

    # clean folders
    if self.options.clean:
      self.clean()
      return True

    self.getOptions(scriptArgs)

    success = True
    if self.options.copyMethod == 'copy':
      success = self.copy()
    elif self.options.copyMethod == 'rsync':
      success = self.rsync()

    self.generateResourceManifest()

    return success

  def clean(self):
    if os.path.isdir(self.options.destinationPath):
      ankibuild.util.File.rm_rf(self.options.destinationPath)


  def copy(self):

    # we must delete the folder before we copy items again. no other way to guarantee removal of extra data.
    self.clean()

    # unity assets
    unityAssetsPath = self.options.unityAssetsPath.rstrip('/')
    destinationPath = self.options.destinationPath
    if not ankibuild.util.File.cptree(unityAssetsPath, destinationPath):
      self.log.error("error copying {0} to {1}".format (unityAssetsPath, destinationPath))
      return False
    for root, dirs, files in os.walk(self.options.destinationPath):
      for file in files:
        if file.endswith(".meta"):
          ankibuild.util.File.rm(os.path.join(root, file))

    # cozmo resources
    cozmoResourcesPath = os.path.join(self.options.destinationPath, 'cozmo_resources')
    if not os.path.isdir(cozmoResourcesPath):
      ankibuild.util.File.mkdir_p(cozmoResourcesPath)

    # assets
    assetsPath = os.path.join(cozmoResourcesPath, 'assets')
    if os.path.isdir(assetsPath):
      ankibuild.util.File.rm_rf(assetsPath)
    if not ankibuild.util.File.cptree(self.options.externalAssetsPath, assetsPath):
      self.log.error("error copying {0} to {1}".format (self.options.externalAssetsPath, assetsPath))
      return False

    # remove .svn
    ankibuild.util.File.rm_rf(os.path.join(assetsPath, '.svn'))

    # remove tar files
    for root, dirs, files in os.walk(os.path.join(assetsPath, 'animations')):
      for file in files:
        if file.endswith(".tar"):
          ankibuild.util.File.rm(os.path.join(root, file))
    for root, dirs, files in os.walk(os.path.join(assetsPath, 'faceAnimations')):
      for file in files:
        if file.endswith(".tar"):
          ankibuild.util.File.rm(os.path.join(root, file))

    # animation groups
    animationGroupPath = os.path.join(cozmoResourcesPath, 'assets/animationGroupMaps')
    if os.path.isdir(animationGroupPath):
      ankibuild.util.File.rm_rf(animationGroupPath)
    if not ankibuild.util.File.cptree(self.options.animationGroupPath, animationGroupPath):
      self.log.error("error copying {0} to {1}".format (self.options.animationGroupPath, animationGroupPath))
      return False

    # daily goals
    dailyGoalsPath = os.path.join(cozmoResourcesPath, 'assets/DailyGoals')
    if os.path.isdir(dailyGoalsPath):
      ankibuild.util.File.rm_rf(dailyGoalsPath)
    if not ankibuild.util.File.cptree(self.options.dailyGoalsPath, dailyGoalsPath):
      self.log.error("error copying {0} to {1}".format (self.options.dailyGoalsPath, dailyGoalsPath))
      return False

    # rewarded actions
    rewardedActionsPath = os.path.join(cozmoResourcesPath, 'assets/RewardedActions')
    if os.path.isdir(rewardedActionsPath):
      ankibuild.util.File.rm_rf(rewardedActionsPath)
    if not ankibuild.util.File.cptree(self.options.rewardedActionsPath, rewardedActionsPath):
      self.log.error("error copying {0} to {1}".format (self.options.rewardedActionsPath, rewardedActionsPath))
      return False

    # engine resources
    engineResourcesPath = os.path.join(cozmoResourcesPath, 'config')
    if os.path.isdir(engineResourcesPath):
      ankibuild.util.File.rm_rf(engineResourcesPath)
    if not ankibuild.util.File.cptree(self.options.engineResourcesPath, engineResourcesPath):
      self.log.error("error copying {0} to {1}".format (self.options.engineResourcesPath, engineResourcesPath))
      return False

    # pocket sphinx
    pocketSphinxPath = os.path.join(cozmoResourcesPath, 'pocketsphinx')
    if os.path.isdir(pocketSphinxPath):
      ankibuild.util.File.rm_rf(pocketSphinxPath)
    if not ankibuild.util.File.cptree(self.options.pocketSphinxPath, pocketSphinxPath):
      self.log.error("error copying {0} to {1}".format (self.options.pocketSphinxPath, pocketSphinxPath))
      return False

    # sound banks
    soundBanksPlatform = ''
    if self.options.platform == 'ios':
      soundBanksPlatform = 'iOS'
    elif self.options.platform == 'mac':
      soundBanksPlatform = 'Mac'
    elif self.options.platform == 'android':
      soundBanksPlatform = 'Android'

    soundBanksPath = os.path.join(cozmoResourcesPath, 'sound')
    if os.path.isdir(soundBanksPath):
      ankibuild.util.File.rm_rf(soundBanksPath)
    if not ankibuild.util.File.cptree(os.path.join(self.options.soundBanksPath, soundBanksPlatform), soundBanksPath):
      self.log.error("error copying {0} to {1}".format (
        os.path.join(self.options.soundBanksPath, soundBanksPlatform), soundBanksPath))
      return False

    return True


  def rsync(self):
    baseargs = ['rsync', '-a', '-k', '--delete']

    # unity assets
    args = baseargs[:]
    args += ['--exclude', '*.meta']
    args += [self.options.unityAssetsPath, self.options.destinationPath]
    ankibuild.util.File.execute(args)

    # cozmo resources
    cozmoResourcesPath = os.path.join(self.options.destinationPath, 'cozmo_resources')
    if not os.path.isdir(cozmoResourcesPath):
      ankibuild.util.File.mkdir_p(cozmoResourcesPath)

    # assets
    assetsPath = os.path.join(cozmoResourcesPath, 'assets')
    args = baseargs[:]
    args += ['--exclude', '.svn']
    args += ['--exclude', '*.tar']
    args += [self.options.externalAssetsPath, assetsPath]
    ankibuild.util.File.execute(args)

    # animation groups
    animationGroupPath = os.path.join(cozmoResourcesPath, 'assets/animationGroupMaps')
    args = baseargs[:]
    args += [self.options.animationGroupPath, animationGroupPath]
    ankibuild.util.File.execute(args)

    # daily goals
    dailyGoalsPath = os.path.join(cozmoResourcesPath, 'assets/DailyGoals')
    args = baseargs[:]
    args += [self.options.dailyGoalsPath, dailyGoalsPath]
    ankibuild.util.File.execute(args)

    # rewarded actions
    rewardedActionsPath = os.path.join(cozmoResourcesPath, 'assets/RewardedActions')
    args = baseargs[:]
    args += [self.options.rewardedActionsPath, rewardedActionsPath]
    ankibuild.util.File.execute(args)

    # engine resources
    engineResourcesPath = os.path.join(cozmoResourcesPath, 'config')
    args = baseargs[:]
    args += [self.options.engineResourcesPath, engineResourcesPath]
    ankibuild.util.File.execute(args)

    # pocket sphinx
    pocketSphinxPath = os.path.join(cozmoResourcesPath, 'pocketsphinx')
    args = baseargs[:]
    if not self.options.pocketSphinxPath.endswith('/'):
      self.options.pocketSphinxPath = self.options.pocketSphinxPath + '/'
    args += [self.options.pocketSphinxPath, pocketSphinxPath]
    ankibuild.util.File.execute(args)

    # sound banks
    soundBanksPlatform = ''
    if self.options.platform == 'ios':
      soundBanksPlatform = 'iOS/'
    elif self.options.platform == 'mac':
      soundBanksPlatform = 'Mac/'
    elif self.options.platform == 'android':
      soundBanksPlatform = 'Android/'

    soundBanksPath = os.path.join(cozmoResourcesPath, 'sound')
    args = baseargs[:]
    args += [os.path.join(self.options.soundBanksPath, soundBanksPlatform), soundBanksPath]
    ankibuild.util.File.execute(args)

    return True

  def generateResourceManifest(self):
    f = open(os.path.join(self.options.destinationPath, 'resources.txt'), 'w')
    cwdir = os.getcwd()
    os.chdir(self.options.destinationPath)
    for root, dirs, files in os.walk('.', followlinks=True):
      if root.startswith('./'):
        root = root[2:]
      for dir in dirs:
        if root == '.':
          f.write(dir + '\n')
        else:
          f.write(root + '/' + dir + '\n')
      for file in files:
        if root == '.':
          f.write(file + '\n')
        else:
          f.write(root + '/' + file + '\n')
    f.close()
    os.chdir(cwdir)


if __name__ == '__main__':

  # go to the script dir, so that we can find the project dir
  # in the future replace this with command line param
  # selfPath = os.path.dirname(os.path.realpath(__file__))
  # os.chdir(selfPath)

  # find project root path
  # projectRoot = subprocess.check_output(['git', 'rev-parse', '--show-toplevel']).rstrip("\r\n")
  copyResources = CopyResources()
  args = sys.argv
  # args.extend(['--projectRoot', projectRoot])

  if copyResources.run(args):
    sys.exit(0)
  else:
    sys.exit(1)
