#include "optimize.hpp"

#include <vector>
#include <cmath>
#include <cstdlib>
#include <cassert>
#include <stdexcept>
#include <limits>
#include <algorithm>

#include "pll_util.hpp"
#include "constants.hpp"
#include "Log.hpp"

using namespace std;

static void traverse_update_partials(pll_utree_t * tree, pll_partition_t * partition,
    pll_utree_t ** travbuffer, double * branch_lengths, unsigned int * matrix_indices,
    pll_operation_t * operations)
{
  unsigned int num_matrices, num_ops;
  vector<unsigned int> param_indices(partition->rate_cats, 0);
  /* perform a full traversal*/
  assert(tree->next != nullptr);
  unsigned int traversal_size;
  // TODO this only needs to be done once, outside of this func. pass traversal size also
  // however this is practically nonexistent impact compared to clv comp
  pll_utree_traverse(tree, cb_full_traversal, travbuffer, &traversal_size);

  /* given the computed traversal descriptor, generate the operations
     structure, and the corresponding probability matrix indices that
     may need recomputing */
  pll_utree_create_operations(travbuffer,
                              traversal_size,
                              branch_lengths,
                              matrix_indices,
                              operations,
                              &num_matrices,
                              &num_ops);

  pll_update_prob_matrices(partition,
                           &param_indices[0],
                           matrix_indices,// matrices to update
                           branch_lengths,
                           num_matrices); // how many should be updated

  /* use the operations array to compute all num_ops inner CLVs. Operations
     will be carried out sequentially starting from operation 0 towrds num_ops-1 */
  pll_update_partials(partition, operations, num_ops);

}

static void utree_derivative_func (void * parameters, double proposal,
                                     double *df, double *ddf)
{
  pll_newton_tree_params_t * params = (pll_newton_tree_params_t *) parameters;
  pll_compute_likelihood_derivatives (
      params->partition,
      params->tree->scaler_index,
      params->tree->back->scaler_index,
      proposal,
      params->params_indices,
      params->sumtable, df, ddf);
}

/**
 * Branch length optimization akin to how pplacer does it: slide the pendant branch along
 * insertion branch of the reference tree, optimize pendant length fully.
 * 
 * @param  partition  the partition
 * @param  tree       the tree structure
 * @param  smoothings maximum number of iterations
 * @return            negative log likelihood after optimization
 */
