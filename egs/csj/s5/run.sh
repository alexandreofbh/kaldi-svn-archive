#!/bin/bash

# Copyright  2015 Tokyo Institute of Technology (Authors: Takafumi Moriya and Takahiro Shinozaki)
#            2015 Mitsubishi Electric Research Laboratories (Author: Shinji Watanabe)
# Apache 2.0
# Acknowledgement  This work was supported by JSPS KAKENHI Grant Number 26280055.

# This recipe is based on the Switchboard corpus recipe, by Arnab Ghoshal,
# in the egs/swbd/s5c/ directory.

# This is a shell script, but it's recommended that you run the commands one by
# one by copying and pasting into the shell.
# Caution: some of the graph creation steps use quite a bit of memory, so you
# should run this on a machine that has sufficient memory.

. cmd.sh
. path.sh
set -e # exit on error

#: << '#SKIP'

if [ ! -d data/csj-data/eval ]; then
 echo "CSJ transcription file does not exist"
 #local/csj_make_trans/csj_automake.sh <RESOUCE_DIR> <MAKING_PLACE(no change)> || exit 1;
 local/csj_make_trans/csj_automake.sh /database/NINJAL/CSJ/ data/csj-data 2>/dev/null
fi
wait

[ ! -d data/csj-data/eval ]\
    && echo "Not finished processing CSJ data" && exit 1;

# Prepare Corpus of Spontaneous Japanese (CSJ) data. 
# Processing CSJ data to KALDI format based on switchboard recipe.
# local/csj_data_prep.sh <SPEECH_and_TRANSCRIPTION_DATA_DIRECTORY>
local/csj_data_prep.sh data/csj-data/

local/csj_prepare_dict.sh 

utils/prepare_lang.sh data/local/dict_nosp "<unk>" data/local/lang_nosp data/lang_nosp

# Now train the language models.
local/csj_train_lms.sh data/local/train/text data/local/dict_nosp/lexicon.txt data/local/lm

# We don't really need all these options for SRILM, since the LM training script
# does some of the same processing (e.g. -subset -tolower)
srilm_opts="-subset -prune-lowprobs -unk -tolower -order 3"
LM=data/local/lm/csj.o3g.kn.gz
utils/format_lm_sri.sh --srilm-opts "$srilm_opts" \
  data/lang_nosp $LM data/local/dict_nosp/lexicon.txt data/lang_nosp_csj_tg

# Data preparation and formatting for evaluation set.
# CSJ has 3 types of evaluation data
#local/csj_eval_data_prep.sh <SPEECH_and_TRANSCRIPTION_DATA_DIRECTORY_ABOUT_EVALUATION_DATA> <EVAL_NUM>
for eval_dev in eval1 eval2 eval3 ; do
    local/csj_eval_data_prep.sh data/csj-data/eval $eval_dev
done

# Now make MFCC features.
# mfccdir should be some place with a largish disk where you
# want to store MFCC features. 
mfccdir=mfcc

for x in train eval1 eval2 eval3; do
  steps/make_mfcc.sh --nj 10 --cmd "$train_cmd" \
    data/$x exp/make_mfcc/$x $mfccdir
  steps/compute_cmvn_stats.sh data/$x exp/make_mfcc/$x $mfccdir
  utils/fix_data_dir.sh data/$x
done

echo "Finish creating MFCCs"

#SKIP

##### Training and Decoding steps start from here #####

# Use the first 4k sentences as dev set.  Note: when we trained the LM, we used
# the 1st 10k sentences as dev set, so the 1st 4k won't have been used in the
# LM training data.   However, they will be in the lexicon, plus speakers
# may overlap, so it's still not quite equivalent to a test set.
utils/subset_data_dir.sh --first data/train 4000 data/train_dev # 6hr 31min
n=$[`cat data/train/segments | wc -l` - 4000]
utils/subset_data_dir.sh --last data/train $n data/train_nodev

# Confirm the amount of utterance segmentations.
# perl -ne 'split; $s+=($_[3]-$_[2]); END{$h=int($s/3600); $r=($s-$h*3600); $m=int($r/60); $r-=$m*60; printf "%.1f sec -- %d:%d:%.1f\n", $s, $h, $m, $r;}' data/train/segments

# Now-- there are 162k utterances (240hr 8min), and we want to start the 
# monophone training on relatively short utterances (easier to align), but want 
# to exclude the shortest ones.
# Therefore, we first take the 100k shortest ones;
# remove most of the repeated utterances, and 
# then take 10k random utterances from those (about 8hr 9mins)
utils/subset_data_dir.sh --shortest data/train_nodev 100000 data/train_100kshort
utils/subset_data_dir.sh data/train_100kshort 30000 data/train_30kshort

