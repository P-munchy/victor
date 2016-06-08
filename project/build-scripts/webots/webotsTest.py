#!/usr/bin/python

import os
import os.path
import shutil
import errno
import subprocess
import textwrap
import sys
import argparse
import time
import glob
import xml.etree.ElementTree as ET
from decimal import Decimal
import threading
import tarfile
import logging
import ConfigParser
import Queue
from datetime import datetime

#set up default logger
UtilLog = logging.getLogger('webots.test')
stdout_handler = logging.StreamHandler()
formatter = logging.Formatter('%(name)s - %(message)s')
stdout_handler.setFormatter(formatter)
UtilLog.addHandler(stdout_handler)

runningMultipleTests = False
curTime = datetime.strftime(datetime.now(), '%Y-%m-%d %H:%M:%S')

worldFileTestNamePlaceHolder = '%COZMO_SIM_TEST%'
generatedWorldFileName = '__generated__.wbt'


class WorkContext(object): pass


def mkdir_p(path):
  try:
    os.makedirs(path)
  except OSError as exc: # Python >2.5
    if exc.errno == errno.EEXIST and os.path.isdir(path):
      pass
    else: 
      raise



# build unittest executable
def build(options):
  # prepare paths
  project = os.path.join(options.projectRoot, 'generated/mac')
  derivedData = os.path.join(options.projectRoot, 'generated/mac/DerivedData')


  # prepare build command
  buildCommand = [
    'xcodebuild', 
    '-project', os.path.join(options.projectRoot, 'generated/mac/cozmoEngine.xcodeproj'),
    '-target', 'webotsControllers',
    '-sdk', 'macosx',
    '-configuration', options.buildType,
    'SYMROOT=' + derivedData,
    'OBJROOT=' + derivedData,
    'build'
    ]

  UtilLog.debug('build command ' + ' '.join(buildCommand))
  # return true if build is good
  return subprocess.call(buildCommand) == 0


# runs webots test
def runWebots(options, resultQueue, test):
  # prepare run command
  runCommand = [
    '/Applications/Webots/webots', 
    '--stdout',
    '--stderr',
    '--disable-modules-download',
    '--minimize',  # Ability to start without graphics is on the wishlist
    '--mode=fast',
    os.path.join(options.projectRoot, 'simulator/worlds/' +  generatedWorldFileName),
    ]

  if options.showGraphics:
    runCommand.remove('--stdout')
    runCommand.remove('--stderr')
    runCommand.remove('--minimize')
    runCommand = ['--mode=run' if x=='--mode=fast' else x for x in runCommand]

  UtilLog.debug('run command ' + ' '.join(runCommand))

  buildFolder = os.path.join(options.projectRoot, 'build/mac/', options.buildType)
  mkdir_p(buildFolder)

  if runningMultipleTests:
    logFileName = os.path.join(buildFolder, 'webots_out_' + test + '_' + curTime + '.txt')
  else:
    logFileName = os.path.join(buildFolder, 'webots_out_' + test + '.txt')

  logFile = open(logFileName, 'w')
  startedTimeS = time.time()
  returnCode = subprocess.call(runCommand, stdout = logFile, stderr = logFile, cwd=buildFolder)
  resultQueue.put(returnCode)
  ranForS = time.time() - startedTimeS
  logFile.close()

  UtilLog.info('webots run took %f seconds' % (ranForS))


def WaitUntil(conditionFcn, timeout, period=0.25):
  mustend = time.time() + timeout
  while time.time() < mustend:
    if conditionFcn(): return True
    time.sleep(period)
  return False

def IsWebotsRunning():
  process = subprocess.Popen("ps -ax | grep [/]Applications/Webots/webots.app", shell=True, stdout=subprocess.PIPE)
  result = process.communicate()[0]
  if len(result) > 0:
    return True
  return False
  
def IsWebotsNotRunning():
  return not IsWebotsRunning()