static double opt_branch_lengths_pplacer(pll_partition_t * partition, pll_utree_t * tree, unsigned int smoothings, const unsigned int tolerance)
{
  double loglikelihood = 0.0, new_loglikelihood;
  double xmin,    /* min branch length */
         xguess,  /* initial guess */
         xmax,    /* max branch length */
         xtol,    /* tolerance */
         xres;    /* optimal found branch length */

  const auto score_node = tree;
  const auto blo_node = tree->next->back;
  const auto blo_antinode = tree->next->next->back;

  const auto original_length = blo_node->length * 2;
  vector<unsigned int> param_indices(partition->rate_cats, 0);

  
  /* get the initial likelihood score */
  loglikelihood = -pll_compute_edge_loglikelihood (partition,
                                                  score_node->back->clv_index,
                                                  score_node->back->scaler_index,
                                                  score_node->clv_index,
                                                  score_node->scaler_index,
                                                  score_node->pmatrix_index,
                                                  &param_indices[0],
                                                  nullptr);

  /* set parameters for N-R optimization */
  pll_newton_tree_params_t nr_params;
  nr_params.partition         = partition;
  // nr_params.tree              = score_node;
  nr_params.params_indices    = &param_indices[0];
  // nr_params.branch_length_min = PLLMOD_OPT_MIN_BRANCH_LEN;
  // nr_params.branch_length_max = PLLMOD_OPT_MAX_BRANCH_LEN;
  // nr_params.tolerance         = tolerance;
  nr_params.sumtable          = nullptr;

  pll_operation_t toward_score;
  toward_score.parent_clv_index = score_node->clv_index;
  toward_score.parent_scaler_index = score_node->scaler_index;
  toward_score.child1_clv_index = blo_node->clv_index;
  toward_score.child1_scaler_index = blo_node->scaler_index;
  toward_score.child1_matrix_index = blo_node->pmatrix_index;
  toward_score.child2_clv_index = blo_antinode->clv_index;
  toward_score.child2_scaler_index = blo_antinode->scaler_index;
  toward_score.child2_matrix_index = blo_antinode->pmatrix_index;

  pll_operation_t toward_blo_node;
  toward_blo_node.parent_clv_index = blo_node->back->clv_index;
  toward_blo_node.parent_scaler_index = blo_node->back->scaler_index;
  toward_blo_node.child1_clv_index = score_node->back->clv_index;
  toward_blo_node.child1_scaler_index = score_node->back->scaler_index;
  toward_blo_node.child1_matrix_index = score_node->pmatrix_index;
  toward_blo_node.child2_clv_index = blo_antinode->clv_index;
  toward_blo_node.child2_scaler_index = blo_antinode->scaler_index;
  toward_blo_node.child2_matrix_index = blo_antinode->pmatrix_index;

  /* allocate the sumtable */
  auto sites_alloc = partition->sites;
  if (partition->attributes & PLL_ATTRIB_AB_FLAG)
    sites_alloc += partition->states;

  if ((nr_params.sumtable = (double *) pll_aligned_alloc(
       sites_alloc * partition->rate_cats * partition->states_padded *
       sizeof(double), partition->alignment)) == nullptr)
  {
    throw runtime_error{"Cannot allocate memory for bl opt variables"};
  }

  double lengths[3] = {blo_node->length, blo_antinode->length, score_node->length};
  uint p_indices[3] = {blo_node->pmatrix_index, blo_antinode->pmatrix_index, score_node->pmatrix_index};

  while (smoothings)
  {
    auto old_blonode_length = blo_node->length;
    auto old_pendant_length = score_node->length;

    /*=============================================================
            NR for Pendant
     =============================================================*/

    xmin = PLLMOD_OPT_MIN_BRANCH_LEN;
    xmax = PLLMOD_OPT_MAX_BRANCH_LEN;
    xtol = xmin/10.0;
    xguess = score_node->length;
    if (xguess < xmin || xguess > xmax)
      xguess = PLLMOD_OPT_DEFAULT_BRANCH_LEN;

    /* prepare sumtable for current branch */
    pll_update_sumtable(partition,
                        score_node->clv_index,
                        score_node->back->clv_index,
                        &param_indices[0],
                        nr_params.sumtable);

    nr_params.tree              = score_node;
    nr_params.branch_length_min = xmin;
    nr_params.branch_length_max = xmax;
    nr_params.tolerance         = xtol;

    // minimize newton for pendant length
    xres = pllmod_opt_minimize_newton(xmin, xguess, xmax, xtol,
                                10, &nr_params,
                                utree_derivative_func);
    assert(xres >= 0.0);

    // update length and pmatrix for pendant
    lengths[2] = score_node->length = score_node->back->length = xres;
    pll_update_prob_matrices(partition, &param_indices[0], &p_indices[2], &lengths[2], 1);

    /*=============================================================
            NR for Proximal
     =============================================================*/

    // calculate partial toward blo node (proximal)
    pll_update_partials(partition, &toward_blo_node, 1);

    /* set N-R parameters */
    xmin = PLLMOD_OPT_MIN_BRANCH_LEN;
    xmax = original_length;
    xtol = xmin/10.0;
    xguess = blo_node->length;
    if (xguess < xmin || xguess > xmax)
      xguess = original_length / 2.0;

    /* prepare sumtable for current branch */
    pll_update_sumtable(partition,
                        blo_node->clv_index,
                        blo_node->back->clv_index,
                        &param_indices[0],
                        nr_params.sumtable);

    nr_params.tree              = blo_node;
    nr_params.branch_length_min = xmin;
    nr_params.branch_length_max = xmax;
    nr_params.tolerance         = xtol;

    // minimize newton for pendant length
    xres = pllmod_opt_minimize_newton(xmin, xguess, xmax, xtol,
                                10, &nr_params,
                                utree_derivative_func);
    assert(xres >= 0.0);

    // update lengths and pmatrices for proximal/distal
    lengths[0] = blo_node->length     = blo_node->back->length      = xres;
    lengths[1] = blo_antinode->length = blo_antinode->back->length  = original_length - xres;
    pll_update_prob_matrices(partition, &param_indices[0], p_indices, lengths, 2);

    /*=============================================================
            Calculate the score
     =============================================================*/

    pll_update_partials(partition, &toward_score, 1);

    new_loglikelihood = -pll_compute_edge_loglikelihood(partition,
                                        score_node->back->clv_index,
                                        score_node->back->scaler_index,
                                        score_node->clv_index,
                                        score_node->scaler_index,
                                        score_node->pmatrix_index,
                                        &param_indices[0],
                                        nullptr);

    
    if(new_loglikelihood - loglikelihood > new_loglikelihood * 1e-14)
    {
      // the NR procedure returned a worse logl than the previous iteration
      // thus we reset the branch lengths to the previous values and return
      // reset lengths
      score_node->length = old_pendant_length;
      score_node->back->length = old_pendant_length;
      blo_node->length = old_blonode_length;
      blo_node->back->length = old_blonode_length;
      blo_antinode->length = original_length - old_blonode_length;
      blo_antinode->back->length = blo_antinode->length;
      break;
    }

    --smoothings;

    /* check convergence */
    if (fabs (new_loglikelihood - loglikelihood) < tolerance)
      smoothings = 0;

    loglikelihood = new_loglikelihood;

  }

  

  /* deallocate sumtable */
  pll_aligned_free(nr_params.sumtable);

  return loglikelihood;
}

