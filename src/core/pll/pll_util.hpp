#pragma once

#include "core/pll/pllhead.hpp"
#include "core/pll/rtree_mapper.hpp"
#include "util/Range.hpp"

#include <sstream>
#include <string>
#include <tuple>

typedef struct
{
  int clv_valid;
} node_info_t;

// deleter typedefs
using partition_deleter = void ( * )( pll_partition_t* );
using utree_deleter     = void ( * )( pll_utree_t* );
using fasta_deleter     = void ( * )( pll_fasta_t* );

// deleters
void fasta_close( pll_fasta_t* fptr );
void utree_destroy( pll_utree_t* tree );
int utree_free_node_data( pll_unode_t* node );

// traversal callbacks
int cb_partial_traversal( pll_unode_t* node );
int cb_full_traversal( pll_unode_t* node );

// recursive traversal functions
unsigned int utree_query_branches( pll_utree_t const* const node,
                                   pll_unode_t** node_list );
void set_unique_clv_indices( pll_unode_t* const tree,
                             int const num_tip_nodes );
void set_missing_branch_lengths( pll_utree_t* tree,
                                 double const length );
void set_branch_lengths( pll_utree_t* tree,
                         double const length );
double sum_branch_lengths( pll_utree_t const* const tree );

// tiny tree specific
void reset_triplet_lengths( pll_unode_t* toward_pendant,
                            pll_partition_t* partition,
                            double const old_length );

// general helpers
std::string get_numbered_newick_string( pll_utree_t const* const root,
                                        rtree_mapper const& mapper,
                                        size_t precision = 10 );
pll_unode_t* get_tip_node( pll_unode_t* node );

pll_unode_t* get_root( pll_utree_t const* const tree );

pll_utree_t* make_utree_struct( pll_unode_t* root, unsigned int const num_nodes );

// deprecated
void shift_partition_focus( pll_partition_t* partition, int const offset, unsigned int const span );

// templates
template< typename Func, typename... Args >
double call_focused( Func func, Range& range, pll_partition_t* partition, Args&&... args )
{
  auto const num_sites = partition->sites;
  // Shift there...
  shift_partition_focus( partition, range.begin, range.span );

  double ret = func( partition, args... );

  // ... and shift back again
  shift_partition_focus( partition, -range.begin, num_sites );

  return ret;
}
