/**
 * File: testPuzzleMazeBehavior.cpp
 *
 * Author: Molly Jameson
 * Created: 2017-11-28
 *
 * Description: Unit tests and simulator for how fast cozmo runs the maze with different settings.
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

// Access protected factory functions for test purposes
#define protected public

#include "gtest/gtest.h"

#include "coretech/common/engine/utils/timer.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorPuzzleMaze.h"
#include "engine/aiComponent/puzzleComponent.h"
#include "engine/cozmoContext.h"
#include "engine/robot.h"
#include "engine/cozmoAPI/comms/uiMessageHandler.h"
#include "clad/types/behaviorComponent/behaviorTypes.h"
#include "engine/aiComponent/aiComponent.h"

#include "test/engine/behaviorComponent/testBehaviorFramework.h"

#include "coretech/common/engine/utils/data/dataPlatform.h"

using namespace Anki;
using namespace Anki::Cozmo;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CreatePuzzleMazeBehavior(Robot& robot, ICozmoBehaviorPtr& puzzleMazeBehavior, BehaviorExternalInterface& behaviorExternalInterface,
                              const Anki::Util::Data::DataPlatform& dataPlatform)
{
  ASSERT_TRUE(puzzleMazeBehavior == nullptr) << "test bug: should not have behavior yet";
  
  BehaviorContainer& behaviorContainer = robot.GetAIComponent().GetBehaviorContainer();
  
  Json::Value jsonRoot;
  dataPlatform.readAsJson(Util::Data::Scope::Resources, "config/engine/behaviorComponent/behaviors/freeplay/userInteractive/puzzleMaze.json", jsonRoot);
  
  const std::string& configStr = jsonRoot.toStyledString();

  Json::Value config;
  Json::Reader reader;
  bool parseOK = reader.parse( configStr.c_str(), config);
  ASSERT_TRUE(parseOK) << "failed to parse JSON, bug in the test";

  puzzleMazeBehavior = behaviorContainer.CreateBehaviorAndAddToContainer(BehaviorClass::PuzzleMaze,
                                                                    config);
  puzzleMazeBehavior->Init(behaviorExternalInterface);
  puzzleMazeBehavior->OnEnteredActivatableScope();
  ASSERT_TRUE(puzzleMazeBehavior != nullptr);
}

float GetTimeForCurrentPuzzle(BehaviorPuzzleMaze* puzzleMazePtr,AIComponent& aiComponent,
                              Robot& robot, BehaviorExternalInterface& behaviorExternalInterface )
{
  std::string currentActivityName;
  std::string behaviorDebugStr;
  puzzleMazePtr->SetAnimateBetweenPoints(false);
  puzzleMazePtr->OnActivated(behaviorExternalInterface);
  puzzleMazePtr->TransitionToState(BehaviorPuzzleMaze::MazeState::MazeStep);
  int i = 0;
  constexpr int maxIterations = 100000;
  while( !puzzleMazePtr->IsPuzzleCompleted() && i < maxIterations)
  {
    // Tick
    IncrementBaseStationTimerTicks();
    aiComponent.Update(robot, currentActivityName, behaviorDebugStr);
    puzzleMazePtr->Update(behaviorExternalInterface);
    i++;
  }
  // Expect every puzzle is solvable in under max iterations. This is the actually reasonable unit tests part.
  EXPECT_LT(i, maxIterations);
  float totalTime = puzzleMazePtr->GetTotalTimeFromLastRun();
  puzzleMazePtr->OnBehaviorDeactivated(behaviorExternalInterface);
  return totalTime;
}

struct MazeData
{
  MazeData()
  {
    minTime = std::numeric_limits<float>().max();
    maxTime = std::numeric_limits<float>().min();
  }
  float avgTime;
  float minTime;
  float maxTime;
  size_t width;
  size_t height;
};

void ConvertSecondsToMinutes(const float totalSeconds, int& outMinutes, int& outSeconds)
{
  outMinutes = totalSeconds/ 60;
  outSeconds = (int)(totalSeconds - (outMinutes * 60));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
TEST(PuzzleMazeBehavior, DISABLED_BalanceTool)
{
  // This isn't really a unit test, we just needed a way to show time it took to solve puzzles.
  // Just run through the test N times and print the average of each puzzle.
  
  UiMessageHandler handler(0, nullptr);
  
  char cwdPath[1256];
  getcwd(cwdPath, 1255);
  std::string path = cwdPath;
  std::string resourcePath = path + "/resources";
  std::string filesPath = path + "/files";
  std::string cachePath = path + "/temp";
  std::string externalPath = path + "/temp";
  
  // Really need a data Platform for configs.
  Anki::Util::Data::DataPlatform dataPlatform(filesPath, cachePath, externalPath, resourcePath);
  CozmoContext context(&dataPlatform, &handler);
  
  TestBehaviorFramework testBehaviorFramework(1, &context);
  RobotDataLoader::BehaviorIDJsonMap emptyBehaviorMap;
  {
    BehaviorContainer* bc = new BehaviorContainer(emptyBehaviorMap);
    testBehaviorFramework.InitializeStandardBehaviorComponent(nullptr, nullptr, true, bc);
  }
  
  Robot& robot = testBehaviorFramework.GetRobot();
  BehaviorExternalInterface& behaviorExternalInterface = testBehaviorFramework.GetBehaviorExternalInterface();
  ICozmoBehaviorPtr puzzleMazeBehavior = nullptr;
  
  CreatePuzzleMazeBehavior(robot, puzzleMazeBehavior, behaviorExternalInterface, dataPlatform);
  
  BehaviorPuzzleMaze* puzzleMazePtr = (BehaviorPuzzleMaze*)puzzleMazeBehavior.get();
  auto& aiComponent = testBehaviorFramework.GetAIComponent();
  PuzzleComponent& puzzleComp = aiComponent.GetPuzzleComponent();
  
  constexpr int maxRuns = 50;
  size_t numPuzzles = puzzleComp.GetNumMazes();
  std::map<std::string,MazeData> mazeData;
  for( int i = 0; i < numPuzzles; ++i )
  {
    MazeData currMazeData;
    std::vector<float> runTimes;
    for( int j = 0; j < maxRuns; ++j)
    {
      float time = GetTimeForCurrentPuzzle(puzzleMazePtr, aiComponent, robot, behaviorExternalInterface);
      runTimes.push_back(time);
      if( time < currMazeData.minTime )
      {
        currMazeData.minTime = time;
      }
      if( time > currMazeData.maxTime )
      {
        currMazeData.maxTime = time;
      }
    }
    float sum = std::accumulate(runTimes.begin(), runTimes.end(), 0);
    std::string id = puzzleComp.GetCurrentMaze().GetID();
    // the ID is the filename usually, but just make the short filename.
    size_t lastSlash = id.find_last_of("/");
    if( lastSlash != std::string::npos )
    {
      id = id.substr(lastSlash + 1, id.length() - lastSlash);
    }
    currMazeData.avgTime = sum / runTimes.size();
    currMazeData.width = puzzleComp.GetCurrentMaze().GetWidth();
    currMazeData.height = puzzleComp.GetCurrentMaze().GetHeight();
    mazeData[id] = currMazeData;
    puzzleComp.CompleteCurrentMaze();
  }
  PRINT_NAMED_INFO("****puzzle.info","Data after %d Runs Each *******",maxRuns);
  for( auto& puzzles : mazeData )
  {
    const char* puzzleid = puzzles.first.c_str();
    int minutes,seconds;
    ConvertSecondsToMinutes(puzzles.second.avgTime, minutes,seconds);
    PRINT_NAMED_INFO("******puzzle.info","id: %s : %zu x %zu *******",puzzleid, puzzles.second.width, puzzles.second.height);
    PRINT_NAMED_INFO("puzzle.info","id: %s : avg Time: %d minutes %d seconds",puzzleid, minutes, seconds);
    ConvertSecondsToMinutes(puzzles.second.minTime, minutes,seconds);
    PRINT_NAMED_INFO("puzzle.info","id: %s : min Time: %d minutes %d seconds",puzzleid, minutes, seconds);
    ConvertSecondsToMinutes(puzzles.second.maxTime, minutes,seconds);
    PRINT_NAMED_INFO("puzzle.info","id: %s : max Time: %d minutes %d seconds",puzzleid, minutes, seconds);
  }

}
