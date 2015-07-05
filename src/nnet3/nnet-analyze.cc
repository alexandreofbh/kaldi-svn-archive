// nnet3/nnet-analyze.cc

// Copyright      2015  Johns Hopkins University (author: Daniel Povey)

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

#include "nnet3/nnet-analyze.h"

namespace kaldi {
namespace nnet3 {

void ComputationVariables::ComputeSplitPoints(
    const NnetComputation &computation) {
  // note, these numbers are only valid if you include the empty zero-indexed
  // matrix/submatrix as a matrix.
  int32 num_matrices = computation.matrices.size(),
      num_submatrices = computation.submatrices.size();
  split_points_.resize(num_matrices);
  KALDI_ASSERT(computation.submatrices[0].num_rows == 0);
  for (int32 submatrix_index = 1;
       submatrix_index < num_submatrices;
       submatrix_index++) {
    const NnetComputation::SubMatrixInfo &s =
        computation.submatrices[submatrix_index];
    split_points_[s.matrix_index].push_back(s.col_offset);
    split_points_[s.matrix_index].push_back(s.col_offset + s.num_cols);
  }
  for (int32 matrix_index = 1; matrix_index < num_matrices; matrix_index++) {
    SortAndUniq(&(split_points_[matrix_index]));
    // should have at least 0 and num_rows included, so size >= 2.
    KALDI_ASSERT(split_points_[matrix_index].size() >= 2);
  }
  // note: the last split point of each matrix doesn't get its own variable index.
  matrix_to_variable_index_.resize(num_matrices + 1);
  matrix_to_variable_index_[0] = 0;
  matrix_to_variable_index_[1] = 0;
  for (int32 matrix_index = 1; matrix_index < num_matrices; matrix_index++) {
    int32 num_variables = split_points_[matrix_index].size() - 1;
    KALDI_ASSERT(num_variables >= 1);
    matrix_to_variable_index_[matrix_index+1] =
        matrix_to_variable_index_[matrix_index] + num_variables;
  }
  num_variables_ = matrix_to_variable_index_.back();
}

void ComputationVariables::ComputeVariableRanges(
    const NnetComputation &computation) {
  // note, these numbers are only valid if you include the empty zero-indexed
  // matrix/submatrix as a matrix.
  int32 num_submatrices = computation.submatrices.size();

  variable_ranges_.resize(num_submatrices);
  variable_ranges_[0] = std::pair<int32,int32>(0, 0);

  full_row_range_.resize(num_submatrices);
  
  for (int32 submatrix_index = 1;
       submatrix_index < num_submatrices;
       submatrix_index++) {
    const NnetComputation::SubMatrixInfo &s =
        computation.submatrices[submatrix_index];
    int32 matrix_index = s.matrix_index;
    int32 start_dim = s.col_offset, end_dim = start_dim + s.num_cols;
    const std::vector<int32> &split = split_points_[matrix_index];
    // std::lower_bound does a binary search -> faster than std::find.
    std::vector<int32>::const_iterator iter = std::lower_bound(
        split.begin(), split.end(), start_dim);
    KALDI_ASSERT(*iter == start_dim);  // or code error.
    int32 start_split_point_index = iter - split.begin();
    iter = std::lower_bound(iter, split.end(), end_dim);
    KALDI_ASSERT(*iter == end_dim);  // or code error.
    int32 end_split_point_index = iter - split.begin();
    int32 matrix_offset = matrix_to_variable_index_[matrix_index];
    int32 start_variable_index = matrix_offset + start_split_point_index,
        end_variable_index = matrix_offset + end_split_point_index;
    KALDI_ASSERT(end_variable_index > start_variable_index);
    variable_ranges_[submatrix_index].first = start_variable_index;
    variable_ranges_[submatrix_index].second = end_variable_index;
    full_row_range_[submatrix_index] =
        (s.row_offset == 0 && s.num_rows ==
         computation.matrices[matrix_index].num_rows);
  }
}

void ComputationVariables::ComputeVariableToMatrix(
    const NnetComputation &computation) {
  variable_to_matrix_.clear();
  variable_to_matrix_.resize(NumVariables(), -1);
  int32 num_submatrices = variable_ranges_.size();
  for (int32 submatrix_index = 1;
       submatrix_index < num_submatrices;
       submatrix_index++) {
    int32 matrix_index = computation.submatrices[submatrix_index].matrix_index;
    int32 variable_start = variable_ranges_[submatrix_index].first,
        variable_end = variable_ranges_[submatrix_index].second;
    for (int32 variable_index = variable_start;
         variable_index < variable_end;
         variable_index++) {
      if (variable_to_matrix_[variable_index] == -1) {
        variable_to_matrix_[variable_index] = matrix_index;
      } else {
        KALDI_ASSERT(variable_to_matrix_[variable_index] == matrix_index);
      }
    }
  }
  // make sure we covered all variables.
  KALDI_ASSERT(std::count(variable_to_matrix_.begin(),
                          variable_to_matrix_.end(), -1) == 0);
}

ComputationVariables::ComputationVariables(
    const NnetComputation &computation) {
  ComputeSplitPoints(computation);
  ComputeVariableRanges(computation);
  ComputeVariableToMatrix(computation);
}

int32 ComputationVariables::GetMatrixForVariable(int32 variable) const {
  KALDI_ASSERT(static_cast<size_t>(variable) < variable_to_matrix_.size());
  return variable_to_matrix_[variable];
}

void ComputationVariables::AppendVariablesForSubmatrix(
    int32 submatrix_index,
    std::vector<int32> *variable_indexes) const {
  KALDI_ASSERT(static_cast<size_t>(submatrix_index) < variable_ranges_.size());
  int32 start = variable_ranges_[submatrix_index].first,
      end = variable_ranges_[submatrix_index].second;
  for (int32 variable_index = start; variable_index < end; variable_index++)
    variable_indexes->push_back(variable_index);
}

void ComputationVariables::AppendVariablesForMatrix(
    int32 matrix_index,
    std::vector<int32> *variable_indexes) const {
  KALDI_ASSERT(static_cast<size_t>(matrix_index + 1) <
               matrix_to_variable_index_.size());
  int32 start = matrix_to_variable_index_[matrix_index],
      end = matrix_to_variable_index_[matrix_index + 1];

  for (int32 variable_index = start; variable_index < end; variable_index++)
    variable_indexes->push_back(variable_index);
}

void ComputationVariables::RecordAccessForSubmatrix(
    int32 submatrix_index,
    AccessType access_type,
    CommandAttributes *ca) const {
  switch (access_type) {
    case kReadAccess:
      AppendVariablesForSubmatrix(submatrix_index,
                                  &(ca->variables_read));
      break;
    case kWriteAccess:
      AppendVariablesForSubmatrix(submatrix_index,
                                  &(ca->variables_written));
      // if submatrix does not span the full row range of the matrix,
      // a write operation has to be considered a read/write operation
      // on the underlying variable.
      if (!full_row_range_[submatrix_index])
        AppendVariablesForSubmatrix(submatrix_index,
                                    &(ca->variables_read));
      break;
    case kReadWriteAccess:
      AppendVariablesForSubmatrix(submatrix_index,
                                  &(ca->variables_written));
      AppendVariablesForSubmatrix(submatrix_index,
                                  &(ca->variables_read));
  }
}



/// given a vector of pairs from computation.indexes_multi_indexes
/// containing paris (submatrix-index, row-index), this function outputs
/// to "submatrix_indexes" all (unique) submatrix indexes that appear;
/// and it outputs to "contains_null_marker" true if the pair (-1, -1)
/// appears anywhere in indexes_multi, and false otherwise.
static void IndexesMultiToSubmatrixIndexes(
    const std::vector<std::pair<int32, int32> > &indexes_multi,
    std::vector<int32> *submatrix_indexes) {
  submatrix_indexes->clear();
  std::vector<std::pair<int32, int32> >::const_iterator
      iter = indexes_multi.begin(), end = indexes_multi.end();
  int32 cur_submatrix_index = -1; // an optimization.
  for (; iter != end; ++iter) {
    int32 submatrix_index = iter->first;
    if (submatrix_index != -1 && submatrix_index != cur_submatrix_index) {
      cur_submatrix_index = submatrix_index;
      submatrix_indexes->push_back(submatrix_index);
    }
  }
  SortAndUniq(submatrix_indexes);
}


void ComputeCommandAttributes(
    const Nnet &nnet,
    const NnetComputation &computation,
    const ComputationVariables &vars,
    std::vector<CommandAttributes> *attributes) {
  int32 num_commands = computation.commands.size();
  attributes->clear();
  attributes->resize(num_commands);
  for (int32 command_index = 0; command_index < num_commands; command_index++) {
    const NnetComputation::Command &c = computation.commands[command_index];
    CommandAttributes &attr = (*attributes)[command_index];
    switch (c.command_type) {
      case NnetComputation::kResizeMatrixZeroed:
        vars.AppendVariablesForMatrix(c.arg1, &attr.variables_written);
        break;
      case NnetComputation::kResizeMatrixUndefined: // nothing is written here. 
        break;
      case NnetComputation::kResizeMatrixEmpty: // ditto.
        break;
      case NnetComputation::kPropagate:
        vars.RecordAccessForSubmatrix(c.arg2, kReadAccess, &attr);
        if (nnet.GetComponent(c.arg1)->Properties() & kPropagateAdds)
          vars.RecordAccessForSubmatrix(c.arg3, kReadWriteAccess, &attr);
        else
          vars.RecordAccessForSubmatrix(c.arg3, kWriteAccess, &attr);        
        break;
      case NnetComputation::kStoreStats:
        vars.RecordAccessForSubmatrix(c.arg2, kReadAccess, &attr);        
      case NnetComputation::kBackprop:
        vars.RecordAccessForSubmatrix(c.arg4, kReadAccess, &attr);
        vars.RecordAccessForSubmatrix(c.arg5, kReadAccess, &attr);
        vars.RecordAccessForSubmatrix(c.arg6, kReadAccess, &attr);
        if (nnet.GetComponent(c.arg1)->Properties() & kBackpropAdds)      
          vars.RecordAccessForSubmatrix(c.arg7, kReadWriteAccess, &attr);
        else
          vars.RecordAccessForSubmatrix(c.arg7, kWriteAccess, &attr);        
        if (nnet.GetComponent(c.arg2)->Properties() & kUpdatableComponent)
          attr.has_side_effects = true;
        break;
      case NnetComputation::kMatrixCopy:
        vars.RecordAccessForSubmatrix(c.arg1, kWriteAccess, &attr);
        vars.RecordAccessForSubmatrix(c.arg2, kReadAccess, &attr);
        break;
      case NnetComputation::kMatrixAdd:      
        vars.RecordAccessForSubmatrix(c.arg1, kReadWriteAccess, &attr);
        vars.RecordAccessForSubmatrix(c.arg2, kReadAccess, &attr);
        break;
      case NnetComputation::kAddRows:
        vars.RecordAccessForSubmatrix(c.arg1, kReadWriteAccess, &attr);
        vars.RecordAccessForSubmatrix(c.arg2, kReadAccess, &attr);
        break;      
      case NnetComputation::kCopyRows: {
        const std::vector<int32> &indexes = computation.indexes[c.arg3];
        // if there are -1's in "indexes", then the result of the operation
        // will depend on the initial value of the matrix, so it's
        // a "rw" operation, not a "write" operation.
        if (std::count(indexes.begin(), indexes.end(), -1) > 0)
          vars.RecordAccessForSubmatrix(c.arg1, kReadWriteAccess, &attr);
        else
          vars.RecordAccessForSubmatrix(c.arg1, kWriteAccess, &attr);
        vars.RecordAccessForSubmatrix(c.arg2, kReadAccess, &attr);
        break;
      }
      case NnetComputation::kAddRowsMulti: {
        vars.RecordAccessForSubmatrix(c.arg1, kReadWriteAccess, &attr);
        std::vector<int32> submatrix_indexes;
        IndexesMultiToSubmatrixIndexes(computation.indexes_multi[c.arg2],
                                       &submatrix_indexes);
        for (size_t i = 0; i < submatrix_indexes.size(); i++)
          vars.RecordAccessForSubmatrix(submatrix_indexes[i],
                                        kReadAccess, &attr);
        break;
      }
      case NnetComputation::kCopyRowsMulti: {
        std::vector<int32> submatrix_indexes;
        IndexesMultiToSubmatrixIndexes(computation.indexes_multi[c.arg2],
                                       &submatrix_indexes);
        // note: the CopyRows command assigns zero in cases where
        // there is no source for some row
        vars.RecordAccessForSubmatrix(c.arg1, kWriteAccess, &attr);
        for (size_t i = 0; i < submatrix_indexes.size(); i++)
          vars.RecordAccessForSubmatrix(submatrix_indexes[i],
                                        kReadAccess, &attr);
        break;
      }
      case NnetComputation::kAddToRowsMulti:
      case NnetComputation::kCopyToRowsMulti: {
        vars.RecordAccessForSubmatrix(c.arg1, kReadAccess, &attr);
        // if the submatrixes we're writing to (in kCopyToRowsMulti) had all
        // rows covered, it would be a pure write operation.
        std::vector<int32> submatrix_indexes;
        IndexesMultiToSubmatrixIndexes(computation.indexes_multi[c.arg2],
                                       &submatrix_indexes);
        for (size_t i = 0; i < submatrix_indexes.size(); i++)
          vars.RecordAccessForSubmatrix(submatrix_indexes[i], kReadWriteAccess,
                                        &attr);
        break;
      }
      case NnetComputation::kAddRowRanges: {
        vars.RecordAccessForSubmatrix(c.arg1, kReadWriteAccess, &attr);
        vars.RecordAccessForSubmatrix(c.arg2, kReadAccess, &attr);
      }
      case NnetComputation::kNoOperation:
      case NnetComputation::kNoOperationMarker:
        break;
      default:
        KALDI_ERR << "Unknown command type.";
    }
    SortAndUniq(&attr.variables_read);
    SortAndUniq(&attr.variables_written);
  }
}

void ComputeVariableAccesses(
    const ComputationVariables &variables,
    const std::vector<CommandAttributes> &command_attributes,
    std::vector<VariableAccesses> *variable_accesses) {
  int32 num_variables = variables.NumVariables(),
      num_commands = command_attributes.size();
  variable_accesses->clear();
  variable_accesses->resize(num_variables);
  for (int32 c = 0; c < num_commands; c++) {
    const CommandAttributes &attr = command_attributes[c];
    KALDI_ASSERT(IsSortedAndUniq(attr.variables_read));
    KALDI_ASSERT(IsSortedAndUniq(attr.variables_written));
    std::vector<int32> all_variables;
    all_variables.reserve(attr.variables_read.size() +
                          attr.variables_written.size());
    all_variables.insert(all_variables.end(), attr.variables_read.begin(),
                         attr.variables_read.end());
    all_variables.insert(all_variables.end(), attr.variables_written.begin(),
                         attr.variables_written.end());
    SortAndUniq(&all_variables);
    
    std::vector<int32>::const_iterator iter = all_variables.begin(),
        end = all_variables.end();
    for (; iter != end; ++iter) {
      int32 variable_index = *iter;
      bool is_read = std::binary_search(attr.variables_read.begin(),
                                        attr.variables_read.end(),
                                        variable_index),
          is_written = (!is_read ? true :
                        std::binary_search(attr.variables_written.begin(),
                                           attr.variables_written.end(),
                                           variable_index));
      if (is_read && is_written) {
        (*variable_accesses)[variable_index].accesses.push_back(
            VariableAccesses::Access(c, kReadWriteAccess));
      } else if (is_read) {
        (*variable_accesses)[variable_index].accesses.push_back(
            VariableAccesses::Access(c, kReadAccess));
      } else {
        (*variable_accesses)[variable_index].accesses.push_back(
            VariableAccesses::Access(c, kWriteAccess));
      }
    }
  }
}

void ComputeMatrixAccesses(
    const Nnet &nnet,
    const NnetComputation &computation,
    const ComputationVariables &variables,
    const std::vector<CommandAttributes> &command_attributes,
    std::vector<MatrixAccesses> *matrix_accesses) {
  int32 num_matrices = computation.matrices.size(),
      num_commands = command_attributes.size();
  matrix_accesses->clear();
  matrix_accesses->resize(num_matrices);
  for (int32 c = 0; c < num_commands; c++) {
    const CommandAttributes &attr = command_attributes[c];
    for (int32 i = 0; i < 2; i++) {
      const std::vector<int32> &vars = (i == 0 ? attr.variables_read :
                                        attr.variables_written);
      std::vector<int32>::const_iterator iter = vars.begin(), end = vars.end();
      for (; iter != end; ++iter) {
        int32 variable_index = *iter,
            matrix_index = variables.GetMatrixForVariable(variable_index);
        KALDI_ASSERT(matrix_index > 0 && matrix_index < num_matrices);
        (*matrix_accesses)[matrix_index].access_commands.push_back(c);
      }
    }
    
    const NnetComputation::Command &command = computation.commands[c];
    int32 matrix_index = command.arg1;
    
    switch (command.command_type) {
      case NnetComputation::kResizeMatrixZeroed:
      case NnetComputation::kResizeMatrixUndefined:
        if ((*matrix_accesses)[matrix_index].initialize_command != -1)
          KALDI_ERR << "Matrix " << matrix_index << " initialized twice.";
        (*matrix_accesses)[matrix_index].initialize_command = c;
        break;
      case NnetComputation::kResizeMatrixEmpty:
        if ((*matrix_accesses)[matrix_index].destroy_command != -1)
          KALDI_ERR << "Matrix " << matrix_index << " destroyed twice.";
        (*matrix_accesses)[matrix_index].destroy_command = c;
        break;
      default:
        ;
    }
  }
  // now set up the is_input and is_output fields.
  unordered_map<int32, std::pair<int32, int32> >::const_iterator
      iter = computation.input_output_info.begin(),
      end = computation.input_output_info.end();
  for (; iter != end; ++iter) {
    int32 node_index = iter->first,
        value_matrix_index = iter->second.first,
        deriv_matrix_index = iter->second.second;
    KALDI_ASSERT(value_matrix_index > 0 && value_matrix_index < num_matrices);
    if (nnet.IsInputNode(node_index)) {
      // the assert checks for repeats
      KALDI_ASSERT(!(*matrix_accesses)[value_matrix_index].is_input);
      (*matrix_accesses)[value_matrix_index].is_input = true;
      if (deriv_matrix_index != 0) {
        // the derivatives, if requested, would be outputs of the computation,
        // even though the node is an input node.
        KALDI_ASSERT(!(*matrix_accesses)[deriv_matrix_index].is_output);
        (*matrix_accesses)[deriv_matrix_index].is_output = true;
      }
    } else {
      KALDI_ASSERT(nnet.IsOutputNode(node_index));
      // the assert checks for repeats
      KALDI_ASSERT(!(*matrix_accesses)[value_matrix_index].is_output);
      (*matrix_accesses)[value_matrix_index].is_output = true;
      if (deriv_matrix_index != 0) {
        // the derivatives, if provided, would be inputs to the computation,
        // even though the node is an output node.
        KALDI_ASSERT(!(*matrix_accesses)[deriv_matrix_index].is_input);
        (*matrix_accesses)[deriv_matrix_index].is_input = true;
      }
    }
  }
}
        

ComputationChecker::ComputationChecker(
    const CheckComputationConfig &config,
    const Nnet &nnet,
    const ComputationRequest &request,
    const NnetComputation &computation):
    config_(config), nnet_(nnet), request_(request),
    computation_(computation), variables_(computation) {
}    


void ComputationChecker::Check() {
  CheckComputationIndexes();
  ComputeCommandAttributes(nnet_, computation_,
                           variables_, &attributes_);
  ComputeVariableAccesses(computation_, attributes_,
                        &variable_accesses_);
  ComputeMatrixAccesses(nnet_, computation_, variables_, attributes_,
                        &matrix_accesses_);  
  CheckComputationOrder();
  CheckComputationMatrixAccesses();
  CheckComputationUndefined();  
  if (config_.check_rewrite)
    CheckComputationRewrite();

}


/**
   Checks for the situation where a read-only operation on a variable is
   followed by an operation that writes to the variable.  This should never
   occur prior to optimization, but after certain optimization we in effect
   "re-use" variables by doing things like propagate and backprop in-place, so
   this check shouldn't be performed after optimization.
*/
void ComputationChecker::CheckComputationRewrite() const {
  int32 num_variables = variable_accesses_.size();
  for (int32 v = 0; v < num_variables; v++) {
    const VariableAccesses &accesses = variable_accesses_[v];
    int32 matrix_index = variables_.GetMatrixForVariable(v);
    if (accesses.accesses.empty())
      KALDI_ERR << "Variable " << v << " (part of matrix m"
                << matrix_index << ") "
                << "is never used.";
    int32 num_accesses = accesses.accesses.size();
    int32 first_pure_read = -1;
    for (int32 access = 0; access < num_accesses; access++) {
      if (accesses.accesses[access].access_type == kReadAccess) {
        first_pure_read = access;
        break;
      }
    }
    if (first_pure_read != -1) {
      for (int32 access = first_pure_read + 1;
           access < num_accesses; access++) {
        if (accesses.accesses[access].access_type != kReadAccess) {
          KALDI_ERR << "Variable " << v << " (part of matrix m"
                    << matrix_index << ") "
                    << "is modified after being read "
                    << "(this is not expected before optimization)";
        }
      }
    }
  }
}


/**
   Checks for the situation where a variable is read before being written.
*/
void ComputationChecker::CheckComputationUndefined() const {
  int32 num_variables = variable_accesses_.size();
  for (int32 v = 0; v < num_variables; v++) {
    const VariableAccesses &accesses = variable_accesses_[v];
    int32 matrix_index = variables_.GetMatrixForVariable(v);
    bool is_input = matrix_accesses_[matrix_index].is_input;
    if (accesses.accesses.empty())
      KALDI_ERR << "Variable " << v << " (part of matrix m"
                << matrix_index << ") "
                << "is never used.";
    if (accesses.accesses[0].access_type != kWriteAccess &&
        ! is_input)
      KALDI_ERR << "Variable " << v << " (part of matrix m"
                << matrix_index << ") "
                << "is read before it is written to";
  }
}


/**
   Checks that we never use variables before they are allocated or after they
   are deallocated, and some other checks that can be done from the
   MatrixAccesses.
*/
void ComputationChecker::CheckComputationMatrixAccesses() const {
  int32 num_matrices = matrix_accesses_.size();

  for (int32 matrix_index = 1; matrix_index < num_matrices; matrix_index++) {
    const MatrixAccesses &accesses = matrix_accesses_[matrix_index];
    if (accesses.is_input) {
      if (accesses.initialize_command != -1)
        KALDI_ERR << "Input matrix is initialized.";
    } else {
      if (accesses.initialize_command == -1)
        KALDI_ERR << "Matrix is not initialized.";
      if (accesses.access_commands.empty()) {
        KALDI_ERR << "Matrix m" << matrix_index << " is never accessed.";
      } else if (accesses.access_commands.front() <
                 accesses.initialize_command) {
        KALDI_ERR << "Matrix m" << matrix_index << " is accessed before "
            "it is initialized";
      }
    }
    if (accesses.is_output) {
      if (accesses.destroy_command != -1)
        KALDI_ERR << "Output matrix is destroyed.";
    } else {
      if (accesses.destroy_command == -1)
        KALDI_ERR << "Matrix is not destroyed.";
      if (accesses.access_commands.empty()) {
        KALDI_ERR << "Matrix m" << matrix_index << " is never accessed.";
      } else if (accesses.access_commands.back() >=
                 accesses.destroy_command) {
        KALDI_ERR << "Matrix m" << matrix_index << " is accessed after "
            "it is destroyed";
      }
    }
  }
}

/**
   This very basic check just makes sure that all indexes in the commands are
   within range, that dimensions agree with the request, that row/column dimensions
   agree with component dimensions.
*/
void ComputationChecker::CheckComputationIndexes() const {
  int32 num_commands = computation_.commands.size(),
      num_matrices = computation_.matrices.size(),
      num_submatrices = computation_.submatrices.size();
  const std::vector<NnetComputation::SubMatrixInfo> &submatrices =
      computation_.submatrices;
  
  for (int32 command_index = 0; command_index < num_commands; command_index++) {
    const NnetComputation::Command &c = computation_.commands[command_index];
    switch (c.command_type) {
      case NnetComputation::kResizeMatrixZeroed:
      case NnetComputation::kResizeMatrixUndefined:
      case NnetComputation::kResizeMatrixEmpty:
        if (c.arg1 < 1 || c.arg1 >= num_matrices)
          KALDI_ERR << "matrix index out of range.";
        break;
      case NnetComputation::kPropagate: {
        if (c.arg1 < 0 || c.arg1 >= nnet_.NumComponents())
          KALDI_ERR << "Component index out of range";
        const Component *component = nnet_.GetComponent(c.arg1);
        int32 properties = component->Properties();
        if (c.arg2 < 0 ||
            c.arg2 > computation_.component_precomputed_indexes.size())
          KALDI_ERR << "Precomputed-indexes index out of range";
        if (c.arg2 != 0 && (properties & kSimpleComponent))
          KALDI_ERR << "Precomputed-indexes index nonzero for simple component";
        // note: input may be the empty matrix (in unusual circumstances, for non-simple
        // components).
        if (c.arg3 < 0 || c.arg3 >= num_submatrices ||
            (c.arg3 == 0 && !(properties & kSimpleComponent)) ||
            c.arg4 < 1 || c.arg4 >= num_submatrices)
          KALDI_ERR << "Sub-matrix indexes out of range.";
        if (submatrices[c.arg3].num_cols != component->InputDim())
          KALDI_ERR << "Input-dim mismatch.";
        if (submatrices[c.arg4].num_cols != component->OutputDim())
          KALDI_ERR << "Input-dim mismatch.";
        if ((properties & kSimpleComponent) &&
            submatrices[c.arg3].num_rows !=
            submatrices[c.arg4].num_rows)
          KALDI_ERR << "Num-rows mismatch for simple component.";
        if (!(properties & kPropagateInPlace) &&
            c.arg3 == c.arg4)
          KALDI_ERR << "In-place propagation not supported for this component";
        break;
      }
      case NnetComputation::kStoreStats: {
        if (c.arg1 < 0 || c.arg1 >= nnet_.NumComponents())
          KALDI_ERR << "Component index out of range";
        const Component *component = nnet_.GetComponent(c.arg1);
        int32 properties = component->Properties();
        if (!(properties & kStoresStats))
          KALDI_ERR << "StoreStats called on component that does not do it.";
        if (c.arg2 < 1 || c.arg2 >= num_submatrices)
          KALDI_ERR << "Invalid sub-matrix index in StoreStats";
        if (submatrices[c.arg2].num_cols != component->OutputDim())
          KALDI_ERR << "Dimension mismatch in StoreStats";
      }
      case NnetComputation::kBackprop: {
        if (c.arg1 < 0 || c.arg1 >= nnet_.NumNodes())
          KALDI_ERR << "Node index in backprop out of range";
        if (c.arg2 < 0 || c.arg2 >= nnet_.NumComponents());
        KALDI_ERR << "Component index in backprop out of range";
        const Component *component = nnet_.GetComponent(c.arg2);
        int32 properties = component->Properties();
        if (c.arg3 < 0 ||
            c.arg3 > computation_.component_precomputed_indexes.size())
          KALDI_ERR << "Precomputed-indexes index out of range";
        if (c.arg3 != 0 && (properties & kSimpleComponent))
          KALDI_ERR << "Precomputed-indexes index nonzero for simple component";
        // output-deriv (arg6) must be supplied; others could plausibly be zero.
        if (c.arg4 < 0 || c.arg4 >= num_submatrices ||
            c.arg5 < 0 || c.arg5 >= num_submatrices ||
            c.arg6 < 1 || c.arg6 >= num_submatrices ||
            c.arg7 < 0 || c.arg7 >= num_submatrices)
          KALDI_ERR << "Submatrix index out of range for backprop.";
        if ((properties & kBackpropNeedsInput) && c.arg4 == 0)
          KALDI_ERR << "Backprop input needed but not supplied.";
        if ((properties & kBackpropNeedsOutput) && c.arg5 == 0)
          KALDI_ERR << "Backprop output needed but not supplied.";
        if (c.arg7 == 0 && !(properties && kUpdatableComponent)) {
          // note: we could perhaps make this just a warning,
          // or optimize it away somehow.
          KALDI_ERR << "Backprop is done but has no effect.";
        }
        if (c.arg6 == c.arg7 && !(properties & kBackpropInPlace))
          KALDI_ERR << "In-place backprop used where not supported.";
        if (c.arg4 != 0 &&
            submatrices[c.arg4].num_cols != component->InputDim())
          KALDI_ERR << "Input-dim mismatch in backprop.";
        if (c.arg5 != 0 &&
            submatrices[c.arg5].num_cols != component->OutputDim())
          KALDI_ERR << "Output-dim mismatch in backprop.";
        if (c.arg6 != 0 &&
            submatrices[c.arg6].num_cols != component->OutputDim())
          KALDI_ERR << "Output-dim mismatch in backprop.";
        if (c.arg7 != 0 &&
            submatrices[c.arg7].num_cols != component->InputDim())
          KALDI_ERR << "Input-dim mismatch in backprop.";
        // check num-rows consistency for input.
        if (c.arg4 != 0 && c.arg7 != 0 &&
            submatrices[c.arg4].num_rows != submatrices[c.arg7].num_rows)
          KALDI_ERR << "Num-rows mismatch in backprop input";
        // check num-rows consistency for output
        if (c.arg5 != 0 ||
            submatrices[c.arg5].num_rows != submatrices[c.arg6].num_rows)
          KALDI_ERR << "Num-rows mismatch in backprop output";
        if ((properties & kSimpleComponent) && c.arg7 != 0 &&
            submatrices[c.arg6].num_rows != submatrices[c.arg7].num_rows)
          KALDI_ERR << "Num-rows mismatch in backprop input vs output.";
        break;
      }
      case NnetComputation::kMatrixCopy:
      case NnetComputation::kMatrixAdd:
        if (c.arg1 < 1 || c.arg1 >= num_submatrices ||
            c.arg2 < 1 || c.arg2 >= num_submatrices)
          KALDI_ERR << "Submatrix indexes out of range in matrix copy/add";
        if (submatrices[c.arg1].num_rows != submatrices[c.arg2].num_rows ||
            submatrices[c.arg1].num_cols != submatrices[c.arg2].num_cols)
          KALDI_ERR << "Submatrix indexes out of range in matrix copy/add";
        if (c.arg1 == c.arg2)
          KALDI_ERR << "Adding/copying to self";
        break;
      case NnetComputation::kAddRows:
      case NnetComputation::kCopyRows: {
        if (c.arg1 < 1 || c.arg1 >= num_submatrices ||
            c.arg2 < 1 || c.arg2 >= num_submatrices ||
            static_cast<size_t>(c.arg3) >= computation_.indexes.size())
          KALDI_ERR << "Index out of range in add-rows/copy-rows command.";
        const std::vector<int32> &indexes = computation_.indexes[c.arg3];
        if (indexes.size() != static_cast<size_t>(submatrices[c.arg1].num_rows))
          KALDI_ERR << "Indexes size mismatch in add-rows/copy-rows";
        if (submatrices[c.arg1].num_cols != submatrices[c.arg2].num_cols)
          KALDI_ERR << "Dimension mismatch in add-rows/copy-rows";
        if (*std::max_element(indexes.begin(), indexes.end()) >=
            submatrices[c.arg2].num_rows)
          KALDI_ERR << "Row-index out of range in add-rows/copy-rows";
        if (c.arg1 == c.arg2)
          KALDI_ERR << "Copying to self in add-rows/copy-rows command.";
        break;
      }
      case NnetComputation::kAddRowsMulti:
      case NnetComputation::kCopyRowsMulti:
      case NnetComputation::kAddToRowsMulti:
      case NnetComputation::kCopyToRowsMulti: {
        if (c.arg1 < 1 || c.arg1 >= num_submatrices ||
            static_cast<size_t>(c.arg2) >= computation_.indexes_multi.size())
          KALDI_ERR << "Index out of range in *-multi command";
        const std::vector<std::pair<int32, int32> > pairs =
            computation_.indexes_multi[c.arg2];
        int32 num_rows = submatrices[c.arg1].num_rows,
            num_cols =  submatrices[c.arg1].num_cols;
        if (pairs.size() != static_cast<size_t>(num_rows))
          KALDI_ERR << "Indexes dimension mismatch in *-multi command";
        std::vector<std::pair<int32, int32> >::const_iterator
            iter = pairs.begin(), end = pairs.end();
        for (; iter != end; ++iter) {
          int32 submatrix_index = iter->first, row_index = iter->second;
          if (submatrix_index == -1) {
            if (row_index != -1)
              KALDI_ERR << "Expected -1 row index if submatrix index is -1";
          } else {
            if (submatrix_index < 1 || submatrix_index >= num_submatrices)
              KALDI_ERR << "Submatrix index out of range in indexes_multi";
            if (row_index < 0 ||
                row_index >= submatrices[submatrix_index].num_rows)
              KALDI_ERR << "Row index out of range in indexes_multi";
            if (submatrix_index == c.arg1)
              KALDI_ERR << "Copying from self in *-multi command.";
            if (submatrices[submatrix_index].num_cols != num_cols)
              KALDI_ERR << "Mismatching dimension in *-multi command";
          }
        }
        if (c.command_type == NnetComputation::kAddToRowsMulti ||
            c.command_type == NnetComputation::kCopyToRowsMulti) {
          // check for duplicates; these are not allowed in kAddToRowsMulti
          // or kCopyToRowsMulti because they would necessitate extra work
          // in CUDA kernels.
          std::vector<std::pair<int32, int32> > pairs_copy(pairs);
          std::sort(pairs_copy.begin(), pairs_copy.end());
          std::vector<std::pair<int32, int32> >::const_iterator
              iter = pairs_copy.begin(), end = pairs_copy.end(),
              next_iter;
          for (; iter != end; ++iter) {
            next_iter = iter;
            ++next_iter;
            if (next_iter != end && *iter == *next_iter &&
                iter->first != -1) {
              KALDI_ERR << "Duplicate element "
                        << iter->first << ',' << iter->second << " found in "
                        << "indexes for {add,copy}-to-rows-multi command.";
            }
          }
        }
        break;
      }
      case NnetComputation::kAddRowRanges: {
        if (c.arg1 < 1 || c.arg1 >= num_submatrices ||
            c.arg2 < 1 || c.arg2 >= num_submatrices ||
            static_cast<size_t>(c.arg3) >= computation_.indexes_ranges.size())          
          KALDI_ERR << "Index out of range in add-row-ranges command";
        const std::vector<std::pair<int32, int32> > pairs =
            computation_.indexes_ranges[c.arg2];
        if (static_cast<size_t>(submatrices[c.arg1].num_rows) != pairs.size())
          KALDI_ERR << "Num-rows mismatch in add-row-ranges command";
        if (submatrices[c.arg1].num_cols != submatrices[c.arg2].num_cols)
          KALDI_ERR << "Dimension mismatch in add-row-ranges command";
        int32 src_num_rows = submatrices[c.arg2].num_rows;
        std::vector<std::pair<int32, int32> >::const_iterator
            iter = pairs.begin(), end = pairs.end();
        for (; iter != end; ++iter) {
          // note: -1's are not allowed.  To represent the empty range,
          // the user should use some valid index twice.
          if (iter->second < iter->first || iter->first < 0 ||
              iter->second > src_num_rows)
            KALDI_ERR << "Row range " << iter->first << ',' << iter->second
                      << " out of range in add-row-ranges command.";
        }
        break;
      }
      case NnetComputation::kNoOperation:
      case NnetComputation::kNoOperationMarker:
        break;
      default:
        KALDI_ERR << "Unknown command type.";
    }
  }
}


// make sure Propagate comes before kNoOpMarker and Backprop comes after it,
// and that the value of computation_computation_end matches the position of
// kNoOpMarker.
void ComputationChecker::CheckComputationOrder() const {
  int32 num_commands = computation_.commands.size(),
      forward_computation_end = computation_.forward_computation_end;
  if (forward_computation_end < 0 ||
      computation_.forward_computation_end >= num_commands)
    KALDI_ERR << "forward_computation_end has bad value";
  if (computation_.commands[forward_computation_end].command_type !=
      NnetComputation::kNoOperationMarker)
    KALDI_ERR << "expected kNoOpMarker at forward_computation_end";
  for (int32 c = 0; c < num_commands; c++) {
    NnetComputation::CommandType command_type =
        computation_.commands[c].command_type;
    if (c != forward_computation_end &&
        command_type == NnetComputation::kNoOperationMarker)
      KALDI_ERR << "Found kNoOpMarker in unexpected place";
    if (c < forward_computation_end &&
        command_type == NnetComputation::kBackprop)
      KALDI_ERR << "Backprop occurs before kNoOpMarker";
    if (c > forward_computation_end &&
        command_type == NnetComputation::kPropagate)
      KALDI_ERR << "Propagate occurs after kNoOpMarker";
    if (c > forward_computation_end &&
        command_type == NnetComputation::kStoreStats)
      KALDI_ERR << "StoreStats occurs after kNoOpMarker";
  }
}

void ComputeSubmatLists(const NnetComputation &computation,
                        std::vector<std::vector<int32> > *submat_lists) {
  int32 num_matrices = computation.matrices.size(),
      num_submatrices = computation.submatrices.size();
  submat_lists->clear();
  submat_lists->resize(num_matrices);
  for (int32 submatrix_index = 1;
       submatrix_index < num_submatrices;
       submatrix_index++) {
    int32 matrix_index = computation.submatrices[submatrix_index].matrix_index;
    KALDI_ASSERT(matrix_index > 0 && matrix_index < num_matrices);
    (*submat_lists)[matrix_index].push_back(submatrix_index);
  }
}



} // namespace nnet3
} // namespace kaldi
