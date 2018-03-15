/**
 * File: xythetaActions.cpp
 *
 * Author: Michael Willett
 * Created: 2018-03-13
 *
 * Description: classes relating to actions including motion primitives
 *
 * Copyright: Anki, Inc. 2014-2018
 *
 **/

#include "xythetaActions.h"
#include "xythetaEnvironment.h"

#include "util/console/consoleInterface.h"
#include "util/jsonWriter/jsonWriter.h"

#include "coretech/common/shared/radians.h"
#include "coretech/common/engine/jsonTools.h"

#include "json/json.h"


namespace Anki {
namespace Planning {

#define LATTICE_PLANNER_ACCEL 200
#define LATTICE_PLANNER_DECEL 200

#define LATTICE_PLANNER_ROT_ACCEL 10
#define LATTICE_PLANNER_ROT_DECEL 10
  
#define LATTICE_PLANNER_POINT_TURN_TOL DEG_TO_RAD(2)

CONSOLE_VAR(f32, kXYTPlanner_PointTurnTolOverride_deg, "Planner", 2.0f);

bool MotionPrimitive::IntermediatePosition::Import(const Json::Value& config)
{
  try {
    if( config.isNull() ) {
      PRINT_NAMED_ERROR("IntermediatePosition.Import.Null", "config value is null");
      return false;
    }

    if( ! position.Import(config["position"]) ) {
      return false;
    }
  
    nearestTheta = config["theta"].asInt();
    oneOverDistanceFromLastPosition = config["inverseDist"].asFloat();
  }
  catch( const std::exception&  e ) {
    PRINT_NAMED_ERROR("IntermediatePosition.Import.Exception",
                      "json exception: %s",
                      e.what());
    return false;
  }


  return true;
}

void  MotionPrimitive::IntermediatePosition::Dump(Util::JsonWriter& writer) const
{
  writer.StartGroup("position");
  position.Dump(writer);
  writer.EndGroup();

  writer.AddEntry("theta", nearestTheta);
  writer.AddEntry("inverseDist", oneOverDistanceFromLastPosition);
}

ActionType::ActionType()
  : _extraCostFactor(0.0)
  , _id(-1)
  , _name("<invalid>")
  , _reverse(false)
{
}

bool ActionType::Import(const Json::Value& config)
{
  try {
    if( config.isNull() ) {
      PRINT_NAMED_ERROR("ActionType.Import.Null", "config value is null");
      return false;
    }

    if(!JsonTools::GetValueOptional(config, "extra_cost_factor", _extraCostFactor) ||
       !JsonTools::GetValueOptional(config, "index", _id) ||
       !JsonTools::GetValueOptional(config, "name", _name)) {
      printf("error: could not parse ActionType\n");
      JsonTools::PrintJsonCout(config, 1);
      return false;
    }
    JsonTools::GetValueOptional(config, "reverse_action", _reverse);
  }
  catch( const std::exception&  e ) {
    PRINT_NAMED_ERROR("ActionType.Import.Exception",
                      "json exception: %s",
                      e.what());
    return false;
  }

  return true;
}

void ActionType::Dump(Util::JsonWriter& writer) const
{
  writer.AddEntry("extra_cost_factor", _extraCostFactor);
  writer.AddEntry("index", _id);
  writer.AddEntry("name", _name);
  writer.AddEntry("reverse_action", _reverse);
}


bool MotionPrimitive::Import(const Json::Value& config)
{
  try {
    if( config.isNull() ) {
      return false;
    }

    if( config["action_index"].isNull() ) {
      PRINT_NAMED_ERROR("MotionPrimitive.Import.InvalidConfig",
                        "no action_index in config. dump follows");
      JsonTools::PrintJsonCout(config, 3);
      return false;
    }

    if( config["cost"].isNull() ) {
      PRINT_NAMED_ERROR("MotionPrimitive.Import.InvalidConfig2",
                        "missing 'cost' key. Did you mean to call Create() instead of Import()?");
      JsonTools::PrintJsonCout(config, 3);
      return false;
    }

    id = config["action_index"].asInt();
    startTheta = config["start_theta"].asInt();
    cost = config["cost"].asFloat();
    if( ! endStateOffset.Import(config["end_state_offset"]) ) {
      return false;
    }
  
    if(config["intermediate_poses"].size() <= 1 ) {
      PRINT_NAMED_ERROR("MotionPrimitive.Import.InvalidConfig3",
                        "'intermediate_poses' size %d too small (or not a list). Dump follows",
                        config["intermediate_poses"].size());
      JsonTools::PrintJsonCout(config["intermediate_poses"], 3);
      return false;
    }

    intermediatePositions.clear();

    for(const auto& poseConfig : config["intermediate_poses"]) {
      IntermediatePosition p;
      if( ! p.Import( poseConfig ) ) {
        return false;
      }
      intermediatePositions.push_back(p);
    }
  }
  catch( const std::exception&  e ) {
    PRINT_NAMED_ERROR("MotionPrimitive.Import.Exception",
                      "json exception: %s",
                      e.what());
    return false;
  }

  CacheBoundingBox();

  return true;
}

void MotionPrimitive::CacheBoundingBox()
{
  minX = std::numeric_limits<decltype(minX)>::max();
  maxX = std::numeric_limits<decltype(maxX)>::min();
  minY = std::numeric_limits<decltype(minY)>::max();
  maxY = std::numeric_limits<decltype(maxY)>::min();

  for( const auto& pt : intermediatePositions ) {
    if( pt.position.x_mm < minX ) {
      minX = pt.position.x_mm;
    }
    if( pt.position.x_mm > maxX ) {
      maxX = pt.position.x_mm;
    }

    if( pt.position.y_mm < minY ) {
      minY = pt.position.y_mm;
    }
    if( pt.position.y_mm > maxY ) {
      maxY = pt.position.y_mm;
    }
  }
}

void MotionPrimitive::Dump(Util::JsonWriter& writer) const
{
  writer.AddEntry("action_index", id);
  writer.AddEntry("start_theta", startTheta);
  writer.AddEntry("cost", cost);

  writer.StartGroup("end_state_offset");
  endStateOffset.Dump(writer);
  writer.EndGroup();

  writer.StartList("intermediate_poses");
  for(const auto& pose : intermediatePositions) {
    writer.NextListItem();
    pose.Dump(writer);
  }
  writer.EndList();
}


u8 MotionPrimitive::AddSegmentsToPath(State_c start, Path& path) const
{
  State_c curr(start);

  bool added = false;
  u8 firstSegment = path.GetNumSegments();

  for(u8 pathIdx = 0; pathIdx < pathSegments_.GetNumSegments(); ++pathIdx) {
    const PathSegment& seg(pathSegments_.GetSegmentConstRef(pathIdx));
    PathSegment segment(seg);
    segment.OffsetStart(curr.x_mm, curr.y_mm);

#if REMOTE_CONSOLE_ENABLED
    if( segment.GetType() == PST_POINT_TURN) {
      segment.GetDef().turn.angleTolerance = DEG_TO_RAD(kXYTPlanner_PointTurnTolOverride_deg);
    }
#endif


    float xx, yy, aa;
    segment.GetStartPoint(xx,yy);
    // printf("start: (%f, %f)\n", xx, yy);

    // if this segment can be combined with the previous one, do
    // that. otherwise, append a new segment.
    bool shouldAdd = true;
    if(path.GetNumSegments() > 0 && path[path.GetNumSegments()-1].GetType() == segment.GetType()) {
      size_t endIdx = path.GetNumSegments()-1;

      switch(segment.GetType()) {

      case PST_LINE:
      {
        bool oldSign = path[path.GetNumSegments()-1].GetTargetSpeed() > 0;
        bool newSign = segment.GetTargetSpeed() > 0;
        if(oldSign == newSign) {
          path[endIdx].GetDef().line.endPt_x = segment.GetDef().line.endPt_x;
          path[endIdx].GetDef().line.endPt_y = segment.GetDef().line.endPt_y;
          shouldAdd = false;
        }
        break;
      }

      case PST_ARC:
        // TODO:(bn) had to disable this because it was combing arcs
        // that the robot was going to split up. This doesn't happen
        // in the lattice planner anyway (its always line, arc for
        // each turn action)

        // if(FLT_NEAR(path[endIdx].GetDef().arc.centerPt_x, segment.GetDef().arc.centerPt_x) &&
        //        FLT_NEAR(path[endIdx].GetDef().arc.centerPt_y, segment.GetDef().arc.centerPt_y) &&
        //        FLT_NEAR(path[endIdx].GetDef().arc.radius, segment.GetDef().arc.radius)) {

        //   path[endIdx].GetDef().arc.sweepRad += segment.GetDef().arc.sweepRad;
        //   shouldAdd = false;
        // }
        break;

      case PST_POINT_TURN:
        // only combine point turns if they are the same and the new
        // target angle is less that 180 degrees away from the current
        // angle
        if(FLT_NEAR(path[endIdx].GetDef().turn.x, segment.GetDef().turn.x) &&
           FLT_NEAR(path[endIdx].GetDef().turn.y, segment.GetDef().turn.y) &&
           FLT_NEAR(path[endIdx].GetTargetSpeed(), segment.GetTargetSpeed())) {
          path[endIdx].GetDef().turn.targetAngle = segment.GetDef().turn.targetAngle;
          shouldAdd = false;
        }
        break;

      default:
        printf("ERROR (AddSegmentsToPath): Undefined segment %d\n", segment.GetType());
        assert(false);
      }
    }

    if(shouldAdd) {
      path.AppendSegment(segment);
      added = true;
    }

    segment.GetEndPose(xx,yy,aa);
    // printf("end: (%f, %f)\n", xx, yy);
  }

  if(!added && firstSegment > 0)
    firstSegment--;

  return firstSegment;
}


bool MotionPrimitive::Create(const Json::Value& config, GraphTheta startingAngle, const xythetaEnvironment& env)
{
  startTheta = startingAngle;

  if(!JsonTools::GetValueOptional(config, "action_index", id)) {
    printf("error: missing key 'action_index'\n");
    JsonTools::PrintJsonCout(config, 1);
    return false;
  }
  assert(id >= 0);

  if(!endStateOffset.Import(config["end_pose"])) {
    printf("error: could not read 'end_pose'\n");
    return false;
  }

  unsigned int numIntermediatePoses = config["intermediate_poses"].size();
  for(unsigned int i=0; i<numIntermediatePoses; ++i) {
    State_c s;
    if(!s.Import(config["intermediate_poses"][i])) {
      printf("error: could not read 'intermediate_poses'[%d]\n", i);
        return false;
    }

    State_c old(0, 0, 0);
    float penalty = 0.0;

    if(!intermediatePositions.empty()) {
      old = intermediatePositions.back().position;

      float dist = env.GetDistanceBetween(old, s);

      // TODO:(bn) use actual time / cost computation!
      float cost = dist;

      Radians deltaTheta = Radians(s.theta) - Radians(old.theta);
      cost += std::abs(deltaTheta.ToFloat()) *
        env._robotParams.halfWheelBase_mm *
        env._robotParams.oneOverMaxVelocity;

      penalty = 1.0 / cost;
    }

    intermediatePositions.emplace_back(s, env.GetTheta(s.theta),  penalty);
  }

  if(config.isMember("extra_cost_factor")) {
    printf("ERROR: individual primitives shouldn't have cost factors. Old file format?\n");
    return false;
  }

  double linearSpeed = env._robotParams.maxVelocity_mmps;
  double oneOverLinearSpeed = env._robotParams.oneOverMaxVelocity;
  bool isReverse = env.GetActionType(id).IsReverseAction();
  if(isReverse) {
    linearSpeed = env.GetMaxReverseVelocity_mmps();
    oneOverLinearSpeed = 1.0 / env._robotParams.maxReverseVelocity_mmps;
  }

  // Compute cost based on the action. Cost is time in seconds
  cost = 0.0;

#ifdef HACK_USE_FIXED_SPEED
  linearSpeed = HACK_USE_FIXED_SPEED;
  oneOverLinearSpeed = 1.0 / linearSpeed;
#endif

  double length = std::abs(config["straight_length_mm"].asDouble());
  if(length > 0.0) {
    cost += length * oneOverLinearSpeed;

    float signedLength = config["straight_length_mm"].asFloat();
    
    if(std::abs(signedLength) > 0.001) {
      pathSegments_.AppendLine(0.0,
                               0.0,
                               signedLength * cos(env.GetTheta_c(startingAngle)),
                               signedLength * sin(env.GetTheta_c(startingAngle)),
                               isReverse ? -linearSpeed : linearSpeed,
                               LATTICE_PLANNER_ACCEL,
                               LATTICE_PLANNER_DECEL);
    }
  }

  if(config.isMember("arc")) {
    // the section of the angle we will sweep through
    double deltaTheta = std::abs(config["arc"]["sweepRad"].asDouble());

    // the radius of the circle that the outer wheel will follow
    double turningRadius = std::abs(config["arc"]["radius_mm"].asDouble());
    double radius_mm = turningRadius + env._robotParams.halfWheelBase_mm;

    // the total time is the arclength of the outer wheel arc divided by the max outer wheel speed
    Cost arcTime = deltaTheta * radius_mm * oneOverLinearSpeed;
    cost += arcTime;

    Cost arcSpeed = deltaTheta * turningRadius / arcTime;

    // TODO:(bn) these don't work properly backwards at the moment

#ifdef HACK_USE_FIXED_SPEED
    arcSpeed = HACK_USE_FIXED_SPEED;
#endif

    pathSegments_.AppendArc(config["arc"]["centerPt_x_mm"].asFloat(),
                            config["arc"]["centerPt_y_mm"].asFloat(),
                            config["arc"]["radius_mm"].asFloat(),
                            config["arc"]["startRad"].asFloat(),
                            config["arc"]["sweepRad"].asFloat(),
                            isReverse ? -arcSpeed : arcSpeed,
                            LATTICE_PLANNER_ACCEL,
                            LATTICE_PLANNER_DECEL);
  }
  else if(config.isMember("turn_in_place_direction")) {
    double direction = config["turn_in_place_direction"].asDouble();

    // turn in place is just like an arc with radius 0
    Radians startRads(env.GetTheta_c(startTheta));
    double deltaTheta = startRads.angularDistance(env.GetTheta_c(endStateOffset.theta), direction < 0);

    Cost turnTime = std::abs(deltaTheta) * env._robotParams.halfWheelBase_mm * oneOverLinearSpeed;
    cost += turnTime;

    float rotSpeed = deltaTheta / turnTime;

    pathSegments_.AppendPointTurn(0.0,
                                  0.0,
                                  startRads.ToFloat(),
                                  env.GetTheta_c(endStateOffset.theta),
                                  rotSpeed,
                                  LATTICE_PLANNER_ROT_ACCEL,
                                  LATTICE_PLANNER_ROT_DECEL,
                                  LATTICE_PLANNER_POINT_TURN_TOL,
                                  true);
  }

  assert(env.GetNumActions() > id);

  if(cost < 1e-6) {
    printf("ERROR: base action cost is %f for action %d '%s'\n", cost, id, env.GetActionType(id).GetName().c_str());
    return false;
  }

  // printf("from angle %2d %20s costs %f * %f = %f\n",
  //            startTheta,
  //            env.GetActionType(id).GetName().c_str(),
  //            cost,
  //            env.GetActionType(id).GetExtraCostFactor(),
  //            cost * env.GetActionType(id).GetExtraCostFactor());

  cost *= env.GetActionType(id).GetExtraCostFactor();

  if(cost < 1e-6) {
    printf("ERROR: final action cost is %f (%f x) for action %d '%s\n",
           cost,
           env.GetActionType(id).GetExtraCostFactor(),
           id, env.GetActionType(id).GetName().c_str());
    return false;
  }

  CacheBoundingBox();

  return true;
}

SuccessorIterator::SuccessorIterator(const xythetaEnvironment* env, StateID startID, Cost startG, bool reverse)
  : start_c_(env->StateID2State_c(startID))
  , start_(startID)
  , startG_(startG)
  , nextAction_(0)
  , reverse_(reverse)
{
  assert(start_.theta == xythetaEnvironment::GetThetaFromStateID(startID));
}

// TODO:(bn) inline?
bool SuccessorIterator::Done(const xythetaEnvironment& env) const
{
  if( ! reverse_ ) {
    return nextAction_ > env.allMotionPrimitives_[start_.theta].size();
  }
  else {
    return nextAction_ > env.reverseMotionPrimitives_[start_.theta].size();
  }
}

void SuccessorIterator::Next(const xythetaEnvironment& env)
{
  size_t numActions = 0;

  if( ! reverse_ ) {
    numActions = env.allMotionPrimitives_[start_.theta].size();
  }
  else {
    numActions = env.reverseMotionPrimitives_[start_.theta].size();
  }

  while(nextAction_ < numActions) {
    const MotionPrimitive* prim = nullptr; 
    if( ! reverse_ ) {
      prim = &env.allMotionPrimitives_[start_.theta][nextAction_];
    }
    else { 
      prim = &env.reverseMotionPrimitives_[start_.theta][nextAction_];
    }

    // collision checking
    long endPoints = prim->intermediatePositions.size();
    bool collision = false; // fatal collision
    bool reverseMotion = env.GetActionType(prim->id).IsReverseAction();

    nextSucc_.g = 0;

    Cost penalty = 0.0f;

    // first, check if we are well-clear of everything, and can skip this check
    bool possibleObstacle = false;

    State_c primtiveOffset = start_c_;

    GraphState result(start_);
    result.x += prim->endStateOffset.x;
    result.y += prim->endStateOffset.y;
    result.theta = prim->endStateOffset.theta;


    if( reverse_ ) {
      primtiveOffset = env.State2State_c(result);
    }

    float minPrimX = prim->minX + primtiveOffset.x_mm;
    float maxPrimX = prim->maxX + primtiveOffset.x_mm;
    float minPrimY = prim->minY + primtiveOffset.y_mm;
    float maxPrimY = prim->maxY + primtiveOffset.y_mm;

    if( env.obstacleBounds_.empty() && ! env.obstaclesPerAngle_[0].empty() ) {
      // unit tests might do this
      PRINT_NAMED_WARNING("xythetaEnvironment.Successor.NoBounds",
                          "missing obstacle bounding boxes! Did you call env.PrepareForPlanning()???");
      possibleObstacle = true;      
    }
    else {
      for( const auto& bound : env.obstacleBounds_ ) {
        if( maxPrimX < bound.minX ||
            minPrimX > bound.maxX ||
            maxPrimY < bound.minY ||
            minPrimY > bound.maxY ) {
          // can't possibly be a collision
          continue;
        }
        // otherwise, we need to do a full check
        possibleObstacle = true;
        break;
      }
    }

    
    if( possibleObstacle ) {

      // two collision check cases. If the angle is changing, then we'll need to potentially switch which
      // obstacle angle we check while checking, so that is the more complciated case

      // First, handle the simpler case, for straight lines. In this case, we can do a quick bounding box check first

      if( prim->endStateOffset.theta == prim->startTheta ) {
        for( const auto& obs : env.obstaclesPerAngle_[prim->startTheta] ) {

          if( maxPrimX < obs.first.GetMinX() ||
              minPrimX > obs.first.GetMaxX() ||
              maxPrimY < obs.first.GetMinY() ||
              minPrimY > obs.first.GetMaxY() ) {
            // can't possibly be a collision, rule out this whole obstacle
            continue;
          }

          for( const auto& pt : prim->intermediatePositions ) {
            if( obs.first.Contains(primtiveOffset.x_mm + pt.position.x_mm,
                                   primtiveOffset.y_mm + pt.position.y_mm ) ) {

              if(obs.second >= MAX_OBSTACLE_COST) {
                collision = true;
                break;
              }
              else {
                // apply soft penalty, but allow the action
                penalty += obs.second * pt.oneOverDistanceFromLastPosition 
                        +  (reverseMotion ? REVERSE_OVER_OBSTACLE_COST : 0);

                assert(!isinf(penalty));
                assert(!isnan(penalty));
              }
            }
          }
        }
      }

      else {
        // handle the more complex case

        for(long pointIdx = endPoints-1; pointIdx >= 0; --pointIdx) {

          GraphTheta angle = prim->intermediatePositions[pointIdx].nearestTheta;
          for( const auto& obs : env.obstaclesPerAngle_[angle] ) {
            if( obs.first.Contains(
                  primtiveOffset.x_mm + prim->intermediatePositions[pointIdx].position.x_mm,
                  primtiveOffset.y_mm + prim->intermediatePositions[pointIdx].position.y_mm ) ) {

              if(obs.second >= MAX_OBSTACLE_COST) {
                collision = true;
                break;
              }
              else {
                // apply soft penalty, but allow the action
                penalty += obs.second  * prim->intermediatePositions[pointIdx].oneOverDistanceFromLastPosition
                        +  (reverseMotion ? REVERSE_OVER_OBSTACLE_COST : 0);

                assert(!isinf(penalty));
                assert(!isnan(penalty));
              }
            }
          }
        }
      }
    }

    assert(!isinf(penalty));
    assert(!isnan(penalty));

    nextSucc_.g += penalty;

    if(!collision) {
      nextSucc_.stateID = result.GetStateID();
      nextSucc_.g += startG_ + prim->cost;

      assert(!isinf(nextSucc_.g));
      assert(!isnan(nextSucc_.g));

      nextSucc_.penalty = penalty;
      nextSucc_.actionID = prim->id;
      assert( reverse_ || nextAction_ == prim->id);
      break;
    }

    nextAction_++;
  }

  nextAction_++;
}


} // Planning
} // Anki