#include "graph_stats.h"
#include "state.h"
#include "graph.h"

// Standard headers
#include <limits>
#include <set>
#include <queue>
#include <vector>
#include <cassert>

namespace {

class GetGraphHeight
{
public:
  GetGraphHeight()
    : _heights()
  {}

private:
  typedef map<Node*, size_t> Heights;
  Heights _heights;

public:
  void Reset() {
    _heights.clear();
  }

  size_t operator()(const vector<Node*>& root_nodes) {
    Reset();
    return GetMaxHeight(root_nodes);
  }

private:
  size_t operator()(Node* node) {
    size_t& height = _heights[node];
    if (height == 0) {
      if (Edge* e = node->in_edge()) {
        height = GetMaxHeight(e->inputs_) + 1;
      } else {
        height = 1;
      }
    }
    return height;
  }

  size_t GetMaxHeight(const vector<Node*>& nodes) {
    size_t height = numeric_limits<size_t>::min();
    for (vector<Node*>::const_iterator i = nodes.begin();
         i != nodes.end(); ++i) {
      height = max(height, (*this)(*i));
    }
    return height;
  }

}; // class GetGraphHeight

class GetGraphWidth
{
public:
  GetGraphWidth()
    : _q()
    , _s()
  {}

  void Reset() {
    _s.clear();
  }

  size_t operator()(const vector<Node*>& root_nodes) {
    Reset();
    assert(_q.empty());
    _PushNode(root_nodes);
    size_t w = _q.size();
    while (!_q.empty()) {
      Node* n = _q.front();
      _q.pop();
      if (Edge* e = n->in_edge()) {
        _PushNode(e->inputs_);
        w = max(w, _q.size());
      }
    }
    return w;
  }

private:
  void _PushNode(const vector<Node*>& nodes) {
    for (vector<Node*>::const_iterator i = nodes.begin();
         i != nodes.end(); ++i) {
      pair<set<Node*>::iterator, bool> ret = _s.insert(*i);
      if (ret.second)
        _q.push(*i);
    }
  }

  queue<Node*> _q;
  set<Node*> _s;
}; // class GetGraphWidth

} // anonymous namespace

void GraphStats::Reset() {
  nnodes = 0;
  nsources = 0;
  noutputs = 0;
  nintermed = 0;
  nedges = 0;
  max_edge_output = 0;
  min_edge_output = 0;
  max_edge_input = 0;
  min_edge_input = 0;
  nphony_edges = 0;
}

void GetGraphStats(const State& state, GraphStats* stats) {
  stats->Reset();
  set<Node*> nodes;
  ////// Computes edges statitics.
  stats->min_edge_input = numeric_limits<size_t>::max();
  stats->min_edge_output = numeric_limits<size_t>::max();
  for (vector<Edge*>::const_iterator e = state.edges_.begin();
       e != state.edges_.end(); ++e) {
    stats->max_edge_output = max(stats->max_edge_output,
                                 (*e)->outputs_.size());
    stats->min_edge_output = min(stats->min_edge_output,
                                 (*e)->outputs_.size());
    stats->max_edge_input = max(stats->max_edge_input,
                                (*e)->inputs_.size());
    stats->min_edge_input = min(stats->min_edge_input,
                                (*e)->inputs_.size());
    for (vector<Node*>::const_iterator out_node = (*e)->outputs_.begin();
         out_node != (*e)->outputs_.end(); ++out_node)
      nodes.insert(*out_node);
    for (vector<Node*>::const_iterator in_node = (*e)->inputs_.begin();
         in_node != (*e)->inputs_.end(); ++in_node)
      nodes.insert(*in_node);
    if ((*e)->is_phony())
      ++stats->nphony_edges;
  }
  stats->nedges = state.edges_.size();
  stats->nnodes = nodes.size();
  ////// Computes nodes statitics.
  vector<Node*> root_nodes;
  for (set<Node*>::const_iterator n = nodes.begin(); n != nodes.end(); ++n) {
    if ((*n)->in_edge()) {
      if ((*n)->out_edges().empty()) {
        ++stats->noutputs;
        root_nodes.push_back(*n);
      } else {
        ++stats->nintermed;
      }
    } else {
      ++stats->nsources;
    }
  }
  assert(stats->nsources + stats->nintermed + stats->noutputs == stats->nnodes);
  stats->width = GetGraphWidth()(root_nodes);
  stats->height = GetGraphHeight()(root_nodes);
}
