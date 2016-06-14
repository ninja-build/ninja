#ifndef NINJA_GRAPH_STATS_H_
#define NINJA_GRAPH_STATS_H_

#include <cstddef>

using namespace std;

// Forward declarations
struct State;

struct GraphStats
{
  GraphStats()
    : nnodes(0)
    , nsources(0)
    , noutputs(0)
    , nintermed(0)
    , nedges(0)
    , max_edge_output(0)
    , min_edge_output(0)
    , max_edge_input(0)
    , min_edge_input(0)
    , nphony_edges(0)
    , width(0)
    , height(0)
  {}

  void Reset();

  size_t nnodes; /// Number of unique nodes
  size_t nsources; /// Files that is not produced by any edge.
  size_t noutputs; /// Target that is not the input of any edge.
  size_t nintermed; /// Intermediary targets (neither source nor output)
  size_t nedges; /// Total number of edges.
  size_t max_edge_output;
  size_t min_edge_output;
  size_t max_edge_input;
  size_t min_edge_input;
  size_t nphony_edges;
  /// Maximum size of the queue of a breadth first traversal of the graph.
  size_t width;
  /// Maximum size of the stack of a depth first traversal of the graph.
  size_t height;

  double sources_ratio() const
  { return static_cast<double>(nsources) / nnodes; }
  double outputs_ratio() const
  { return static_cast<double>(noutputs) / nnodes; }
  double intermed_ratio() const
  { return static_cast<double>(nintermed) / nnodes; }
}; // struct GraphStats

void GetGraphStats(const State& state, GraphStats* stats);

#endif /* !NINJA_GRAPH_STATS_H_ */
