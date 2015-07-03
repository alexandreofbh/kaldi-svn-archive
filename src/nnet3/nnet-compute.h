// nnet3/nnet-compute.h

// Copyright   2012-2015  Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#ifndef KALDI_NNET3_NNET_COMPUTE_H_
#define KALDI_NNET3_NNET_COMPUTE_H_

#include "nnet3/nnet-common.h"
#include "nnet3/nnet-nnet.h"
#include "nnet3/nnet-computation.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <map>


namespace kaldi {
namespace nnet3 {



/**
  class NnetComputer is responsible for executing the computation described in the
  "computation" object.

  You call in sequence, the constructor, then AcceptInput(), then Forward(),
  then GetOutput(), then if applicable (Backward(), then if applicable
  GetInputDeriv()).
 */
class NnetComputer {
  /// Constructor.  nnet_to_update will be NULL if you are not doing
  /// model update or model-derivative computation.
  NnetComputer(const NnetComputation &computation,
               const Nnet &nnet,
               Nnet *nnet_to_update);

  /// e.g. AcceptInput ("input", input_mat).  Will crash if there is no
  /// input node with the given name.  This function is destructive of "input"
  /// as it takes it using the Swap function of CuMatrix.
  /// Must have the same number of rows as the corresponding input described
  /// in the ComputationRequest e.g. the indexes.size() in the corresponding
  /// IoSpecification.
  void AcceptInput(const std::string &input_name,
                   CuMatrix<BaseFloat> *input);

  // Does the forward computation.
  void Forward();
  
  // e.g. GetOutput ("output").  Will crash if no such output.
  const CuMatrixBase<BaseFloat> &GetOutput(const std::string &output_name);

  // Does the backward computation.
  void Backward();

  // e.g. GetInputDeriv ("input").  Will crash if no such input derivative.
  // You may only call this if you requested this input derivative in the
  // ComputationRequest.
  const CuMatrixBase<BaseFloat> &GetInputDeriv(const std::string &input_name);
  
 private:
  const NnetComputation &computation_;
  const Nnet &nnet_;
  Nnet *nnet_to_update_;
  bool forward_done_;

  // The matrices used in the computation.
  std::vector<CuMatrix<BaseFloat> > matrices_;

  // executes the command in computation_.commands[command].
  void ExecuteCommand(int32 command);

  CuSubMatrix<BaseFloat> GetSubMatrix(int32 submatrix_index);

  void GetPointers(int32 indexes_multi_index,
                   int32 num_cols,
                   CuArray<BaseFloat*> *pointers);
  void GetPointers(int32 indexes_multi_index,
                   int32 num_cols,                   
                   CuArray<const BaseFloat*> *pointers);

  
};



} // namespace nnet3
} // namespace kaldi

#endif
