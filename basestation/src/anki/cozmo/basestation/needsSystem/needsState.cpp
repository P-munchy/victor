/**
 * File: needsState
 *
 * Author: Paul Terry
 * Created: 04/12/2017
 *
 * Description: State data for Cozmo's Needs system
 *
 * Copyright: Anki, Inc. 2017
 *
 **/


#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/needsSystem/needsManager.h"
#include "anki/cozmo/basestation/needsSystem/needsState.h"
#include "anki/cozmo/basestation/robot.h"
#include <assert.h>


namespace Anki {
namespace Cozmo {


NeedsState::NeedsState()
: _timeLastWritten(Time())
, _timeLastDisconnect(Time())
, _timeLastAppBackgrounded(Time())
, _timesOpenedSinceLastDisconnect(0)
, _robotSerialNumber(0)
, _rng(nullptr)
, _curNeedsLevels()
, _partIsDamaged()
, _curNeedsUnlockLevel(0)
, _numStarsAwarded(0)
, _numStarsForNextUnlock(1)
, _timeLastStarAwarded(Time())
, _forceNextSong(UnlockId::Invalid)
, _needsConfig(nullptr)
, _starRewardsConfig(nullptr)
, _curNeedsBracketsCache()
, _prevNeedsBracketsCache()
, _needsBracketsDirty(true)
{
}

NeedsState::~NeedsState()
{
  Reset();
}


void NeedsState::Init(NeedsConfig& needsConfig, const u32 serialNumber,
                      const std::shared_ptr<StarRewardsConfig> starRewardsConfig, Util::RandomGenerator* rng)
{
  Reset();

  _timeLastWritten         = Time();  // ('never')
  _timeLastDisconnect      = Time();  // ('never')
  _timeLastAppBackgrounded = Time();  // ('never')
  _timesOpenedSinceLastDisconnect = 0;

  _needsConfig = &needsConfig;

  _robotSerialNumber = serialNumber;

  _rng = rng;

  for (int i = 0; i < static_cast<int>(NeedId::Count); i++)
  {
    const auto& needId = static_cast<NeedId>(i);
    _curNeedsLevels[needId] = needsConfig._initialNeedsLevels[needId];
  }

  _needsBracketsDirty = true;
  UpdateCurNeedsBrackets(needsConfig._needsBrackets);
  
  for (int i = 0; i < RepairablePartIdNumEntries; i++)
  {
    const auto& repairablePartId = static_cast<RepairablePartId>(i);
    _partIsDamaged[repairablePartId] = false;
  }

  _starRewardsConfig = starRewardsConfig;
  
  _curNeedsUnlockLevel = 0;
  _numStarsAwarded = 0;
  _numStarsForNextUnlock = _starRewardsConfig->GetMaxStarsForLevel(0);
  _forceNextSong = UnlockId::Invalid;
}


void NeedsState::Reset()
{
  _curNeedsLevels.clear();
  _curNeedsBracketsCache.clear();
  _prevNeedsBracketsCache.clear();
  _partIsDamaged.clear();

  _needsBracketsDirty = true;
}


void NeedsState::SetDecayMultipliers(const DecayConfig& decayConfig, std::array<float, (size_t)NeedId::Count>& multipliers)
{
  PRINT_CH_INFO(NeedsManager::kLogChannelName, "NeedsState.SetDecayMultipliers",
                "Setting needs decay multipliers");

  // Set some decay rate multipliers, based on config data, and the CURRENT needs levels:

  // Note that for long time periods (i.e. unconnected), we won't handle the progression across
  // multiple tiers of brackets FOR MULTIPLIER PURPOSES, but design doesn't want any multipliers
  // for unconnected decay anyway.  We do, however handle multiple tiers properly when we apply
  // decay in ApplyDecay.

  multipliers.fill(1.0f);

  for (int needIndex = 0; needIndex < (size_t)NeedId::Count; needIndex++)
  {
    const DecayModifiers& modifiers = decayConfig._decayModifiersByNeed[needIndex];
    if (!modifiers.empty()) // (It's OK for there to be no modifiers)
    {
      const float curNeedLevel = _curNeedsLevels[static_cast<NeedId>(needIndex)];

      // Note that the modifiers are assumed to be in descending order by threshold
      int modifierIndex = 0;
      for ( ; modifierIndex < modifiers.size(); modifierIndex++)
      {
        if (curNeedLevel >= modifiers[modifierIndex]._threshold)
        {
          break;
        }
      }
      // We can get here with an out of range index, because the last threshold
      // in the list does not have to be zero...
      if (modifierIndex < modifiers.size())
      {
        const OtherNeedModifiers& otherNeedModifiers = modifiers[modifierIndex]._otherNeedModifiers;

        for (const auto& onm : otherNeedModifiers)
        {
          int otherNeedIndex = static_cast<int>(onm._otherNeedID);
          multipliers[otherNeedIndex] *= onm._multiplier;
        }
      }
    }
  }
}

void NeedsState::ApplyDecay(const DecayConfig& decayConfig, const int needIndex, const float timeElasped_s, const NeedsMultipliers& multipliers)
{
  PRINT_CH_INFO(NeedsManager::kLogChannelName, "NeedsState.ApplyDecay",
                "Decaying need index %d with elapsed time of %f seconds", needIndex, timeElasped_s);
  
  // This handles any time elapsed passed in
  const NeedId needId = static_cast<NeedId>(needIndex);
  float curNeedLevel = _curNeedsLevels[needId];
  const DecayRates& rates = decayConfig._decayRatesByNeed[needIndex];

  // Find the decay 'bracket' the level is currently in
  // Note that the rates are assumed to be in descending order by threshold
  int rateIndex = 0;
  for ( ; rateIndex < rates.size(); rateIndex++)
  {
    if (curNeedLevel >= rates[rateIndex]._threshold)
    {
      break;
    }
  }
  if (rateIndex >= rates.size())
  {
    // Can happen if bottom bracket is non-zero threshold, or there are
    // no brackets at all; in those cases, just don't decay
    return;
  }

  float timeRemaining_min = (timeElasped_s / 60.0f);
  while (timeRemaining_min > 0.0f)
  {
    const DecayRate& rate = rates[rateIndex];
    const float bottomThreshold = rate._threshold;
    const float decayRatePerMin = rate._decayPerMinute * multipliers[needIndex];

    if (decayRatePerMin <= 0.0f)
    {
      break;  // Done if no decay (and avoid divide by zero below)
    }

    const float timeToBottomThreshold_min = (curNeedLevel - bottomThreshold) / decayRatePerMin;
    if (timeRemaining_min > timeToBottomThreshold_min)
    {
      timeRemaining_min -= timeToBottomThreshold_min;
      curNeedLevel = bottomThreshold;
      if (++rateIndex >= rates.size())
        break;
    }
    else
    {
      curNeedLevel -= (timeRemaining_min * decayRatePerMin);
      break;
    }
  }

  if (curNeedLevel < _needsConfig->_minNeedLevel)
  {
    curNeedLevel = _needsConfig->_minNeedLevel;
  }

  _curNeedsLevels[needId] = curNeedLevel;
  _needsBracketsDirty = true;

  if (needId == NeedId::Repair)
  {
    PossiblyDamageParts(NeedsActionId::Decay);
  }
}


bool NeedsState::ApplyDelta(const NeedId needId, const NeedDelta& needDelta, const NeedsActionId cause)
{
  bool startFullnessCooldown = false;

  float needLevel = _curNeedsLevels[needId];

  const float randDist = _rng->RandDbl(needDelta._randomRange * 2.0f) - needDelta._randomRange;
  const float delta = (needDelta._delta + randDist);
  needLevel += delta;
  needLevel = Util::Clamp(needLevel, _needsConfig->_minNeedLevel, _needsConfig->_maxNeedLevel);

  if (delta > 0.0f)
  {
    // See if this need is now in (or still in) the "full" bracket
    const auto& bracketThresholds = _needsConfig->_needsBrackets.find(needId)->second;
    const float fullThreshold = bracketThresholds[static_cast<int>(NeedBracketId::Full)];
    if (needLevel >= fullThreshold)
    {
      startFullnessCooldown = true;
    }

    if (needId == NeedId::Repair)
    {
      // If Repair level is going up, clamp the delta so that it stays within the range of
      // thresholds for broken parts, according to the actual current number of broken parts
      const int numDamagedParts = NumDamagedParts();
      const static float epsilon = 0.00001f;
      const size_t numThresholds = _needsConfig->_brokenPartThresholds.size();

      // FIRST:  Clamp against going too high
      float maxLevel = _needsConfig->_maxNeedLevel;
      if (numDamagedParts >= numThresholds)
      {
        maxLevel = _needsConfig->_brokenPartThresholds[numThresholds - 1];
      }
      else if (numDamagedParts > 0)
      {
        maxLevel = _needsConfig->_brokenPartThresholds[numDamagedParts - 1];
      }
      if (needLevel > maxLevel)
      {
        needLevel = maxLevel - epsilon;
      }

      // SECOND:  Clamp against not going high enough
      float minLevel = _needsConfig->_minNeedLevel;
      if (numDamagedParts < numThresholds)
      {
        minLevel = _needsConfig->_brokenPartThresholds[numDamagedParts];
      }
      if (needLevel < minLevel)
      {
        needLevel = minLevel + epsilon;
      }
    }

    if (needId == NeedId::Energy)
    {
      // If transitioning into 'full' Energy bracket, set Energy to max
      if ((_curNeedsLevels[needId] < fullThreshold) && (needLevel >= fullThreshold))
      {
        needLevel = _needsConfig->_maxNeedLevel;
      }
    }
  }

  _curNeedsLevels[needId] = needLevel;
  _needsBracketsDirty = true;

  if ((needId == NeedId::Repair) && (delta < 0.0f))
  {
    PossiblyDamageParts(cause);
  }

  return startFullnessCooldown;
}


NeedBracketId NeedsState::GetNeedBracketByIndex(size_t needIndex)
{
  UpdateCurNeedsBrackets(_needsConfig->_needsBrackets);

  return _curNeedsBracketsCache[static_cast<NeedId>(needIndex)];
}

bool NeedsState::AreNeedsMet()
{
  UpdateCurNeedsBrackets(_needsConfig->_needsBrackets);

  for( size_t needIndex = 0; needIndex < static_cast<size_t>(NeedId::Count); needIndex++ ) {
    const NeedBracketId bracketId = _curNeedsBracketsCache[static_cast<NeedId>(needIndex)];
    const bool defaultVal = false;
    if( ! IsNeedBracketMet( bracketId, defaultVal  ) ) {
      return false;
    }
  }

  return true;
}


void NeedsState::SetStarLevel(int newLevel)
{
  _curNeedsUnlockLevel = newLevel;
  _numStarsAwarded = 0;
  _numStarsForNextUnlock = _starRewardsConfig->GetMaxStarsForLevel(_curNeedsUnlockLevel);
}

float NeedsState::GetNeedLevel(NeedId need) const
{
  const auto& it = _curNeedsLevels.find(need);
  if( it != _curNeedsLevels.end() ) {
    return it->second;
  }
  else {
    PRINT_NAMED_ERROR("NeedsState.InvalidNeedLevel", "Need level does not exist in curr levels!");
    return 0.0f;
  }
}

NeedBracketId NeedsState::GetNeedBracket(NeedId need)
{
  UpdateCurNeedsBrackets(_needsConfig->_needsBrackets);

  const auto& it = _curNeedsBracketsCache.find(need);
  if( it != _curNeedsBracketsCache.end() ) {
    return it->second;
  }
  else {
    PRINT_NAMED_WARNING("NeedsState.InvalidNeedLevel", "Need level does not exist in brackets cache");
    return NeedBracketId::Count;
  }
}
  

void NeedsState::UpdateCurNeedsBrackets(const NeedsBrackets& needsBrackets)
{
  if (!_needsBracketsDirty)
    return;

  // Set each of the needs' "current bracket" based on the current level for that need
  for (int needIndex = 0; needIndex < (size_t)NeedId::Count; needIndex++)
  {
    const NeedId needId = static_cast<NeedId>(needIndex);
    const float curNeedLevel = _curNeedsLevels[needId];

    const auto& bracketThresholds = needsBrackets.find(needId)->second;
    size_t bracketIndex = 0;
    const auto numBracketThresholds = bracketThresholds.size();
    for ( ; bracketIndex < numBracketThresholds; bracketIndex++)
    {
      if (curNeedLevel >= bracketThresholds[bracketIndex])
      {
        break;
      }
    }
    if (bracketIndex >= numBracketThresholds)
    {
      bracketIndex = numBracketThresholds - 1;
    }
    _curNeedsBracketsCache[needId] = static_cast<NeedBracketId>(bracketIndex);
  }

  _needsBracketsDirty = false;
}

int NeedsState::NumDamagedParts() const
{
  int numDamagedParts = 0;
  for (const auto& part : _partIsDamaged)
  {
    if (part.second)
    {
      numDamagedParts++;
    }
  }
  return numDamagedParts;
}

int NeedsState::NumDamagedPartsForRepairLevel(const float level) const
{
  int newNumDamagedParts = 0;
  for ( ; newNumDamagedParts < _needsConfig->_brokenPartThresholds.size(); newNumDamagedParts++)
  {
    if (level > _needsConfig->_brokenPartThresholds[newNumDamagedParts])
    {
      break;
    }
  }
  return newNumDamagedParts;
}

void NeedsState::PossiblyDamageParts(const NeedsActionId cause)
{
  const int numDamagedParts = NumDamagedParts();
  const int numPartsTotal = static_cast<int>(_partIsDamaged.size());
  if (numDamagedParts >= numPartsTotal)
    return;

  const float curRepairLevel = _curNeedsLevels[NeedId::Repair];
  int newNumDamagedParts = NumDamagedPartsForRepairLevel(curRepairLevel);
  if (newNumDamagedParts > numPartsTotal)
  {
    newNumDamagedParts = numPartsTotal;
  }

  const int partsToDamage = newNumDamagedParts - numDamagedParts;
  if (partsToDamage <= 0)
    return;

  for (int i = 0; i < partsToDamage; i++)
  {
    RepairablePartId part = PickPartToDamage();
    _partIsDamaged[part] = true;

    // DAS Event: "needs.part_damaged"
    // s_val: The name of the part damaged (RepairablePartId)
    // data: New number of damaged parts, followed by a colon, followed
    //       by the cause of damage (NeedsActionId, which can be 'decay')
    std::string data = std::to_string(numDamagedParts + i + 1) + ":" +
                       NeedsActionIdToString(cause);
    Anki::Util::sEvent("needs.part_damaged",
                       {{DDATA, data.c_str()}},
                       RepairablePartIdToString(part));
  }
}

RepairablePartId NeedsState::PickPartToDamage() const
{
  const int numUndamagedParts = static_cast<int>(_partIsDamaged.size()) - NumDamagedParts();
  int undamagedPartIndex = _rng->RandInt(numUndamagedParts);
  int i = 0;
  for (const auto& part : _partIsDamaged)
  {
    if (!part.second)
    {
      if (undamagedPartIndex == 0)
      {
        break;
      }
      undamagedPartIndex--;
    }
    i++;
  }
  return static_cast<RepairablePartId>(i);
}

RepairablePartId NeedsState::PickPartToRepair() const
{
  int damagedPartIndex = _rng->RandInt(NumDamagedParts());
  int i = 0;
  for (const auto& part : _partIsDamaged)
  {
    if (part.second)
    {
      if (damagedPartIndex == 0)
      {
        break;
      }
      damagedPartIndex--;
    }
    i++;
  }
  return static_cast<RepairablePartId>(i);
}

bool NeedsState::IsNeedAtBracket(const NeedId need, const NeedBracketId bracket)
{
  UpdateCurNeedsBrackets(_needsConfig->_needsBrackets);
  const auto& iter = _curNeedsBracketsCache.find(need);
  if(iter != _curNeedsBracketsCache.end())
  {
    return iter->second == bracket;
  }
  PRINT_NAMED_ERROR("NeedsState.IsNeedAtBracket.InvalidNeed",
                    "No needs bracket for need %d",
                    static_cast<int>(need));
  return false;
}

void NeedsState::SetPrevNeedsBrackets()
{
  UpdateCurNeedsBrackets(_needsConfig->_needsBrackets);

  _prevNeedsBracketsCache = _curNeedsBracketsCache;
}

void NeedsState::GetLowestNeedAndBracket(NeedId& lowestNeedId, NeedBracketId& lowestNeedBracketId) const
{
  float lowestNeedValue = std::numeric_limits<float>::max();
  for(const auto& need : _curNeedsLevels)
  {
    if(need.second < lowestNeedValue)
    {
      lowestNeedId = need.first;
      lowestNeedValue = need.second;
    }
  }
  
  const auto& needsBracket = _curNeedsBracketsCache.find(lowestNeedId);
  DEV_ASSERT(needsBracket != _curNeedsBracketsCache.end(),
             "NeedsState.GetLowestNeed.UnknownNeedId");
  lowestNeedBracketId = needsBracket->second;
}

#if ANKI_DEV_CHEATS
void NeedsState::DebugFillNeedMeters()
{
  for (int i = 0; i < static_cast<int>(NeedId::Count); i++)
  {
    _curNeedsLevels[static_cast<NeedId>(i)] = _needsConfig->_maxNeedLevel;
  }

  _needsBracketsDirty = true;
  UpdateCurNeedsBrackets(_needsConfig->_needsBrackets);
  
  for (int i = 0; i < RepairablePartIdNumEntries; i++)
  {
    const auto& repairablePartId = static_cast<RepairablePartId>(i);
    _partIsDamaged[repairablePartId] = false;
  }
}
#endif


} // namespace Cozmo
} // namespace Anki

