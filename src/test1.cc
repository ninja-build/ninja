#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include "graph.h"
#include "manifest_parser.h"
#include "state.h"
#include "util.h"
#include "version.h"

using namespace std;

void CreateHelloWorldGraph(State* state) {
  // Create a rule
  Rule* compile_rule = new Rule("CXX_EXECUTABLE_LINKER__hello_world_");
  compile_rule->AddBinding("command", "g++ $in -o $out");
  state->AddRule(compile_rule);

  // Create nodes
  Node* source_file = state->GetNode("hello_world.cpp", 0);
  Node* output_file = state->GetNode("hello_world", 0);

  // Create an edge
  Edge* edge = state->AddEdge(compile_rule);
  edge->inputs_.push_back(source_file);
  edge->outputs_.push_back(output_file);

  // Connect nodes to the edge
  source_file->AddOutEdge(edge);
  output_file->AddInEdge(edge);
}