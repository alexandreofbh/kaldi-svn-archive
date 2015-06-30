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

#include <iterator>
#include <sstream>
#include "nnet3/nnet-computation.h"

namespace kaldi {
namespace nnet3 {

bool ComputationRequest::NeedDerivatives() const {
  bool ans = false;
  if (need_model_derivative)
    ans = true;
  for (size_t i = 0; i < inputs.size(); i++) {
    if (inputs[i].has_deriv) { // derivative requested for this input
      ans = true;
      break;
    }
  }
  if (ans) {
    // check that the output actually provides a derivative, else the
    // request could not be meaningfully satisfied.
    size_t i;
    for (i = 0; i < outputs.size(); i++)
      if (outputs[i].has_deriv)
        break;
    if (i == outputs.size()) {
      KALDI_ERR << "You requested model derivatives or input derivatives, but "
                << "provide no derivatives at the output.";
    }
  }
  return false;
}

int32 ComputationRequest::IndexForInput(
    const std::string &node_name) const {
  int32 ans = -1;
  for (size_t i = 0; i < inputs.size(); i++) {
    if (inputs[i].name == node_name) {
      KALDI_ASSERT(ans == -1 && "Two inputs with the same name");
      ans = i;
    }
  }
  return ans;
}

int32 ComputationRequest::IndexForOutput(
    const std::string &node_name) const {
  int32 ans = -1;
  for (size_t i = 0; i < outputs.size(); i++) {
    if (outputs[i].name == node_name) {
      KALDI_ASSERT(ans == -1 && "Two inputs with the same name");
      ans = i;
    }
  }
  return ans;
}

NnetComputation::~NnetComputation() {
  for (size_t i = 0; i < component_precomputed_indexes.size(); i++)
    delete component_precomputed_indexes[i];
}

void NnetComputation::ComputeCudaIndexes() {
  indexes_cuda.resize(indexes.size());

  for (size_t i = 0; i < indexes.size(); i++)
    indexes_cuda[i].CopyFromVec(indexes[i]);

  std::vector<bool> need_cuda(indexes_multi.size(), false);
  for (int32 c = 0; c < commands.size(); c++) {
    if (commands[c].command_type == kAddRowRanges) {
      int32 indexes_multi_index = commands[c].arg3;
      KALDI_ASSERT(static_cast<size_t>(indexes_multi_index) < need_cuda.size());
      need_cuda[indexes_multi_index] = true;
    }
  }
  KALDI_ASSERT(sizeof(Int32Pair) == sizeof(std::pair<int32,int32>));
  indexes_multi_cuda.resize(indexes_multi.size());
  for (int32 i = 0; i < indexes_multi.size(); i++) {
    if (need_cuda[i]) {
      const std::vector<std::pair<int32,int32> > *input = &(indexes_multi[i]);
      const std::vector<Int32Pair> *input_cast =
          reinterpret_cast<const std::vector<Int32Pair> *>(input);
      // note: the indexes for CUDA use can't very easily use STL types due to
      // the interface of CUDA being plain C.
      indexes_multi_cuda[i].CopyFromVec(*input_cast);
    }
  }
}

int32 NnetComputation::NewSubMatrix(int32 base_matrix, int32 dim_offset,
                                    int32 dim) {
  KALDI_ASSERT(static_cast<size_t>(base_matrix) < matrices.size());
  int32 num_rows = matrices[base_matrix].num_rows,
      num_cols = matrices[base_matrix].num_cols;
  KALDI_ASSERT(dim_offset >= 0 && dim_offset + dim <= num_cols);
  int32 ans = sub_matrices.size();
  KALDI_ASSERT(ans >= matrices.size());
  sub_matrices.push_back(
      NnetComputation::SubMatrixInfo(base_matrix, 0, num_rows,
                                     dim_offset, dim));
  return ans;
}
  
int32 NnetComputation::NewMatrix(int32 num_rows, int32 num_cols) {
  KALDI_ASSERT(num_rows > 0 && num_cols > 0);
  if (matrices.empty()) {  // Set up the zero matrix; index zero is reserved.
    matrices.push_back(MatrixInfo(0, 0));
    sub_matrices.push_back(SubMatrixInfo(0, 0, 0, 0, 0));
  }
  int32 matrix_index = matrices.size(),
      submatrix_index = sub_matrices.size();
  matrices.push_back(MatrixInfo(num_rows, num_cols));
  sub_matrices.push_back(SubMatrixInfo(matrix_index, 0, num_rows, 0, num_cols));
  return submatrix_index;
}

// outputs a string explaining the meaning each sub-matrix in vaguely
// matlab-like notation: for whole matrices, something like "m1", "m2";
// and for parts of matrices, "m1(0:10, 20:40)".
static void GetSubmatrixStrings(const Nnet &nnet,
                                const NnetComputation &computation,
                                std::vector<std::string> *submat_strings) {
  int32 num_submatrices = computation.sub_matrices.size();
  KALDI_ASSERT(num_submatrices > 0);
  submat_strings->resize(num_submatrices);
  (*submat_strings)[0] = "[]";  // the empty matrix
  for (int32 i = 1; i < num_submatrices; i++) {
    const NnetComputation::SubMatrixInfo &submat = computation.sub_matrices[i];
    std::ostringstream os;    
    if (computation.IsWholeMatrix(i)) {
      os << 'm' << submat.matrix_index;
    } else { // part of a range.
      os << 'm' << submat.matrix_index << '(' << submat.row_offset << ':'
         << (submat.row_offset + submat.num_rows - 1) << ", "
         << submat.col_offset << ':' << (submat.col_offset + submat.num_cols - 1)
         << ')';
    }
    (*submat_strings)[i] = os.str();    
  }  
}

// outputs a string containing a text form of each of the elements of the
// "indexes" vector: if indexes[i] is (1, 2, 3), then (*indexes_strings)[i]
// is "1,2,3".
static void GetIndexesStrings(const Nnet &nnet,
                              const NnetComputation &computation,
                              std::vector<std::string> *indexes_strings) {
  int32 size = computation.indexes.size();
  indexes_strings->resize(size);
  for (int32 i = 0; i < size; i++) {
    std::ostringstream os;
    PrintIntegerVector(os, computation.indexes[i]);
    (*indexes_strings)[i] = os.str();
  }
}

// outputs a string containing a text form of each of the elements of the
// "indexes_multi" vector.  this requires a little care because the vectors of
// pairs in indexes_multi can have a couple of different meanings.  If used in
// kAddRowsMulti, KAddToRowsMulti, kCopyRowsMulti or kCopyToRowsMulti it's pairs
// (sub-matrix index, row index).  Here, while the .first of each element refers
// to a sub-matrix index, it's only the actual matrices that have names, so we
// have to go back to the names of the actual matrices and maybe specify a
// dimension range.  In a simple case it would be e.g. m1(1,:); in a harder case
// it would be e.g. m1(2, 10:19).  Also the vectors in "indexes_multi" may be used
// in commands of type kAddRowRange where each pair refers to a row range like
// 10-19.  [note, the pair 10,20 would mean the range 10-19 and we print in
// the 10-19 format].  We figure out how each vector is used and print the
// string in the appropriate format.
static void GetIndexesMultiStrings(
    const Nnet &nnet,
    const NnetComputation &computation,
    std::vector<std::string> *indexes_multi_strings) {
  int32 indexes_multi_size = computation.indexes_multi.size();
  indexes_multi_strings->resize(indexes_multi_size);
  std::vector<bool> is_row_range(indexes_multi_size, false);
  for (int32 c = 0; c < computation.commands.size(); c++)
    if (computation.commands[c].command_type == NnetComputation::kAddRowRanges)
      is_row_range[computation.commands[c].arg3] = true;
  
  for (int32 i = 0; i < indexes_multi_size; i++) {
    bool row_range = is_row_range[i];
    std::ostringstream os;
    os << "[";
    const std::vector<std::pair<int32, int32> > &vec =
        computation.indexes_multi[i];
    int32 size = vec.size();
    for (int32 j = 0; j < size; j++) {
      if (row_range) {
        os << vec[j].first << ":" << (vec[j].second - 1);
      } else {
        int32 submat_index = vec[j].first, row_index = vec[j].second;
        const NnetComputation::SubMatrixInfo &submat =
            computation.sub_matrices[submat_index];
        const NnetComputation::MatrixInfo &mat =
            computation.matrices[submat.matrix_index];
        int32 row = row_index + submat.row_offset;
        int32 col_start = submat.col_offset,
            col_end = col_start + submat.num_cols;
        KALDI_ASSERT(row < mat.num_rows);
        if (col_start == 0 && col_end == mat.num_cols)
          os << 'm' << submat.matrix_index << '(' << row << ",:)";
        else
          os << 'm' << submat.matrix_index << '(' << row << ',' << col_start
             << ':' << (col_end - 1) << ')';
      }
      if (j + 1 < size) os << ",";
    }
    os << "]";
    (*indexes_multi_strings)[i] = os.str();
  }
}


// writes to "os" the statement for this command.
static void PrintCommand(std::ostream &os,
                         const Nnet &nnet,                         
                         const NnetComputation &computation,
                         int32 command_index,
                         const std::vector<std::string> &submatrix_strings,
                         const std::vector<std::string> &indexes_strings,
                         const std::vector<std::string> &indexes_multi_strings) {
  KALDI_ASSERT(command_index < computation.commands.size());
  const NnetComputation::Command &c = computation.commands[command_index];
  switch (c.command_type) {
    case NnetComputation::kResizeMatrixZeroed:
      os << "m" << c.arg1 << " = zeros("
         << computation.matrices[c.arg1].num_rows
         << ',' << computation.matrices[c.arg1].num_cols << ")\n";
      break;
    case NnetComputation::kResizeMatrixUndefined:
      os << "m" << c.arg1 << " = undefined("
         << computation.matrices[c.arg1].num_rows
         << ',' << computation.matrices[c.arg1].num_rows << ")\n";
      break;
    case NnetComputation::kResizeMatrixEmpty:
      os << "m" << c.arg1 << " = []\n";
      break;      
    case NnetComputation::kPropagate:
      os << nnet.GetComponentName(c.arg1) << ".Propagate(";
      if (c.arg2 == 0) os << "NULL, ";
      else os << "precomputed_indexes[" << c.arg2 << "], ";
      os << submatrix_strings[c.arg3] << ", &" << submatrix_strings[c.arg4]
         << ")\n";
      break;
    case NnetComputation::kStoreStats:
      os << nnet.GetComponentName(c.arg1) << ".StoreStats("
         << submatrix_strings[c.arg2] << ")\n";
      break;
    case NnetComputation::kBackprop:
      os << nnet.GetComponentName(c.arg2) << ".Backprop(";
      if (c.arg3 == 0) os << "NULL, ";
      else os << "precomputed_indexes[" << c.arg3 << "], ";
      os << submatrix_strings[c.arg4] << ", "
         << submatrix_strings[c.arg5] << ", "
         << submatrix_strings[c.arg6] << ", &"
         << submatrix_strings[c.arg7] << ")\n";
      break;
    case NnetComputation::kMatrixCopy:
      os << submatrix_strings[c.arg1] << " = "
         << submatrix_strings[c.arg2] << "\n";
      break;
    case NnetComputation::kMatrixAdd:
      os << submatrix_strings[c.arg1] << " += "
         << submatrix_strings[c.arg2] << "\n";
      break;
    case NnetComputation::kAddRows:
    case NnetComputation::kCopyRows:
      os << submatrix_strings[c.arg1] << "."
         << (c.command_type == NnetComputation::kAddRows ? "AddRows" :
             "CopyRows") << "(" << submatrix_strings[c.arg2]
         << indexes_strings[c.arg3] << ")\n";
      break;
    case NnetComputation::kAddRowsMulti:
    case NnetComputation::kAddToRowsMulti:
    case NnetComputation::kCopyRowsMulti:
    case NnetComputation::kCopyToRowsMulti: {
      NnetComputation::CommandType ct = c.command_type;
      os << submatrix_strings[c.arg1] << "."
         << (ct == NnetComputation::kAddRowsMulti ? "AddRowsMulti" :
             (ct == NnetComputation::kAddToRowsMulti? "AddToRowsMulti" :
              (ct == NnetComputation::kCopyRowsMulti ? "CopyRowsMulti" :
               "CopyToRowsMulti"))) << "(" << submatrix_strings[c.arg2]
         << indexes_multi_strings[c.arg2] << ")\n";
      break;
    }
    case NnetComputation::kAddRowRanges:
      os << submatrix_strings[c.arg1] << ".AddRowRanges("
          << submatrix_strings[c.arg2] << ", "
          << indexes_multi_strings[c.arg2] << ")\n";
      break;
    case NnetComputation::kNoOperation:
      os << "[no-op]\n";
      break;
    case NnetComputation::kNoOperationMarker:
      os << "[no-op/marker]\n";
      break;
    default:
      KALDI_ERR << "Un-handled command type.";
  }
}

void NnetComputation::Print(std::ostream &os, const Nnet &nnet) const {
  std::vector<std::string> submatrix_strings, indexes_strings,
      indexes_multi_strings;
  GetSubmatrixStrings(nnet, *this, &submatrix_strings);
  GetIndexesStrings(nnet, *this, &indexes_strings);
  GetIndexesMultiStrings(nnet, *this, &indexes_multi_strings);
  
  // First print info about the matrices.
  os << "matrix ";
  for (int32 i = 1; i < matrices.size(); i++) {
    os << "m" << i << "(" << matrices[i].num_rows
       << ", " << matrices[i].num_cols << ")";
    if (i + 1 < matrices.size())
      os << ", ";
  }
  os << "\n";
  // show which matrices the inputs and outputs map to.
  for (unordered_map<int32, std::pair<int32, int32> >::const_iterator iter =
           input_output_info.begin(); iter != input_output_info.end(); ++iter) {
    int32 node_index = iter->first,
        value_submatrix_index = iter->second.first,
        deriv_submatrix_index = iter->second.second;
    os << nnet.GetNodeName(node_index) << ".value -> "
       << submatrix_strings[value_submatrix_index] << "\n";
    if (deriv_submatrix_index != 0) {
      os << nnet.GetNodeName(node_index) << ".deriv -> "
         << submatrix_strings[deriv_submatrix_index] << "\n";
    }    
  }
  if (!matrix_debug_info.empty()) {
    KALDI_ASSERT(matrix_debug_info.size() == matrices.size());
    for (int32 i = 1; i < matrices.size(); i++) {
      const MatrixDebugInfo &debug_info = matrix_debug_info[i];
      if (debug_info.node_index == -1)  // was not set up for some reason.
        continue;
      KALDI_ASSERT(static_cast<size_t>(debug_info.node_index) < nnet.NumNodes());
      os << "m" << i << " = " << nnet.GetNodeName(debug_info.node_index)
         << "." << (debug_info.is_deriv ? "deriv" : "value");
      // PrintIndexes will print the indexes inside [ ] brackets.
      PrintIndexes(os, debug_info.indexes);
      os << "\n";
    }
  }
  os << "# begin forward commands\n";
  for (int32 c = 0; c < commands.size(); c++) {
    if (c == forward_computation_end)
      os << "# begin backward commands\n";
    PrintCommand(os, nnet, *this, c, submatrix_strings,
                 indexes_strings, indexes_multi_strings);
  }
}

bool NnetComputation::IsWholeMatrix(int32 submatrix_index) const {
  KALDI_ASSERT(submatrix_index > 0 && submatrix_index < sub_matrices.size());
  const SubMatrixInfo &submat_info = sub_matrices[submatrix_index];
  const MatrixInfo &mat_info = matrices[submat_info.matrix_index];
  return submat_info.row_offset == 0 && submat_info.col_offset == 0 &&
      submat_info.num_rows == mat_info.num_rows &&
      submat_info.num_cols == mat_info.num_cols;
}

} // namespace nnet3
} // namespace kaldi
