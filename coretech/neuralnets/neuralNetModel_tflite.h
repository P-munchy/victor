/**
 * File: neuralNetModel_tflite.h
 *
 * Author: Andrew Stein
 * Date:   12/5/2017
 *
 * Description: Implementation of NeuralNetModel class which wraps TensorFlow Lite.
 *
 * Copyright: Anki, Inc. 2017
 **/

// NOTE: this wrapper completely compiles out if we're using a different model (e.g. TensorFlow)
#ifdef ANKI_NEURALNETS_USE_TFLITE

#ifndef __Anki_NeuralNets_NeuralNetModel_TFLite_H__
#define __Anki_NeuralNets_NeuralNetModel_TFLite_H__

#include "coretech/neuralnets/neuralNetModel_interface.h"

#include <list>

// Forward declaration
namespace tflite
{
  class FlatBufferModel;
  class Interpreter;
}

namespace Anki {
namespace NeuralNets {

class NeuralNetModel : public INeuralNetModel
{
public:
  
  explicit NeuralNetModel(const std::string& cachePath);
  ~NeuralNetModel();

  // ObjectDetector expects LoadModel and Run to exist
  Result LoadModel(const std::string& modelPath, const Json::Value& config);
  Result Detect(cv::Mat& img, const TimeStamp_t t, std::list<Vision::SalientPoint>& salientPoints);
  
private:
  
  void ScaleImage(cv::Mat& img);
  
  std::unique_ptr<tflite::FlatBufferModel> _model;
  std::unique_ptr<tflite::Interpreter>     _interpreter;

}; // class NeuralNetModel

} // namespace Vision
} // namespace Anki

#endif /* __Anki_Vision_NeuralNetModel_TFLite_H__ */

#endif // #if USE_TENSORFLOW_LITE
