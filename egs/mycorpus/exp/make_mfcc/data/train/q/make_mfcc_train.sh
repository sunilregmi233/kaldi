#!/bin/bash
cd /home/sunil/kaldi/egs/mycorpus
. ./path.sh
( echo '#' Running on `hostname`
  echo '#' Started at `date`
  echo -n '# '; cat <<EOF
extract-segments scp,p:data/train/wav.scp exp/make_mfcc/data/train/segments.${SGE_TASK_ID} ark:- | compute-mfcc-feats --write-utt2dur=ark,t:exp/make_mfcc/data/train/utt2dur.${SGE_TASK_ID} --verbose=2 --config=conf/mfcc.conf ark:- ark:- | copy-feats --compress=true --write-num-frames=ark,t:exp/make_mfcc/data/train/utt2num_frames.${SGE_TASK_ID} ark:- ark,scp:/home/sunil/kaldi/egs/mycorpus/mfcc/raw_mfcc_train.${SGE_TASK_ID}.ark,/home/sunil/kaldi/egs/mycorpus/mfcc/raw_mfcc_train.${SGE_TASK_ID}.scp 
EOF
) >exp/make_mfcc/data/train/make_mfcc_train.$SGE_TASK_ID.log
time1=`date +"%s"`
 ( extract-segments scp,p:data/train/wav.scp exp/make_mfcc/data/train/segments.${SGE_TASK_ID} ark:- | compute-mfcc-feats --write-utt2dur=ark,t:exp/make_mfcc/data/train/utt2dur.${SGE_TASK_ID} --verbose=2 --config=conf/mfcc.conf ark:- ark:- | copy-feats --compress=true --write-num-frames=ark,t:exp/make_mfcc/data/train/utt2num_frames.${SGE_TASK_ID} ark:- ark,scp:/home/sunil/kaldi/egs/mycorpus/mfcc/raw_mfcc_train.${SGE_TASK_ID}.ark,/home/sunil/kaldi/egs/mycorpus/mfcc/raw_mfcc_train.${SGE_TASK_ID}.scp  ) 2>>exp/make_mfcc/data/train/make_mfcc_train.$SGE_TASK_ID.log >>exp/make_mfcc/data/train/make_mfcc_train.$SGE_TASK_ID.log
ret=$?
time2=`date +"%s"`
echo '#' Accounting: time=$(($time2-$time1)) threads=1 >>exp/make_mfcc/data/train/make_mfcc_train.$SGE_TASK_ID.log
echo '#' Finished at `date` with status $ret >>exp/make_mfcc/data/train/make_mfcc_train.$SGE_TASK_ID.log
[ $ret -eq 137 ] && exit 100;
touch exp/make_mfcc/data/train/q/sync/done.106407.$SGE_TASK_ID
exit $[$ret ? 1 : 0]
## submitted with:
# qsub -v PATH -cwd -S /bin/bash -j y -l arch=*64* -o exp/make_mfcc/data/train/q/make_mfcc_train.log   -t 1:1 /home/sunil/kaldi/egs/mycorpus/exp/make_mfcc/data/train/q/make_mfcc_train.sh >>exp/make_mfcc/data/train/q/make_mfcc_train.log 2>&1
