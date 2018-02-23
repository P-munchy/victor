/**
 * File: testBeiConditions.cpp
 *
 * Author: Brad Neuman
 * Created: 2018-01-16
 *
 * Description: Unit tests for BEI Conditions
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "gtest/gtest.h"

// access robot internals for test
#define private public
#define protected public

#include "clad/types/behaviorComponent/userIntent.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorComponent.h"
#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/beiConditions/beiConditionFactory.h"
#include "engine/aiComponent/beiConditions/conditions/conditionLambda.h"
#include "engine/aiComponent/beiConditions/conditions/conditionNegate.h"
#include "engine/aiComponent/beiConditions/conditions/conditionUserIntentPending.h"
#include "engine/aiComponent/beiConditions/iBEICondition.h"
#include "engine/moodSystem/moodManager.h"
#include "engine/robot.h"
#include "test/engine/behaviorComponent/testBehaviorFramework.h"
#include "util/math/math.h"
#include "util/console/consoleInterface.h"

using namespace Anki;
using namespace Anki::Cozmo;

namespace {

void CreateBEI(const std::string& json, IBEIConditionPtr& cond)
{
  Json::Reader reader;
  Json::Value config;
  const bool parsedOK = reader.parse(json, config, false);
  ASSERT_TRUE(parsedOK);

  cond = BEIConditionFactory::CreateBEICondition(config, "testing");

  ASSERT_TRUE( cond != nullptr );
}

class TestCondition : public IBEICondition
{
public:
  explicit TestCondition()
    // use an arbitrary type to make the system happy
    : IBEICondition(IBEICondition::GenerateBaseConditionConfig(BEIConditionType::TrueCondition))
    {
    }

  virtual void InitInternal(BehaviorExternalInterface& behaviorExternalInterface) override {
    _initCount++;
  }

  virtual void SetActiveInternal(BehaviorExternalInterface& behaviorExternalInterface, bool setActive) override {
    if(setActive){
      _setActiveCount++;
    } 
  }

  virtual bool AreConditionsMetInternal(BehaviorExternalInterface& behaviorExternalInterface) const override {
    _areMetCount++;
    return _val;
  }

  bool _val = false;

  int _initCount = 0;
  int _setActiveCount = 0;
  mutable int _areMetCount = 0;  
};

}

TEST(BeiConditions, TestCondition)
{
  // a test of the test, if you will

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();

  auto cond = std::make_shared<TestCondition>();

  EXPECT_EQ(cond->_initCount, 0);
  EXPECT_EQ(cond->_setActiveCount, 0);
  EXPECT_EQ(cond->_areMetCount, 0);

  cond->Init(bei);
  EXPECT_EQ(cond->_initCount, 1);
  EXPECT_EQ(cond->_setActiveCount, 0);
  EXPECT_EQ(cond->_areMetCount, 0);

  cond->SetActive(bei, true);
  EXPECT_EQ(cond->_initCount, 1);
  EXPECT_EQ(cond->_setActiveCount, 1);
  EXPECT_EQ(cond->_areMetCount, 0);

  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_EQ(cond->_initCount, 1);
  EXPECT_EQ(cond->_setActiveCount, 1);
  EXPECT_EQ(cond->_areMetCount, 1);

  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_EQ(cond->_initCount, 1);
  EXPECT_EQ(cond->_setActiveCount, 1);
  EXPECT_EQ(cond->_areMetCount, 2);

  cond->_val = true;

  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_EQ(cond->_initCount, 1);
  EXPECT_EQ(cond->_setActiveCount, 1);
  EXPECT_EQ(cond->_areMetCount, 3);

}

TEST(BeiConditions, CreateLambda)
{
  bool val = false;
  
  auto cond = std::make_shared<ConditionLambda>(
    [&val](BehaviorExternalInterface& behaviorExternalInterface) {
      return val;
    });

  ASSERT_TRUE( cond != nullptr );

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();
  // don't call init, should be ok if no one uses it
  // bei.Init();
  
  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  val = true;
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, True)
{
  const std::string json = R"json(
  {
    "conditionType": "TrueCondition" 
  })json";

  IBEIConditionPtr cond;
  CreateBEI(json, cond);

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();
  
  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, Frustration)
{
  const std::string json = R"json(
  {
    "conditionType": "Frustration",
    "frustrationParams": {
      "maxConfidence": -0.5
    }
  })json";

  IBEIConditionPtr cond;
  CreateBEI(json, cond);

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();
  
  Robot& robot = testBehaviorFramework.GetRobot();
  BEIRobotInfo info(robot);
  MoodManager moodManager;
  InitBEIPartial( { {BEIComponentID::MoodManager, &moodManager}, {BEIComponentID::RobotInfo, &info} }, bei );
  
  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  moodManager.SetEmotion(EmotionType::Confident, -1.0f);
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  moodManager.SetEmotion(EmotionType::Confident, 1.0f);
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, Timer)
{
  BaseStationTimer::getInstance()->UpdateTime(0);
  
  const std::string json = R"json(
  {
    "conditionType": "TimerInRange",
    "begin_s": 30.0,
    "end_s": 35.0
  })json";

  IBEIConditionPtr cond;
  CreateBEI(json, cond);

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();

  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(2.0));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(29.9));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(30.01));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(34.0));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(35.01));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(900.0));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  const float resetTime_s = 950.0f;
  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s));
  cond->SetActive(bei, true);
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 1.0f));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 29.0f));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 30.01f));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 34.7f));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 40.0f));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 80.0f));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, Negate)
{
  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();

  auto subCond = std::make_shared<TestCondition>();

  auto cond = std::make_shared<ConditionNegate>(subCond);

  EXPECT_EQ(subCond->_initCount, 0);
  EXPECT_EQ(subCond->_setActiveCount, 0);
  EXPECT_EQ(subCond->_areMetCount, 0);

  cond->Init(bei);
  EXPECT_EQ(subCond->_initCount, 1);
  EXPECT_EQ(subCond->_setActiveCount, 0);
  EXPECT_EQ(subCond->_areMetCount, 0);

  cond->SetActive(bei, true);
  EXPECT_EQ(subCond->_initCount, 1);
  EXPECT_EQ(subCond->_setActiveCount, 1);
  EXPECT_EQ(subCond->_areMetCount, 0);


  EXPECT_TRUE(cond->AreConditionsMet(bei));
  EXPECT_EQ(subCond->_initCount, 1);
  EXPECT_EQ(subCond->_setActiveCount, 1);
  EXPECT_EQ(subCond->_areMetCount, 1);

  EXPECT_TRUE(cond->AreConditionsMet(bei));
  EXPECT_EQ(subCond->_initCount, 1);
  EXPECT_EQ(subCond->_setActiveCount, 1);
  EXPECT_EQ(subCond->_areMetCount, 2);

  subCond->_val = true;
  EXPECT_FALSE(cond->AreConditionsMet(bei));
  EXPECT_EQ(subCond->_initCount, 1);
  EXPECT_EQ(subCond->_setActiveCount, 1);
  EXPECT_EQ(subCond->_areMetCount, 3);

  EXPECT_EQ(subCond->_initCount, 1);
  EXPECT_EQ(subCond->_setActiveCount, 1);
  EXPECT_EQ(subCond->_areMetCount, 3);

  EXPECT_FALSE(cond->AreConditionsMet(bei));
  EXPECT_EQ(subCond->_initCount, 1);
  EXPECT_EQ(subCond->_setActiveCount, 1);
  EXPECT_EQ(subCond->_areMetCount, 4);

}

TEST(BeiConditions, NegateTrue)
{
  const std::string json = R"json(
  {
    "conditionType": "Negate",
    "operand": {
      "conditionType": "TrueCondition"
    }
  })json";

  IBEIConditionPtr cond;
  CreateBEI(json, cond);

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();
  
  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  cond->SetActive(bei, true);
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, NegateTimerInRange)
{
  BaseStationTimer::getInstance()->UpdateTime(0);
  
  const std::string json = R"json(
  {
    "conditionType": "Negate",
    "operand": {
      "conditionType": "TimerInRange",
      "begin_s": 30.0,
      "end_s": 35.0
    }
  })json";

  IBEIConditionPtr cond;
  CreateBEI(json, cond);

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();

  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(2.0));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(29.9));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(30.01));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(34.0));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(35.01));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(900.0));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  const float resetTime_s = 950.0f;
  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s));
  cond->SetActive(bei, true);
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 1.0f));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 29.0f));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 30.01f));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 34.7f));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 40.0f));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(resetTime_s + 80.0f));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, OnCharger)
{
  const std::string json = R"json(
  {
    "conditionType": "OnCharger"
  })json";

  IBEIConditionPtr cond;
  CreateBEI(json, cond);

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();

  TestBehaviorFramework tbf(1, nullptr);
  Robot& robot = tbf.GetRobot();
  
  BEIRobotInfo info(robot);
  InitBEIPartial( { {BEIComponentID::RobotInfo, &info} }, bei );
  
  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  // charger implies platform here
  robot.SetOnCharger(true);
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  // off charger, but still on platform
  robot.SetOnCharger(false);
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );

  robot.SetOnChargerPlatform(false);
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  // just on platform
  robot.SetOnChargerPlatform(true);
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, TimedDedup)
{
  BaseStationTimer::getInstance()->UpdateTime(0);
  
  const std::string json = R"json(
  {
    "conditionType": "TimedDedup",
    "dedupInterval_ms" : 4000.0,
    "subCondition": {
      "conditionType": "TrueCondition"
    }
  })json";

  IBEIConditionPtr cond;
  CreateBEI(json, cond);

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();

  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(2.0));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(3.9));
  EXPECT_FALSE( cond->AreConditionsMet(bei) );

  BaseStationTimer::getInstance()->UpdateTime(Util::SecToNanoSec(4.1));
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, TriggerWordPending)
{
  BaseStationTimer::getInstance()->UpdateTime(0);
  
  const std::string json = R"json(
  {
    "conditionType": "TriggerWordPending"
  })json";
  
  IBEIConditionPtr cond;
  CreateBEI(json, cond);
  
  TestBehaviorFramework tbf(1, nullptr);
  tbf.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = tbf.GetBehaviorExternalInterface();
  
  cond->Init(bei);
  cond->SetActive(bei, true);
  
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  
  auto& uic = bei.GetAIComponent().GetBehaviorComponent().GetUserIntentComponent();
  uic.SetTriggerWordPending();
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  uic.SetTriggerWordPending();
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  
  uic.ClearPendingTriggerWord();
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
}

TEST(BeiConditions, UserIntentPending)
{
  BaseStationTimer::getInstance()->UpdateTime(0);
  
  const std::string json = R"json(
  {
    "conditionType": "UserIntentPending",
    "list": [
      {
        "type": "test_user_intent_1"
      },
      {
        "type": "set_timer"
      },
      {
        "type": "test_name",
        "name": ""
      },
      {
        "type": "test_timeWithUnits",
        "time": 60,
        "units": "m"
      },
      {
        "type": "test_name",
        "_lambda": "test_lambda"
      }
    ]
  })json";
  // in the above, the condition should fire if
  // (1) test_user_intent_1  matches the tag
  // (2) set_timer           matches the tag
  // (3) test_name           matches the tag and name must strictly be empty
  // (4) test_timeWithUnits  matches the tag and and data
  // (5) test_name           matches the tag and lambda must eval (name must be Victor)
  
  IBEIConditionPtr ptr;
  std::shared_ptr<ConditionUserIntentPending> cond;
  CreateBEI( json, ptr );
  cond = std::dynamic_pointer_cast<ConditionUserIntentPending>(ptr);
  ASSERT_NE( cond, nullptr );
  
  TestBehaviorFramework tbf(1, nullptr);
  tbf.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = tbf.GetBehaviorExternalInterface();
  
  cond->Init(bei);
  cond->SetActive(bei, true);
  
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  
  auto& uic = bei.GetAIComponent().GetBehaviorComponent().GetUserIntentComponent();
  
  // (1) test_user_intent_1  matches the tag
  
  uic.SetUserIntentPending( USER_INTENT(test_user_intent_1) ); // right intent
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_EQ( cond->GetUserIntentTagSelected(), USER_INTENT(test_user_intent_1) );
  
  
  uic.ClearUserIntent( USER_INTENT(test_user_intent_1) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) ); // no intent
  
  UserIntent_Test_TimeWithUnits timeWithUnits;
  uic.SetUserIntentPending( UserIntent::Createtest_timeWithUnits(std::move(timeWithUnits)) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) ); // wrong intent
  uic.ClearUserIntent( USER_INTENT(test_timeWithUnits) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) ); // no intent
  
  // (2) set_timer  matches the tag
  
  UserIntent_TimeInSeconds timeInSeconds1; // default
  UserIntent_TimeInSeconds timeInSeconds2{10}; // non default
  uic.SetUserIntentPending( UserIntent::Createset_timer(std::move(timeInSeconds1)) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) ); // correct intent
  EXPECT_EQ( cond->GetUserIntentTagSelected(), USER_INTENT(set_timer) );
  uic.ClearUserIntent( USER_INTENT(set_timer) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  uic.SetUserIntentPending( UserIntent::Createset_timer(std::move(timeInSeconds2)) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) ); // correct intent
  EXPECT_EQ( cond->GetUserIntentTagSelected(), USER_INTENT(set_timer) );
  uic.ClearUserIntent( USER_INTENT(set_timer) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  
  // (3) test_name           matches the tag and name must strictly be empty
  
  UserIntent_Test_Name name1; // default
  UserIntent_Test_Name name2{"whizmo"}; // non default
  uic.SetUserIntentPending( UserIntent::Createtest_name(std::move(name1)) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) ); // correct intent
  EXPECT_EQ( cond->GetUserIntentTagSelected(), USER_INTENT(test_name) );
  uic.ClearUserIntent( USER_INTENT(test_name) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  uic.SetUserIntentPending( UserIntent::Createtest_name(std::move(name2)) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) ); // wrong intent
  uic.ClearUserIntent( USER_INTENT(test_name) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  
  // (4) test_timeWithUnits  matches the tag and data (60mins)
  
  UserIntent_Test_TimeWithUnits timeWithUnits1{60, UserIntent_Test_Time_Units::m};
  UserIntent_Test_TimeWithUnits timeWithUnits2{20, UserIntent_Test_Time_Units::m};
  uic.SetUserIntentPending( UserIntent::Createtest_timeWithUnits(std::move(timeWithUnits1)) );
  EXPECT_TRUE( cond->AreConditionsMet(bei) ); // correct intent
  EXPECT_EQ( cond->GetUserIntentTagSelected(), USER_INTENT(test_timeWithUnits) );
  uic.ClearUserIntent( USER_INTENT(test_timeWithUnits) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  uic.SetUserIntentPending( UserIntent::Createtest_timeWithUnits(std::move(timeWithUnits2)) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) ); // wrong data
  uic.ClearUserIntent( USER_INTENT(test_timeWithUnits) );
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  
  // (5) test_name           matches the tag and lambda must eval (name must be Victor)
  
  UserIntent name5;
  name5.Set_test_name( UserIntent_Test_Name{"Victor"} );
  uic.SetUserIntentPending( std::move(name5) ); // right intent with right data
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  EXPECT_EQ( cond->GetUserIntentTagSelected(), USER_INTENT(test_name) );
  uic.ClearUserIntent( USER_INTENT(test_name) );
}

CONSOLE_VAR( unsigned int, kTestBEIConsoleVar, "unit tests", 0);
TEST(BeiConditions, ConsoleVar)
{
  const std::string json = R"json(
  {
    "conditionType": "ConsoleVar",
    "variable": "TestBEIConsoleVar",
    "value": 5
  })json";

  IBEIConditionPtr cond;
  CreateBEI(json, cond);

  TestBehaviorFramework testBehaviorFramework(1, nullptr);
  testBehaviorFramework.InitializeStandardBehaviorComponent();
  BehaviorExternalInterface& bei = testBehaviorFramework.GetBehaviorExternalInterface();
  
  cond->Init(bei);
  cond->SetActive(bei, true);

  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  kTestBEIConsoleVar = 1;
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
  kTestBEIConsoleVar = 5;
  EXPECT_TRUE( cond->AreConditionsMet(bei) );
  kTestBEIConsoleVar = 1;
  EXPECT_FALSE( cond->AreConditionsMet(bei) );
}