# Take the first 100k utterances (about half the data); we'll use
# this for later stages of training.
utils/subset_data_dir.sh --first data/train_nodev 100000 data/train_100k
local/remove_dup_utts.sh 200 data/train_100k data/train_100k_nodup  # 147hr 6min

# Finally, the full training set:
local/remove_dup_utts.sh 300 data/train_nodev data/train_nodup  # 233hr 36min 

## Starting basic training on MFCC features
steps/train_mono.sh --nj 10 --cmd "$train_cmd" \
  data/train_30kshort data/lang_nosp exp/mono 

steps/align_si.sh --nj 10 --cmd "$train_cmd" \
  data/train_100k_nodup data/lang_nosp exp/mono exp/mono_ali 

steps/train_deltas.sh --cmd "$train_cmd" \
  3200 30000 data/train_100k_nodup data/lang_nosp exp/mono_ali exp/tri1 

graph_dir=exp/tri1/graph_csj_tg
$train_cmd $graph_dir/mkgraph.log \
    utils/mkgraph.sh data/lang_nosp_csj_tg exp/tri1 $graph_dir
for eval_num in `seq 3`; do
    steps/decode_si.sh --nj 10 --cmd "$decode_cmd" --config conf/decode.config \
	$graph_dir data/eval${eval_num} exp/tri1/decode_eval${eval_num}_csj
done

steps/align_si.sh --nj 10 --cmd "$train_cmd" \
  data/train_100k_nodup data/lang_nosp exp/tri1 exp/tri1_ali 

steps/train_deltas.sh --cmd "$train_cmd" \
  4000 70000 data/train_100k_nodup data/lang_nosp exp/tri1_ali exp/tri2 

# The previous mkgraph might be writing to this file.  If the previous mkgraph
# is not running, you can remove this loop and this mkgraph will create it.
while [ ! -s data/lang_nosp_csj_tg/tmp/CLG_3_1.fst ]; do sleep 60; done
sleep 20; # in case still writing.
graph_dir=exp/tri2/graph_csj_tg
$train_cmd $graph_dir/mkgraph.log \
    utils/mkgraph.sh data/lang_nosp_csj_tg exp/tri2 $graph_dir
for eval_num in `seq 3`; do
    steps/decode.sh --nj 10 --cmd "$decode_cmd" --config conf/decode.config \
	$graph_dir data/eval${eval_num} exp/tri2/decode_eval${eval_num}_csj
done	
    
# From now, we start with the LDA+MLLT system
steps/align_si.sh --nj 10 --cmd "$train_cmd" \
  data/train_100k_nodup data/lang_nosp exp/tri2 exp/tri2_ali_100k_nodup 

# From now, we start using all of the data (except some duplicates of common                                                                                                         
# utterances, which don't really contribute much).                                                                                                                                   
steps/align_si.sh --nj 10 --cmd "$train_cmd" \
  data/train_nodup data/lang_nosp exp/tri2 exp/tri2_ali_nodup

# Do another iteration of LDA+MLLT training, on all the data. 
steps/train_lda_mllt.sh --cmd "$train_cmd" \
  6000 140000 data/train_nodup data/lang_nosp exp/tri2_ali_nodup exp/tri3

graph_dir=exp/tri3/graph_csj_tg
$train_cmd $graph_dir/mkgraph.log \
    utils/mkgraph.sh data/lang_nosp_csj_tg exp/tri3 $graph_dir
for eval_num in `seq 3`; do
    steps/decode.sh --nj 10 --cmd "$decode_cmd" --config conf/decode.config \
	$graph_dir data/eval${eval_num} exp/tri3/decode_eval${eval_num}_csj
done

# Now we compute the pronunciation and silence probabilities from training data,                                                                                                     
# and re-create the lang directory.                                                                                                                                                  
steps/get_prons.sh --cmd "$train_cmd" data/train_nodup data/lang_nosp exp/tri3
utils/dict_dir_add_pronprobs.sh --max-normalize true \
  data/local/dict_nosp exp/tri3/pron_counts_nowb.txt exp/tri3/sil_counts_nowb.txt \
  exp/tri3/pron_bigram_counts_nowb.txt data/local/dict

utils/prepare_lang.sh data/local/dict "<unk>" data/local/lang data/lang
LM=data/local/lm/csj.o3g.kn.gz
srilm_opts="-subset -prune-lowprobs -unk -tolower -order 3"
utils/format_lm_sri.sh --srilm-opts "$srilm_opts" \
  data/lang $LM data/local/dict/lexicon.txt data/lang_csj_tg

graph_dir=exp/tri3/graph_csj_tg
$train_cmd $graph_dir/mkgraph.log \
    utils/mkgraph.sh data/lang_csj_tg exp/tri3 $graph_dir
