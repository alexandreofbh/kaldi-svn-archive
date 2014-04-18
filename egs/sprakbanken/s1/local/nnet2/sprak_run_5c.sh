#!/bin/bash

# This is neural net training on top of adapted 40-dimensional features.
# 

. ./cmd.sh

uid=$1
test1=$2
test2=$3

#( 
 steps/nnet2/train_tanh.sh \
   --mix-up 8000 \
   --initial-learning-rate 0.01 --final-learning-rate 0.001 \
   --num-hidden-layers 4 --hidden-layer-dim 1024 \
   --cmd "$decode_cmd" \
#   --stage 419 \
   data/train data/lang exp/tri4b_ali exp/nnet5c || exit 1
  
  steps/decode_nnet_cpu.sh --cmd "$decode_cmd" --nj 7 \
    --transform-dir exp/tri4b/decode_3g_$test1 \
     exp/tri4b/graph_3g data/$test1 exp/nnet5c/decode_3g_$test1

if [ -d $test2 ]; then
  steps/decode_nnet_cpu.sh --cmd "$decode_cmd" --nj 4 \
    --transform-dir exp/tri4b/decode_4g_$test2 \
     exp/tri4b/graph_4g data/$test2 exp/nnet5c/decode_4g_$test2
fi
#)

