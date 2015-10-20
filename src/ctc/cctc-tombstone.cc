// ctc/cctc-training.cc

// Copyright      2015   Johns Hopkins University (author: Daniel Povey)

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


#include "ctc/cctc-tombstone.h"
#include "ctc/cctc-kernels.h"

namespace kaldi {
namespace ctc {

template <typename Real>
void Tensor3dCopy(int32 xdim, int32 ydim, int32 zdim,
                  int32 src_xstride, int32 src_ystride, int32 src_zstride,
                  int32 dst_xstride, int32 dst_ystride, int32 dst_zstride,
                  const Real *src, Real *dst) {
#if HAVE_CUDA == 1
  if (CuDevice::Instantiate().Enabled()) {
    Timer tim;
    dim3 dimBlock(std::min<int32>(CU1DBLOCK, xdim), 1, 1);
    dim3 dimGrid(n_blocks(xdim, dimBlock.x), ydim, zdim);
    // the kernel only needs the xdim because it's only the number of
    // threads (i.e. blockDim) that gets padded.
    cuda_rearrange_3d_tensor(dimGrid, dimBlock, xdim,
                             src_xstride, src_ystride, src_zstride,
                             dst_xstride, dst_ystride, dst_zstride,
                             src, dst);
    CU_SAFE_CALL(cudaGetLastError());
    CuDevice::Instantiate().AccuProfile(__func__, tim.Elapsed());
  } else
#endif
  {
    for (int32 x = 0; x < xdim; x++)
      for (int32 y = 0; y < ydim; y++)
        for (int32 z = 0; z < zdim; z++)
          dst[x * dst_xstride + y * dst_ystride + z * dst_zstride] =
              src[x * src_xstride + y * src_ystride + z * src_zstride];
  }
}

// instantiate the template for float and double.
template
void Tensor3dCopy(int32 xdim, int32 ydim, int32 zdim,
                  int32 src_xstride, int32 src_ystride, int32 src_zstride,
                  int32 dst_xstride, int32 dst_ystride, int32 dst_zstride,
                  const float *src, float *dst);
template
void Tensor3dCopy(int32 xdim, int32 ydim, int32 zdim,
                  int32 src_xstride, int32 src_ystride, int32 src_zstride,
                  int32 dst_xstride, int32 dst_ystride, int32 dst_zstride,
                  const double *src, double *dst);


void RearrangeNnetOutput(
    const CuMatrixBase<BaseFloat> &nnet_output,
    CuMatrixBase<BaseFloat> *nnet_output_rearranged) {
  int32 num_time_steps = nnet_output_rearranged->NumRows(),
      nnet_output_dim = nnet_output.NumCols();
  KALDI_ASSERT(nnet_output.NumRows() % num_time_steps == 0);
  int32 num_sequences = nnet_output.NumRows() / num_time_steps;
  KALDI_ASSERT(nnet_output_rearranged->NumCols() ==
               nnet_output_dim * num_sequences);
  int32 xdim = num_time_steps,
      ydim = nnet_output_dim,
      zdim = num_sequences,
      src_xstride = nnet_output.Stride(),
      src_ystride = 1,
      src_zstride = nnet_output.Stride() * num_time_steps,
      dest_xstride = nnet_output_rearranged->Stride(),
      dest_ystride = num_sequences,
      dest_zstride = 1;
  Tensor3dCopy(xdim, ydim, zdim,
               src_xstride, src_ystride, src_zstride,
               dest_xstride, dest_ystride, dest_zstride,
               nnet_output.Data(), nnet_output_rearranged->Data());
}

void RearrangeNnetOutputReverse(
    const CuMatrixBase<BaseFloat> &nnet_output_rearranged,
    CuMatrixBase<BaseFloat> *nnet_output) {
  int32 num_time_steps = nnet_output_rearranged.NumRows(),
      nnet_output_dim = nnet_output->NumCols();
  KALDI_ASSERT(nnet_output->NumRows() % num_time_steps == 0);
  int32 num_sequences = nnet_output->NumRows() / num_time_steps;
  KALDI_ASSERT(nnet_output_rearranged.NumCols() ==
               nnet_output_dim * num_sequences);
  int32 xdim = num_time_steps,
      ydim = nnet_output_dim,
      zdim = num_sequences,
      src_xstride = nnet_output_rearranged.Stride(),
      src_ystride = num_sequences,
      src_zstride = 1,
      dest_xstride = nnet_output->Stride(),
      dest_ystride = 1,
      dest_zstride = nnet_output->Stride() * num_time_steps;
  Tensor3dCopy(xdim, ydim, zdim,
               src_xstride, src_ystride, src_zstride,
               dest_xstride, dest_ystride, dest_zstride,
               nnet_output_rearranged.Data(), nnet_output->Data());
}


CctcHmm::CctcHmm(const CctcTransitionModel &trans_mdl) {
  SetTransitions(trans_mdl);
  SetInitialProbs(trans_mdl);
}

const Int32Pair* CctcHmm::BackwardTransitions() const {
  return backward_transitions_.Data();
}

const Int32Pair* CctcHmm::ForwardTransitions() const {
  return forward_transitions_.Data();
}

const CctcHmmTransition* CctcHmm::Transitions() const {
  return transitions_.Data();
}

const CuVector<BaseFloat>& CctcHmm::InitialProbs() const {
  return initial_probs_;
}

void CctcHmm::SetTransitions(const CctcTransitionModel &trans_mdl) {
  int32 num_hmm_states = trans_mdl.NumHistoryStates(),
      num_phones = trans_mdl.NumPhones();
  std::vector<std::vector<CctcHmmTransition> > transitions_out(num_hmm_states),
      transitions_in(num_hmm_states);
  for (int32 s = 0; s < num_hmm_states; s++) {
    transitions_out[s].resize(num_phones + 1);
    transitions_in[s].reserve(num_phones + 1); // a reasonable guess.
  }
  for (int32 s = 0; s < num_hmm_states; s++) {
    std::vector<CctcHmmTransition> &this_trans_out = transitions_out[s];
    for (int32 p = 0; p <= num_phones; p++) {
      CctcHmmTransition &this_trans = this_trans_out[p];
      int32 graph_label = trans_mdl.GetGraphLabel(s, p);
      this_trans.transition_prob = trans_mdl.GraphLabelToLmProb(graph_label);
      this_trans.num_index = trans_mdl.GraphLabelToOutputIndex(graph_label);
      this_trans.hmm_state =
          trans_mdl.GraphLabelToNextHistoryState(graph_label);
      CctcHmmTransition reverse_transition(this_trans);
      reverse_transition.hmm_state = s;
      transitions_in[this_trans.hmm_state].push_back(reverse_transition);
    }
  }
  std::vector<Int32Pair> forward_transitions(num_hmm_states);
  std::vector<Int32Pair> backward_transitions(num_hmm_states);
  std::vector<CctcHmmTransition> transitions;
  size_t expected_num_transitions = 2 * num_hmm_states * (num_phones + 1);
  transitions.reserve(expected_num_transitions);
  for (int32 s = 0; s < num_hmm_states; s++) {
    forward_transitions[s].first = static_cast<int32>(transitions.size());
    transitions.insert(transitions.end(), transitions_out[s].begin(),
                       transitions_out[s].end());
    forward_transitions[s].second = static_cast<int32>(transitions.size());
  }
  for (int32 s = 0; s < num_hmm_states; s++) {
    backward_transitions[s].first = static_cast<int32>(transitions.size());
    transitions.insert(transitions.end(), transitions_in[s].begin(),
                       transitions_in[s].end());
    backward_transitions[s].second = static_cast<int32>(transitions.size());
  }
  KALDI_ASSERT(transitions.size() == expected_num_transitions);

  forward_transitions_ = forward_transitions;
  backward_transitions_ = backward_transitions;
  transitions_ = transitions;
}

void CctcHmm::SetInitialProbs(const CctcTransitionModel &trans_mdl) {
  // we very arbitrarily choose to set to uniform probs and then do 20
  // iterations of HMM propagation before taking the probabilities.  These
  // initial probs won't end up making much difference as we won't be using
  // derivatives from the first few frames, so this isn't 100% critical.
  int32 num_iters = 20;
  int32 num_hmm_states = trans_mdl.NumHistoryStates(),
      num_phones = trans_mdl.NumPhones();

  Vector<double> cur_prob(num_hmm_states), next_prob(num_hmm_states);
  cur_prob.Set(1.0 / num_hmm_states);
  for (int32 iter = 0; iter < num_iters; iter++) {
    for (int32 s = 0; s < num_hmm_states; s++) {
      double prob = cur_prob(s);
      for (int32 p = 0; p <= num_phones; p++) {
        int32 graph_label = trans_mdl.GetGraphLabel(s, p);
        BaseFloat trans_prob = trans_mdl.GraphLabelToLmProb(graph_label);
        int32 next_state = trans_mdl.GraphLabelToNextHistoryState(graph_label);
        next_prob(next_state) += prob * trans_prob;
      }
    }
    cur_prob.Swap(&next_prob);
    next_prob.SetZero();
    // Renormalize, beause the HMM won't sum to one [thanks to the
    //e self-loops, which have probability one...]
    cur_prob.Scale(1.0 / cur_prob.Sum());
  }
  Vector<BaseFloat> cur_prob_float(cur_prob);
  MatrixIndexT max_index;
  cur_prob_float.Max(&max_index);
  special_hmm_state_ = max_index;
  initial_probs_ = cur_prob_float;
}

CctcNegativeComputation::CctcNegativeComputation(
    const CctcTransitionModel &trans_model,
    const CctcHmm &hmm,
    const CuMatrixBase<BaseFloat> &exp_nnet_output,
    const CuMatrixBase<BaseFloat> &denominators,
    int32 num_sequences):
    trans_model_(trans_model), hmm_(hmm),
    exp_nnet_output_(exp_nnet_output), denominators_(denominators),
    num_sequences_(num_sequences) {
  KALDI_ASSERT(exp_nnet_output_.NumRows() % num_sequences_ == 0);
  num_time_steps_ = exp_nnet_output_.NumRows() / num_sequences_;
  numerator_dim_ = trans_model_.NumTreeIndexes() +
      trans_model_.NumBlankIndexes();
  num_hmm_states_ = trans_model_.NumHistoryStates();
  numerators_rearranged_.Resize(num_time_steps_,
                                numerator_dim_ * num_sequences_,
                                kUndefined);
  RearrangeNnetOutput(exp_nnet_output_.ColRange(0, numerator_dim_),
                      &numerators_rearranged_);
  denominators_rearranged_.Resize(num_time_steps_,
                                  num_hmm_states_ * num_sequences_,
                                  kUndefined);
  RearrangeNnetOutput(denominators_, &denominators_rearranged_);

  alpha_.Resize(num_time_steps_ + 1, num_hmm_states_ * num_sequences_,
                kUndefined);
  beta_.Resize(2, num_hmm_states_ * num_sequences_, kUndefined);

}

void CctcNegativeComputation::AlphaFirstFrame() {
  // dim == num_hmm_states_ * num_sequences_.
  BaseFloat *first_frame_alpha = alpha_.RowData(0);
  // create a 'fake matrix' - view this row as a matrix.
  CuSubMatrix<BaseFloat> alpha_mat(first_frame_alpha,
                                   num_hmm_states_,
                                   num_sequences_,
                                   num_sequences_);
  // TODO: It would be more efficient here if we implemented a CopyColsFromVec
  // function in class CuMatrix.
  alpha_mat.SetZero();
  alpha_mat.AddVecToCols(1.0, hmm_.InitialProbs(), 0.0);
}

// the alpha computation for some 0 < t <= num_time_steps_.
void CctcNegativeComputation::AlphaGeneralFrame(int32 t) {
  KALDI_ASSERT(t > 0 && t <= num_time_steps_);
  BaseFloat *this_alpha = alpha_.RowData(t);
  const BaseFloat *prev_alpha = alpha_.RowData(t - 1);
  const Int32Pair *backward_transitions = hmm_.BackwardTransitions();
  const CctcHmmTransition *transitions = hmm_.Transitions();
  const BaseFloat *num_probs = numerators_rearranged_.RowData(t - 1),
      *den_probs = denominators_rearranged_.RowData(t - 1);
  int32 num_hmm_states = num_hmm_states_,
      num_sequences = num_sequences_,
      special_hmm_state = hmm_.SpecialHmmState();

#if HAVE_CUDA == 1
  if (CuDevice::Instantiate().Enabled()) {
    Timer tim;
    dim3 dimBlock(std::min<int32>(CU1DBLOCK, num_sequences), 1, 1);
    dim3 dimGrid(n_blocks(num_sequences, dimBlock.x), num_hmm_states, 1);

    cuda_ctc_hmm_forward(dimGrid, dimBlock, backward_transitions, transitions, t,
                         num_sequences, special_hmm_state, num_probs,
                         den_probs, prev_alpha, this_alpha);

    CU_SAFE_CALL(cudaGetLastError());
    CuDevice::Instantiate().AccuProfile(__func__, tim.Elapsed());
  } else
#endif
  {
    for (int32 h = 0; h < num_hmm_states; h++) {
      for (int32 s = 0; s < num_sequences; s++) {
        double this_tot_alpha = 0.0;
        const CctcHmmTransition
            *trans_iter = transitions + backward_transitions[h].first,
            *trans_end = transitions + backward_transitions[h].second;
        for (; trans_iter != trans_end; ++trans_iter) {
          BaseFloat transition_prob = trans_iter->transition_prob;
          int32 num_index = trans_iter->num_index,
              prev_hmm_state = trans_iter->hmm_state;
          BaseFloat
              den = den_probs[prev_hmm_state * num_sequences + s],
              num = num_probs[num_index * num_sequences + s],
              this_prev_alpha = prev_alpha[prev_hmm_state * num_sequences + s];
          this_tot_alpha += this_prev_alpha * transition_prob * num / den;
        }
        // Let arbitrary_scale be the inverse of the alpha value for the
        // hmm-state indexed special_hmm_state_ on the previous frame (for this
        // sequence); we multiply this into all the transition-probabilities
        // from the previous frame to this frame, in both the forward and
        // backward passes, in order to keep the alphas in a good numeric range.
        // This won't affect the posteriors, but when computing the total
        // likelihood we'll need to compensate for it later on.
        BaseFloat arbitrary_scale =
            1.0 / prev_alpha[special_hmm_state * num_sequences + s];
        KALDI_ASSERT(this_tot_alpha - this_tot_alpha == 0);
        this_alpha[h * num_sequences + s] = this_tot_alpha * arbitrary_scale;
      }
    }
  }
}

BaseFloat CctcNegativeComputation::Forward() {
  AlphaFirstFrame();
  for (int32 t = 1; t <= num_time_steps_; t++)
    AlphaGeneralFrame(t);
  return ComputeTotLogLike();
}

BaseFloat CctcNegativeComputation::ComputeTotLogLike() {
  tot_prob_.Resize(num_sequences_);
  // View the last alpha as a matrix of size num-time-steps by num-sequences.
  CuSubMatrix<BaseFloat> last_alpha(alpha_.RowData(num_time_steps_),
                                    num_hmm_states_,
                                    num_sequences_,
                                    num_sequences_);

  tot_prob_.AddRowSumMat(1.0, last_alpha, 0.0);
  // we should probably add an ApplyLog() function that takes a vector argument.
  tot_log_prob_ = tot_prob_;
  tot_log_prob_.ApplyLog();
  BaseFloat tot_log_prob = tot_log_prob_.Sum();

  // We now have to add something for the arbitrary scaling factor.  the
  // inverses of all the alphas for hmm-states numbered zero, for t = 0
  // ... num_time_steps_ - 1, were included as the 'arbitrary factors' in the
  // transition-probs, so we need to multiply them all together (not inversed)
  // and add them as a correction term to the total log-likes.  Note: the
  // purpose of the arbitrary scaling factors was to keep things in a good
  // floating-point range.
  CuSubMatrix<BaseFloat> inv_arbitrary_scales(
      alpha_, 0, num_time_steps_,
      num_sequences_ * hmm_.SpecialHmmState(), num_sequences_);
  CuMatrix<BaseFloat> log_inv_arbitrary_scales(
      inv_arbitrary_scales);
  log_inv_arbitrary_scales.ApplyLog();
  BaseFloat log_inv_arbitrary_scales_product =
      log_inv_arbitrary_scales.Sum();
  return tot_log_prob + log_inv_arbitrary_scales_product;
}

void CctcNegativeComputation::Backward(
    CuMatrixBase<BaseFloat> *nnet_output_deriv,
    CuMatrixBase<BaseFloat> *denominators_deriv) {
  // we need to zero the log-numerator-derivs becaus the
  // backprop function adds to them rather than setting them.
  log_numerator_derivs_rearranged_.Resize(numerators_rearranged_.NumRows(),
                                          numerators_rearranged_.NumCols());
  // ... but it sets the denominator-derivs.
  denominator_derivs_rearranged_.Resize(denominators_rearranged_.NumRows(),
                                        denominators_rearranged_.NumCols(),
                                        kUndefined);

  // The real backward computation happens here.
  BackwardInternal();

  // do the deriv ative rearrangement.
  CuSubMatrix<BaseFloat> log_numerator_deriv(*nnet_output_deriv,
                                         0, nnet_output_deriv->NumRows(),
                                         0, numerator_dim_);
  RearrangeNnetOutputReverse(log_numerator_derivs_rearranged_,
                             &log_numerator_deriv);
  // set the remaining part of nnet_output_deriv to zero.
  nnet_output_deriv->ColRange(numerator_dim_,
                              trans_model_.NumBlankIndexes()).SetZero();
  RearrangeNnetOutputReverse(denominator_derivs_rearranged_,
                             denominators_deriv);
}


void CctcNegativeComputation::BackwardInternal() {
  BetaLastFrame();
  for (int32 t = num_time_steps_ - 1; t >= 0; t--)
    BetaGeneralFrame(t);
}

void CctcNegativeComputation::BetaLastFrame() {
  // sets up the beta on the last frame (frame == num_time_steps_).  Note that
  // the betas we use here contain a 1/(tot-prob) factor in order to simplify
  // the backprop.

  int32 t = num_time_steps_;
  BaseFloat *last_frame_beta = beta_.RowData(t % 2);

  // create a 'fake matrix' - view this row as a matrix.
  CuSubMatrix<BaseFloat> beta_mat(last_frame_beta,
                                  num_hmm_states_,
                                  num_sequences_,
                                  num_sequences_);
  CuVector<BaseFloat> inv_tot_prob(tot_prob_);
  inv_tot_prob.InvertElements();
  // the beta values at the end of the file only vary with the sequence-index,
  // not with the HMM-index.  There is no notion of final-prob; the sequence
  // ends when it ends, and at that point we treat all states as having a
  // final-prob of one (which we treat as p of being final given that it just
  // ended, i.e. the probability of a sure thing, which is one).
  beta_mat.CopyRowsFromVec(inv_tot_prob);
}

void CctcNegativeComputation::BetaGeneralFrame(int32 t) {
  KALDI_ASSERT(t >= 0 && t < num_time_steps_);
  const BaseFloat *this_alpha = alpha_.RowData(t),
      *next_beta = beta_.RowData((t + 1) % 2);
  BaseFloat *this_beta = beta_.RowData(t % 2);
  const Int32Pair *forward_transitions = hmm_.ForwardTransitions();
  const CctcHmmTransition *transitions = hmm_.Transitions();
  const BaseFloat *num_probs = numerators_rearranged_.RowData(t),
      *den_probs = denominators_rearranged_.RowData(t);
  BaseFloat *log_num_deriv = log_numerator_derivs_rearranged_.RowData(t),
      *den_deriv = denominator_derivs_rearranged_.RowData(t);
  // we'll add the arbitrary-scale thing later on.
  int32 num_hmm_states = num_hmm_states_,
      num_sequences = num_sequences_,
      special_hmm_state = hmm_.SpecialHmmState();

#if HAVE_CUDA == 1
  if (CuDevice::Instantiate().Enabled()) {
    Timer tim;
    dim3 dimBlock(std::min<int32>(CU1DBLOCK, num_sequences), 1, 1);
    dim3 dimGrid(n_blocks(num_sequences, dimBlock.x), num_hmm_states, 1);
    cuda_ctc_hmm_backward(dimGrid, dimBlock, forward_transitions, transitions, t,
                          num_sequences, special_hmm_state,
                          num_probs, den_probs, this_alpha, next_beta,
                          this_beta, log_num_deriv, den_deriv);
    CU_SAFE_CALL(cudaGetLastError());
    CuDevice::Instantiate().AccuProfile(__func__, tim.Elapsed());
  } else
#endif
  {
    for (int32 h = 0; h < num_hmm_states; h++) {
      for (int32 s = 0; s < num_sequences; s++) {
        BaseFloat this_alpha_prob = this_alpha[h * num_sequences + s],
            inv_arbitrary_scale =
            this_alpha[special_hmm_state * num_sequences + s];
        double tot_variable_factor = 0.0;
        BaseFloat this_den_prob = den_probs[h * num_sequences + s],
            common_factor = 1.0 / (this_den_prob * inv_arbitrary_scale),
            occupation_factor = common_factor * this_alpha_prob;
        const CctcHmmTransition
            *trans_iter = transitions + forward_transitions[h].first,
            *trans_end = transitions + forward_transitions[h].second;
        for (; trans_iter != trans_end; ++trans_iter) {
          BaseFloat transition_prob = trans_iter->transition_prob;
          int32 num_index = trans_iter->num_index,
              next_hmm_state = trans_iter->hmm_state;
          BaseFloat variable_factor = transition_prob *
              next_beta[next_hmm_state * num_sequences + s] *
              num_probs[num_index * num_sequences + s];
          tot_variable_factor += variable_factor;
          BaseFloat occupation_prob = variable_factor * occupation_factor;
          log_num_deriv[num_index * num_sequences + s] += occupation_prob;
        }
        // d(objf) / d(den) is an occupation count times the denominator
        // prob.
        den_deriv[h * num_sequences + s] =
            - tot_variable_factor * occupation_factor / this_den_prob;
        this_beta[h * num_sequences + s] = tot_variable_factor * common_factor;
      }
    }
  }
}


}  // namespace ctc
}  // namespace kaldi