// bin/post-to-phone-post.cc

// Copyright 2012  Johns Hopkins University (author: Daniel Povey)

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


#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "hmm/transition-model.h"

int main(int argc, char *argv[]) {
  try {
    using namespace kaldi;
    typedef kaldi::int32 int32;  

    const char *usage =
        "Convert posteriors to phone-level posteriors\n"
        "\n"
        "Usage: post-to-phone-post [options] <model> <post-rspecifier> <phone-post-wspecifier>\n"
        " e.g.: post-to-phone-post --binary=false 1.mdl \"ark:ali-to-post 1.ali|\" ark,t:-\n";
    
    ParseOptions po(usage);

    po.Read(argc, argv);

    if (po.NumArgs() != 3) {
      po.PrintUsage();
      exit(1);
    }
      
    std::string model_rxfilename = po.GetArg(1),
        post_rspecifier = po.GetArg(2),
        phone_post_wspecifier = po.GetArg(3);

    kaldi::SequentialPosteriorReader posterior_reader(post_rspecifier);
    kaldi::PosteriorWriter posterior_writer(phone_post_wspecifier);

    TransitionModel trans_model;    
    {
      bool binary_in;
      Input ki(model_rxfilename, &binary_in);
      trans_model.Read(ki.Stream(), binary_in);
    }
    int32 num_done = 0;      
    
    for (; !posterior_reader.Done(); posterior_reader.Next()) {
      const kaldi::Posterior &posterior = posterior_reader.Value();
      int32 num_frames = static_cast<int32>(posterior.size());
      kaldi::Posterior phone_posterior(num_frames);
      for (int32 i = 0; i < num_frames; i++) {
        std::map<int32, BaseFloat> phone_to_post;
        
        for (int32 j = 0; j < static_cast<int32>(posterior[i].size()); j++) {
          int32 tid = posterior[i][j].first,
              phone = trans_model.TransitionIdToPhone(tid);
          BaseFloat post = posterior[i][j].second;
          if (phone_to_post.count(phone) == 0)
            phone_to_post[phone] = post;
          else
            phone_to_post[phone] += post;
        }
        phone_posterior[i].reserve(phone_to_post.size());
        for (std::map<int32, BaseFloat>::const_iterator iter =
                 phone_to_post.begin(); iter != phone_to_post.end(); ++iter) {
          phone_posterior[i].push_back(
              std::make_pair(iter->first, iter->second));
        }
      }
      posterior_writer.Write(posterior_reader.Key(), phone_posterior);
      num_done++;
    }
    KALDI_LOG << "Done converting posteriors to phone posteriors for "
              << num_done << " utterances.";
    return (num_done != 0 ? 0 : 1);
  } catch(const std::exception &e) {
    std::cerr << e.what();
    return -1;
  }
}

