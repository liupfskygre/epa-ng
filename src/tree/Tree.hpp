#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/pll/pll_util.hpp"
#include "core/pll/pllhead.hpp"
#include "core/pll/rtree_mapper.hpp"
#include "core/raxml/Model.hpp"
#include "io/Binary.hpp"
#include "seq/MSA.hpp"
#include "tree/Tree_Numbers.hpp"
#include "util/Options.hpp"

/* Encapsulates the pll data structures for ML computation */
class Tree {
  public:
  using Scoped_Mutex  = std::lock_guard< std::mutex >;
  using Mutex_List    = std::vector< std::mutex >;
  using partition_ptr = std::unique_ptr< pll_partition_t, partition_deleter >;
  using utree_ptr     = std::unique_ptr< pll_utree_t, utree_deleter >;

  Tree( std::string const& tree_file,
        MSA const& msa,
        raxml::Model& model,
        Options const& options );
  Tree( std::string const& bin_file,
        raxml::Model& model,
        Options const& options );
  Tree()  = default;
  ~Tree() = default;

  Tree( Tree const& other ) = delete;
  Tree( Tree&& other )      = default;

  Tree& operator=( Tree const& other ) = delete;
  Tree& operator=( Tree&& other ) = default;

  // member access
  Tree_Numbers& nums() { return nums_; }
  raxml::Model& model() { return model_; }
  Options& options() { return options_; }
  auto partition() { return partition_.get(); }
  auto tree() { return tree_.get(); }
  rtree_mapper& mapper() { return mapper_; }

  void* get_clv( pll_unode_t const* const );

  double ref_tree_logl();

  private:
  // pll structures

  partition_ptr partition_{ nullptr, pll_partition_destroy };
  utree_ptr tree_{ nullptr, utree_destroy }; // must be top level node as parsed in newick! (for jplace)

  // tree related numbers
  Tree_Numbers nums_;

  // epa related classes
  MSA ref_msa_;
  raxml::Model model_;
  Options options_;
  Binary binary_;
  rtree_mapper mapper_;

  // thread safety
  Mutex_List locks_;
};
