/**
 * File: linearAlgebra.h
 *
 * Author: Andrew Stein (andrew)
 * Created: 5/2/2014
 *
 * Information on last revision to this file:
 *    $LastChangedDate$
 *    $LastChangedBy$
 *    $LastChangedRevision$
 *
 * Description: 
 *    Defines various linear algebra methods. Templated implementations are
 *    in a separate linearAlgebra_impl.h file.
 *
 * Copyright: Anki, Inc. 2014
 *
 **/

#ifndef ANKI_COMMON_BASESTATION_MATH_LINEAR_ALGEBRA_H
#define ANKI_COMMON_BASESTATION_MATH_LINEAR_ALGEBRA_H

#include "anki/common/basestation/math/matrix.h"
#include "anki/common/basestation/math/point.h"

namespace Anki {
  
  // Returns a projection operator for the given plane normal, n,
  //
  //   P = I - n*n^T
  //
  // such that
  //
  //   v' = P*v
  //
  // removes all variation of v along the normal. Note that the normal
  // must be a unit vector!
  //
  // TODO: make normal a UnitVector type
  template<size_t N, typename T>
  SmallSquareMatrix<N,T> GetProjectionOperator(const Point<N,T>& normal);
  
} // namespace Anki

#endif // ANKI_COMMON_BASESTATION_MATH_LINEAR_ALGEBRA_H