# sleep for some time, then kill webots if needed
def stopWebots(options):
  # kill all webots processes
  ps   = subprocess.Popen(('ps', 'Aux'), stdout=subprocess.PIPE)
  grep = subprocess.Popen(('grep', '[w]ebots'), stdin=ps.stdout, stdout=subprocess.PIPE)
  grepMinusThisProcess = subprocess.Popen(('grep', '-v', 'webotsTest.py'), stdin=grep.stdout, stdout=subprocess.PIPE)
  awk  = subprocess.Popen(('awk', '{print $2}'), stdin=grepMinusThisProcess.stdout, stdout=subprocess.PIPE)
  kill = subprocess.Popen(('xargs', 'kill', '-9'), stdin=awk.stdout, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  out,err = kill.communicate()
  if '' != err:
    print err
  print out

  if (WaitUntil(IsWebotsNotRunning, 5, 0.5)):
    cleanWebots(options)
    return True
    
  print 'ERROR: Webots instance was running, but did not die when killed'
  return False
    

# cleans up webots IPC facilities
def cleanWebots(options):
  # Are there other Webots processes running? If so, don't kill anything
  # since they may be in use!
  if IsWebotsRunning():
    print 'INFO: An instance of Webots is running. Will not kill IPC facilities'
    return True


  # kill webots IPC leftovers
  runCommand = [
    os.path.join(options.projectRoot, 'simulator/kill_ipcs.sh'), 
    ]
  # execute
  result = subprocess.call(runCommand)
  if result != 0:
    return False
  return True


def SetTestStatus(testName, status, totalResultFlag, testStatuses):
  testStatuses[testName] = status
  UtilLog.info('Test ' + testName + (' FAILED' if status <> 0 else ' PASSED'))
  if status <> 0 or not totalResultFlag:
    return False
  return True


# runs all threads groups
# returns true if all tests suceeded correctly
def runAll(options):
  # Get list of tests and world files from config
  config = ConfigParser.ConfigParser()
  webotsTestCfgPath = 'project/build-scripts/webots/webotsTests.cfg'
  config.read(webotsTestCfgPath)
  testNames = config.sections()
  testStatuses = {}
  allTestsPassed = True
  totalErrorCount = 0
  totalWarningCount = 0

  for test in testNames:
    if not config.has_option(test, 'world_file'):
      UtilLog.error('ERROR: No world file specified for test ' + test + '.')
      allTestsPassed = SetTestStatus(test, -10, allTestsPassed, testStatuses)
      continue
    
    baseWorldFile = config.get(test, 'world_file')
    UtilLog.info('Running test: ' + test + ' in world ' + baseWorldFile)
    
    # Check if world file contains valid test name place holder
    baseWorldFile = open('simulator/worlds/' + baseWorldFile, 'r')
    baseWorldData = baseWorldFile.read()
    baseWorldFile.close()
    if worldFileTestNamePlaceHolder not in baseWorldData:
      UtilLog.error('ERROR: ' + worldFile + ' is not a valid test world. (No ' + worldFileTestNamePlaceHolder + ' found.)')
      allTestsPassed = SetTestStatus(test, -11, allTestsPassed, testStatuses)
      continue

    # Generate world file with appropriate args passed into test controller
    generatedWorldData = baseWorldData.replace(worldFileTestNamePlaceHolder, test)
    generatedWorldFile = open(os.path.join(options.projectRoot, 'simulator/worlds/' + generatedWorldFileName), 'w+')
    generatedWorldFile.write(generatedWorldData)
    generatedWorldFile.close()

    # Run test in thread
    testResultQueue = Queue.Queue(1)
    runWebotsThread = threading.Thread(target=runWebots, args=[options, testResultQueue, test])
    runWebotsThread.start()
    runWebotsThread.join(120) # with timeout
    
    # Check if timeout exceeded
    if runWebotsThread.isAlive():
      UtilLog.error('ERROR: ' + test + ' exceeded timeout.')
      stopWebots(options)
      allTestsPassed = SetTestStatus(test, -12, allTestsPassed, testStatuses)
      print 'allTestsPassed ' + str(allTestsPassed)
      continue

    # Check log for crashes, errors, and warnings
    # TODO: Crashes affect test result, but errors and warnings do not. Should they?
    buildFolder = os.path.join(options.projectRoot, 'build/mac/', options.buildType)
    
    if runningMultipleTests:
      logFileName = os.path.join(buildFolder, 'webots_out_' + test + '_' + curTime + '.txt')
    else:
      logFileName = os.path.join(buildFolder, 'webots_out_' + test + '.txt')

    (crashCount, errorCount, warningCount) = parseOutput(options, logFileName)
    totalErrorCount += errorCount
    totalWarningCount += warningCount

    # Check for crashes
    if crashCount > 0:
      UtilLog.error('ERROR: ' + test + ' had a crashed controller.');
      allTestsPassed = SetTestStatus(test, -13, allTestsPassed, testStatuses)
      continue

    # Get return code from test
    if testResultQueue.empty():
      UtilLog.error('ERROR: No result code received from ' + test)
      allTestsPassed = SetTestStatus(test, -14, allTestsPassed, testStatuses)
      continue
    
    allTestsPassed = SetTestStatus(test, testResultQueue.get(), allTestsPassed, testStatuses)

  return (allTestsPassed, testStatuses, totalErrorCount, totalWarningCount, len (testNames))



# returns true if there were no errors in the log file
def parseOutput(options, logFile):
  # read log file output
  fileHandle = open(logFile, 'r')
  lines = [line.strip() for line in fileHandle]
  fileHandle.close()
  crashCount = 0
  errorCount = 0
  warningCount = 0

  for line in lines:
    if 'The process crashed some time after starting successfully.' in line:
      crashCount = crashCount + 1
    if 'Error' in line or 'ERROR' in line:
      errorCount = errorCount + 1
    if 'Warn' in line:
      warningCount = warningCount + 1

  return (crashCount, errorCount, warningCount)


# tarball valgrind output files together
def tarball(options):
  buildFolder = os.path.join(options.projectRoot, 'build/mac/', options.buildType)
  tar = tarfile.open(os.path.join(buildFolder, "webots_out.tar.gz"), "w:gz")
  
  config = ConfigParser.ConfigParser()
  webotsTestCfgPath = 'project/build-scripts/webots/webotsTests.cfg'
  config.read(webotsTestCfgPath)
  testNames = config.sections()
  for test in testNames:
    if runningMultipleTests:
      logFileName = os.path.join(buildFolder, 'webots_out_' + test + '_' + curTime + '.txt')
    else:
      logFileName = os.path.join(buildFolder, 'webots_out_' + test + '.txt')
    tar.add(logFileName, arcname=os.path.basename(logFileName))
  
  tar.close()
      

# executes main script logic
def main(scriptArgs):

  # parse arguments
  version = '1.0'
  parser = argparse.ArgumentParser(
  	# formatter_class=argparse.ArgumentDefaultsHelpFormatter,
  	formatter_class=argparse.RawDescriptionHelpFormatter,
  	description='Runs Webots functional tests', 
  	version=version
  	)
  parser.add_argument('--debug', '-d', '--verbose', dest='debug', action='store_true',
                      help='prints debug output')
  parser.add_argument('--buildType', '-b', dest='buildType', action='store', default='Debug',
                      help='build types [ Debug, Release ]. (default: Debug)')
  parser.add_argument('--projectRoot', dest='projectRoot', action='store',
                      help='location of the project root')
  parser.add_argument('--showGraphics', dest='showGraphics', action='store_true',
                      help='display Webots window')
  parser.add_argument('--numRuns', dest='numRuns', action='store', default=1,
                      help='run the tests this many times also saves logs with timestamps so they arent overwritten')
  (options, args) = parser.parse_known_args(scriptArgs)

  if options.debug:
    UtilLog.setLevel(logging.DEBUG)
  else:
    UtilLog.setLevel(logging.INFO)

  # if no project root fund - make one up
  if not options.projectRoot:
    # go to the script dir, so that we can find the project dir
  	# in the future replace this with command line param
  	selfPath = os.path.dirname(os.path.realpath(__file__))
  	os.chdir(selfPath)

  	# find project root path
  	projectRoot = subprocess.check_output(['git', 'rev-parse', '--show-toplevel']).rstrip("\r\n")
  	options.projectRoot = projectRoot
  else:
    options.projectRoot = os.path.normpath(os.path.join(os.getcwd(), options.projectRoot))
  os.chdir(options.projectRoot)

  UtilLog.debug(options)

  # build the project first
  if not build(options):
    UtilLog.error("ERROR build failed")
    return 1

  # if we are running multiple tests set the flag
  if(int(options.numRuns) > 1):
    global runningMultipleTests
    runningMultipleTests= True

  num_of_failed_tests = 0
  num_of_passed_tests = 0

  for _ in range(0, int(options.numRuns)):
    # save current time for logs
    global curTime
    curTime = datetime.strftime(datetime.now(), '%Y-%m-%d %H:%M:%S')
  
    # Kill webots in case it's running
    stopWebots(options)
    
    # run the tests
    (testsSucceeded, testResults, totalErrorCount, totalWarningCount, testCount) = runAll(options)
    tarball(options)

    print 'Test results: '
    for key,val in testResults.items():
      print key + ' : ' + str(val)

    returnValue = 0;
    stopResult = stopWebots(options)
    if not stopResult:
      # how do we notify the build system that there is something wrong, but it is not this build specific?
      returnValue = returnValue + 1

    print '##teamcity[buildStatisticValue key=\'WebotsErrorCount\' value=\'%d\']' % (totalErrorCount)
    print '##teamcity[buildStatisticValue key=\'WebotsWarningCount\' value=\'%d\']' % (totalWarningCount)
    print '##teamcity[buildStatisticValue key=\'WebotsTestCount\' value=\'%d\']' % (testCount)

    if not testsSucceeded:
      UtilLog.error("*************************")
      UtilLog.error("SOME TESTS FAILED")
      UtilLog.error("*************************")
      returnValue = returnValue + 1
      num_of_failed_tests += 1
    else:
      UtilLog.info("*************************")
      UtilLog.info("ALL " + str(len(testResults)) + " TESTS PASSED")
      UtilLog.info("*************************")
      num_of_passed_tests += 1


  num_of_total_tests = num_of_failed_tests + num_of_passed_tests
  UtilLog.info(
    "{0}/{1} ({2:.1f}%) runs failed".format(num_of_failed_tests,
                                            num_of_total_tests,
                                            float(num_of_failed_tests)/num_of_total_tests*100)
  )

  return returnValue



if __name__ == '__main__':
  args = sys.argv
  sys.exit(main(args))
