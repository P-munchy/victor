/**
 * File: pyramidBaseConfigurationContainer.h
 *
 * Author: Kevin M. Karol
 * Created: 11/2/2016
 *
 * Description: Caches pyramid bases for BlockConfigurationManager
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef Anki_Cozmo_PyramidBaseConfigurationContainer_H
#define Anki_Cozmo_PyramidBaseConfigurationContainer_H

#include "engine/blockWorld/blockConfigurationContainer.h"
#include "engine/blockWorld/pyramidConfigurationContainer.h"


namespace Anki{
namespace Cozmo{
  
namespace BlockConfigurations{
// forward declaration
class PyramidBase;

using PyramidBasePtr = std::shared_ptr<const PyramidBase>;
using PyramidBaseWeakPtr = std::weak_ptr<const PyramidBase>;

  
class PyramidBaseConfigurationContainer : public BlockConfigurationContainer
{
public:
  PyramidBaseConfigurationContainer();
  
  // accessors
  const std::vector<PyramidBasePtr>& GetBases() const { return _pyramidBaseCache;}
  std::vector<PyramidBaseWeakPtr> GetWeakBases() const;
  
  // remove any bases that are in the full pyramid list
  void PruneFullPyramids(const std::vector<PyramidPtr>& fullPyramids);
  
  virtual bool AnyConfigContainsObject(const ObjectID& objectID) const override;
  virtual int ConfigurationCount() const override { return static_cast<int>(_pyramidBaseCache.size());}

protected:
  virtual void SetCurrentCacheAsBackup()override { _backupCache = _pyramidBaseCache;}
  virtual void DeleteBackup() override { _backupCache.clear();}
  
  virtual ConfigPtrVec AddAllConfigsWithObjectToCache(const Robot& robot, const ObservableObject* object) override;
  virtual void ClearCache() override;
  
private:
  std::vector<PyramidBasePtr> _pyramidBaseCache;
  std::vector<PyramidBasePtr> _backupCache;

}; // class PyramidBaseConfigurationContainer

} // namespace BlockConfigurations
} // namespace Cozmo
} // namespace Anki


#endif // Anki_Cozmo_PyramidBaseConfigurationContainer_H
