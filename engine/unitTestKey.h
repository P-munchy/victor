/**
 * File: unitTestKey.h
 *
 * Author: ross
 * Created: feb 23 2018
 *
 * Description: A class constructable only by specific unit tests. A shipping class can
 *              expose a member function with UnitTestKey as a parameter, and since only unit
 *              tests can construct it, only unit tests can call it
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef __Cozmo_Engine_UnitTestKey_H__
#define __Cozmo_Engine_UnitTestKey_H__

namespace Anki {
namespace Cozmo {
  
class UnitTestKey
{
private:
  UnitTestKey(){}
  
  friend class TestBehaviorHighLevelAI;
};

} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Engine_UnitTestKey_H__