double optimize_branch_triplet(pll_partition_t * partition, pll_utree_t * tree, bool sliding)
{
  if (!tree->next)
    tree = tree->back;

  vector<pll_utree_t*> travbuffer(4);
  vector<double> branch_lengths(3);
  vector<unsigned int> matrix_indices(3);
  vector<pll_operation_t> operations(4);

  traverse_update_partials(tree, partition, &travbuffer[0], &branch_lengths[0],
    &matrix_indices[0], &operations[0]);

  vector<unsigned int> param_indices(partition->rate_cats, 0);

  auto cur_logl = -numeric_limits<double>::infinity();
  int smoothings = 32;

  if (sliding)
    cur_logl = -opt_branch_lengths_pplacer(partition, tree, smoothings, OPT_BRANCH_EPSILON);
  else
    cur_logl = -pllmod_opt_optimize_branch_lengths_local (
                                                partition,
                                                tree,
                                                &param_indices[0],
                                                PLLMOD_OPT_MIN_BRANCH_LEN,
                                                PLLMOD_OPT_MAX_BRANCH_LEN,
                                                OPT_BRANCH_EPSILON,
                                                smoothings,
                                                1, // radius
                                                1); // keep update

 
  return cur_logl;
}

static double optimize_branch_lengths(pll_utree_t * tree, pll_partition_t * partition, pll_optimize_options_t& params,
  pll_utree_t ** travbuffer, double cur_logl, double lnl_monitor, int* smoothings)
{
  if (!tree->next)
    tree = tree->back;

  traverse_update_partials(tree, partition, travbuffer, params.lk_params.branch_lengths,
    params.lk_params.matrix_indices, params.lk_params.operations);

  pll_errno = 0; // hotfix

  vector<unsigned int> param_indices(partition->rate_cats, 0);

  cur_logl = -1 * pllmod_opt_optimize_branch_lengths_iterative(
    partition,
    tree,
    &param_indices[0],
    PLLMOD_OPT_MIN_BRANCH_LEN,
    PLLMOD_OPT_MAX_BRANCH_LEN,
    OPT_BRANCH_EPSILON,
    *smoothings,
    1); // keep updating BLs during call

  if (cur_logl+1e-6 < lnl_monitor)
    throw runtime_error{string("cur_logl < lnl_monitor: ") + to_string(cur_logl) + string(" : ")
    + to_string(lnl_monitor)};

  // reupdate the indices as they may have changed
  params.lk_params.where.unrooted_t.parent_clv_index = tree->clv_index;
  params.lk_params.where.unrooted_t.parent_scaler_index = tree->scaler_index;
  params.lk_params.where.unrooted_t.child_clv_index = tree->back->clv_index;
  params.lk_params.where.unrooted_t.child_scaler_index = tree->back->scaler_index;
  params.lk_params.where.unrooted_t.edge_pmatrix_index = tree->pmatrix_index;

  traverse_update_partials(tree, partition, travbuffer, params.lk_params.branch_lengths,
    params.lk_params.matrix_indices, params.lk_params.operations);
    cur_logl = pll_compute_edge_loglikelihood (partition, tree->clv_index,
                                                  tree->scaler_index,
                                                  tree->back->clv_index,
                                                  tree->back->scaler_index,
                                                  tree->pmatrix_index, &param_indices[0], nullptr);

  return cur_logl;
}