for eval_num in `seq 3`; do
    steps/decode.sh --nj 10 --cmd "$decode_cmd" --config conf/decode.config \
        $graph_dir data/eval${eval_num} exp/tri3/decode_eval${eval_num}_csj
done


# Train tri4, which is LDA+MLLT+SAT, on all the (nodup) data.
steps/align_fmllr.sh --nj 10 --cmd "$train_cmd" \
  data/train_nodup data/lang exp/tri3 exp/tri3_ali_nodup 

steps/train_sat.sh  --cmd "$train_cmd" \
  11500 200000 data/train_nodup data/lang exp/tri3_ali_nodup exp/tri4 

graph_dir=exp/tri4/graph_csj_tg
$train_cmd $graph_dir/mkgraph.log \
    utils/mkgraph.sh data/lang_csj_tg exp/tri4 $graph_dir
for eval_num in `seq 3`; do
    steps/decode_fmllr.sh --nj 10 --cmd "$decode_cmd" --config conf/decode.config \
	$graph_dir data/eval${eval_num} exp/tri4/decode_eval${eval_num}_csj
done

steps/decode_fmllr.sh --nj 10 --cmd "$decode_cmd" --config conf/decode.config \
    $graph_dir data/train_dev exp/tri4/decode_train_dev_csj

### MMI training
# MMI training starting from the LDA+MLLT+SAT systems with the entire train_nodup (233hr)
steps/align_fmllr.sh --nj 10 --cmd "$train_cmd" \
  data/train_nodup data/lang exp/tri4 exp/tri4_ali_nodup || exit 1

# You can execute DNN training program [local/nnet/run_dnn.sh] from here.

steps/make_denlats.sh --nj 10 --cmd "$decode_cmd" --config conf/decode.config \
  --transform-dir exp/tri4_ali_nodup \
  data/train_nodup data/lang exp/tri4 exp/tri4_denlats_nodup

# 4 iterations of MMI seems to work well overall. The number of iterations is
# used as an explicit argument even though train_mmi.sh will use 4 iterations by
# default.
num_mmi_iters=4
steps/train_mmi.sh --cmd "$decode_cmd" --boost 0.1 --num-iters $num_mmi_iters \
  data/train_nodup data/lang exp/tri4_{ali,denlats}_nodup exp/tri4_mmi_b0.1 

for eval_num in `seq 3`; do
    for iter in 1 2 3 4; do
	graph_dir=exp/tri4/graph_csj_tg
	decode_dir=exp/tri4_mmi_b0.1/decode_eval${eval_num}_${iter}.mdl_csj

	steps/decode.sh --nj 10 --cmd "$decode_cmd" --config conf/decode.config \
	    --iter $iter --transform-dir exp/tri4/decode_eval${eval_num}_csj \
	    $graph_dir data/eval${eval_num} $decode_dir   
    done
done
wait

# Now do fMMI+MMI training
steps/train_diag_ubm.sh --silence-weight 0.5 --nj 10 --cmd "$train_cmd" \
  700 data/train_nodup data/lang exp/tri4_ali_nodup exp/tri4_dubm

steps/train_mmi_fmmi.sh --learning-rate 0.005 --boost 0.1 --cmd "$train_cmd" \
  data/train_nodup data/lang exp/tri4_ali_nodup exp/tri4_dubm \
  exp/tri4_denlats_nodup exp/tri4_fmmi_b0.1  

for eval_num in `seq 3`; do
    for iter in 4 5 6 7 8; do
	graph_dir=exp/tri4/graph_csj_tg
	decode_dir=exp/tri4_fmmi_b0.1/decode_eval${eval_num}_it${iter}_csj
	
	steps/decode_fmmi.sh --nj 10 --cmd "$decode_cmd" --iter $iter \
	    --transform-dir exp/tri4/decode_eval${eval_num}_csj \
	    --config conf/decode.config $graph_dir data/eval${eval_num} $decode_dir
    done
done
wait

# this will help find issues with the lexicon.
# steps/cleanup/debug_lexicon.sh --nj 300 --cmd "$train_cmd" data/train_nodev data/lang exp/tri4 data/local/dict/lexicon.txt exp/debug_lexicon 

# SGMM system
# local/run_sgmm2.sh

#SKIP

##### Start DNN training #####

# # In case of using CSJ on DNN, I recommend you to use local/nnet/run_dnn.sh 
# # Karel's DNN recipe on top of fMLLR features
local/nnet/run_dnn.sh ## Recommend script

# # getting results (see RESULTS file)
# for eval_num in `seq 3`; do
#     echo "=== evaluation set $eval_num ===" ;
#     for x in exp/{tri,dnn}*/decode_eval${eval_num}*; do [ -d $x ] && grep WER $x/wer_* | utils/best_wer.sh; done ;
# done