// decoder/decodable-am-sgmm2.h

// Copyright 2009-2012  Saarland University  Microsoft Corporation
//                      Lukas Burget  Johns Hopkins University (author: Daniel Povey)

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

#ifndef KALDI_DECODER_DECODABLE_AM_SGMM2_H_
#define KALDI_DECODER_DECODABLE_AM_SGMM2_H_

#include <vector>

#include "base/kaldi-common.h"
#include "sgmm2/am-sgmm.h"
#include "hmm/transition-model.h"
#include "itf/decodable-itf.h"

namespace kaldi {

class DecodableAmSgmm2 : public DecodableInterface {
 public:
  DecodableAmSgmm2(const AmSgmm2 &sgmm,
                   const TransitionModel &tm,
                   const Matrix<BaseFloat> &feats,
                   const std::vector<std::vector<int32> > &gselect,
                   BaseFloat log_prune,
                   Sgmm2PerSpkDerivedVars *spk):
      sgmm_(sgmm), spk_(spk),
      trans_model_(tm), feature_matrix_(feats),
      gselect_(gselect), log_prune_(log_prune), cur_frame_(-1),
      sgmm_cache_(sgmm.NumGroups(), sgmm.NumPdfs()) {
    KALDI_ASSERT(gselect.size() == static_cast<size_t>(feats.NumRows()));
  }

  // Note, frames are numbered from zero, but transition indices are 1-based!
  // This is for compatibility with OpenFST.
  virtual BaseFloat LogLikelihood(int32 frame, int32 tid) {
    return LogLikelihoodForPdf(frame, trans_model_.TransitionIdToPdf(tid));
  }
  int32 NumFrames() { return feature_matrix_.NumRows(); }
  virtual int32 NumIndices() { return trans_model_.NumTransitionIds(); }
  
  virtual bool IsLastFrame(int32 frame) {
    KALDI_ASSERT(frame < NumFrames());
    return (frame == NumFrames() - 1);
  }

 protected:
  virtual BaseFloat LogLikelihoodForPdf(int32 frame, int32 pdf_id);

  const AmSgmm2 &sgmm_;
  Sgmm2PerSpkDerivedVars *spk_;
  const TransitionModel &trans_model_;  ///< for tid to pdf mapping
  const Matrix<BaseFloat> &feature_matrix_;
  const std::vector<std::vector<int32> > gselect_; 
  
  BaseFloat log_prune_;
  
  int32 cur_frame_;
  Sgmm2PerFrameDerivedVars per_frame_vars_;
  Sgmm2LikelihoodCache sgmm_cache_;

 private:
  KALDI_DISALLOW_COPY_AND_ASSIGN(DecodableAmSgmm2);
};

class DecodableAmSgmm2Scaled : public DecodableAmSgmm2 {
 public:
  DecodableAmSgmm2Scaled(const AmSgmm2 &sgmm,
                         const TransitionModel &tm,
                         const Matrix<BaseFloat> &feats,
                         const std::vector<std::vector<int32> > &gselect,
                         BaseFloat log_prune,
                         BaseFloat scale,
                         Sgmm2PerSpkDerivedVars *spk)
      : DecodableAmSgmm2(sgmm, tm, feats, gselect, log_prune, spk),
        scale_(scale) {}

  // Note, frames are numbered from zero but transition-ids from one.
  virtual BaseFloat LogLikelihood(int32 frame, int32 tid) {
    return LogLikelihoodForPdf(frame, trans_model_.TransitionIdToPdf(tid))
            * scale_;
  }
 private:
  BaseFloat scale_;
  KALDI_DISALLOW_COPY_AND_ASSIGN(DecodableAmSgmm2Scaled);
};


}  // namespace kaldi

#endif  // KALDI_DECODER_DECODABLE_AM_SGMM_H_