void optimize(Model& model, pll_utree_t * tree, pll_partition_t * partition,
  const Tree_Numbers& nums, const bool opt_branches, const bool opt_model)
{
  if (!opt_branches && !opt_model)
    return;

  if (opt_branches)
    set_branch_lengths(tree, DEFAULT_BRANCH_LENGTH);

  compute_and_set_empirical_frequencies(partition, model);

  auto symmetries = (&(model.symmetries())[0]);
  vector<unsigned int> param_indices(model.rate_cats(), 0);

  // sadly we explicitly need these buffers here and in the params structure
  vector<pll_utree_t*> travbuffer(nums.nodes);
  vector<double> branch_lengths(nums.branches);
  vector<unsigned int> matrix_indices(nums.branches);
  vector<pll_operation_t> operations(nums.nodes);

  traverse_update_partials(tree, partition, &travbuffer[0], &branch_lengths[0],
    &matrix_indices[0], &operations[0]);

  // compute logl once to give us a logl starting point
  auto cur_logl = pll_compute_edge_loglikelihood (partition, tree->clv_index,
                                                tree->scaler_index,
                                                tree->back->clv_index,
                                                tree->back->scaler_index,
                                                tree->pmatrix_index, &param_indices[0], nullptr);

  // double cur_logl = -numeric_limits<double>::infinity();
  int smoothings;
  double lnl_monitor = cur_logl;

  // set up high level options structure
  pll_optimize_options_t params;
  params.lk_params.partition = partition;
  params.lk_params.operations = &operations[0];
  params.lk_params.branch_lengths = &branch_lengths[0];
  params.lk_params.matrix_indices = &matrix_indices[0];
  params.lk_params.params_indices = &param_indices[0];
  params.lk_params.alpha_value = model.alpha();
  params.lk_params.rooted = 0;
  params.lk_params.where.unrooted_t.parent_clv_index = tree->clv_index;
  params.lk_params.where.unrooted_t.parent_scaler_index = tree->scaler_index;
  params.lk_params.where.unrooted_t.child_clv_index = tree->back->clv_index;
  params.lk_params.where.unrooted_t.child_scaler_index =
    tree->back->scaler_index;
  params.lk_params.where.unrooted_t.edge_pmatrix_index = tree->pmatrix_index;

  /* optimization parameters */
  params.params_index = 0;
  params.subst_params_symmetries = symmetries;
  params.factr = OPT_FACTR;
  params.pgtol = OPT_PARAM_EPSILON;

  vector<pll_utree_t*> branches(nums.branches);
  auto num_traversed = utree_query_branches(tree, &branches[0]);
  assert (num_traversed == nums.branches);
  unsigned int branch_index = 0;

  double logl = cur_logl;

  if (opt_branches)
  {
    smoothings = 8;
    cur_logl = optimize_branch_lengths(branches[branch_index],
      partition, params, &travbuffer[0], cur_logl, lnl_monitor, &smoothings);

    // lgr << "after hidden blo crouching tiger: " << to_string(cur_logl) << "\n";

  }

  const size_t rates_size = model.substitution_rates().size();

  std::vector<double> min_rates(rates_size, OPT_RATE_MIN);
  std::vector<double> max_rates(rates_size, OPT_RATE_MAX);

  do
  {
    branch_index = rand () % num_traversed;

    // lgr << "Start: " << to_string(cur_logl) << "\n";
    logl = cur_logl;

    if (opt_model)
    {

      params.which_parameters = PLLMOD_OPT_PARAM_SUBST_RATES;
      cur_logl = -pllmod_opt_optimize_multidim(&params, &min_rates[0], &max_rates[0]);

      // lgr << "after rates: " << to_string(cur_logl) << "\n";

      if (opt_branches)
      {
        smoothings = 2;
        cur_logl = optimize_branch_lengths(branches[branch_index],
          partition, params, &travbuffer[0], cur_logl, lnl_monitor, &smoothings);

        // lgr << "after blo 1: " << to_string(cur_logl) << "\n";

      }

      // params.which_parameters = PLL_PARAMETER_FREQUENCIES;
      // pll_optimize_parameters_multidim(&params, nullptr, nullptr);

      if (opt_branches)
      {
        smoothings = 2;
        cur_logl = optimize_branch_lengths(branches[branch_index],
          partition, params, &travbuffer[0], cur_logl, lnl_monitor, &smoothings);

        // lgr << "after blo 2: " << to_string(cur_logl) << "\n";

      }

      // params.which_parameters = PLL_PARAMETER_PINV;
      // cur_logl = -1 * pll_optimize_parameters_brent(&params);
      params.which_parameters = PLLMOD_OPT_PARAM_ALPHA;
      cur_logl = -pllmod_opt_optimize_onedim(&params, 0.02, 10000.);

      // lgr << "after alpha: " << to_string(cur_logl) << "\n";

    }

    if (opt_branches)
    {
      smoothings = 3;
      cur_logl = optimize_branch_lengths(branches[branch_index],
        partition, params, &travbuffer[0], cur_logl, lnl_monitor, &smoothings);

      // lgr << "after blo 3: " << to_string(cur_logl) << "\n";

    }
  } while (fabs (cur_logl - logl) > OPT_EPSILON);

  if (opt_model)
  {
    // update epa model object as well
    model.alpha(params.lk_params.alpha_value);
    model.substitution_rates(partition->subst_params[0], rates_size);
    model.base_frequencies(partition->frequencies[params.params_index], partition->states);
  }
}

void compute_and_set_empirical_frequencies(pll_partition_t * partition, Model& model)
{
  double * empirical_freqs = pllmod_msa_empirical_frequencies (partition);

  pll_set_frequencies (partition, 0, empirical_freqs);
  model.base_frequencies(partition->frequencies[0], partition->states);
  free (empirical_freqs);
}
