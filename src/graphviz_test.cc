// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "graphviz.h"
#include "build.h"
#include "test.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>


// output operator for failed googletests
std::ostream& operator<<(std::ostream& os, const Node* data) {
  return os << data->path();
}

std::ostream& operator<<(std::ostream& os, const std::set<const Node*>& data) {
  os << "{ ";
  for (auto i : data) {
    if (i == nullptr) {
      os << "null ";
    } else {
      os << i << " ";
    }
  }
  return os << " }";
}

namespace {

std::set<const Node*> flattenNodesCycle(const exportLinks& data) {
  std::set<const Node*> nodes;
  for (auto& i : data) {
    for (const nodeAttribute& o : i.second.set_) {
      if (o.cyclic_)
        nodes.insert(o.node);
    }
  }
  return nodes;
}

std::set<const Node*> flattenNodes(const exportLinks& data) {
  std::set<const Node*> nodes;
  for (auto& i : data) {
    for (const nodeAttribute& o : i.second.set_) {
      nodes.insert(o.node);
    }
  }
  return nodes;
}

std::size_t linkNumber(const exportLinks& data) {
  std::size_t size(0);
  for (auto& i : data) {
    size += i.second.set_.size();
  }
  return size;
}

std::size_t linkNumberCycle(const exportLinks& data) {
  std::size_t size(0);
  for (auto& i : data) {
    for (const nodeAttribute& o : i.second.set_) {
      if (o.cyclic_)
        ++size;
    }
  }
  return size;
}

struct groupFactory {
  graph::Options operator()(std::initializer_list<const Node*> Targets) const {
    graph::Group mygroup;
    for (auto i : Targets) {
      mygroup.targets_.insert(i);
    }
    mygroup.options_ = opt_;
    graph::Options opt;
    opt.Groups_.push_back(mygroup);
    return opt;
  }

  graph::Option opt_;
};

/// make the protected members available
struct GraphVizUUT : public GraphViz {
  GraphVizUUT(const graph::Options& gr) : GraphViz(gr) {}
  using GraphViz::getLinks;
  using GraphViz::GraphViz;
  using GraphViz::printDot;
};

}  // namespace

struct GraphvizTest : public StateTestWithBuiltinRules {
  GraphvizTest() : scan_(&state_, NULL, NULL, &fs_, NULL, NULL) {}

  std::set<const Node*> GetLookupNodes(
      std::initializer_list<std::string> names);

  template <class T>
  void check(const T& opt, std::initializer_list<const Node*> Targets,
             std::initializer_list<std::string> names, int Nr = -1);
  template <class T>
  void checkCycle(const T& opt, std::initializer_list<const Node*> Targets,
              std::initializer_list<std::string> names, int Nr = -1);

  VirtualFileSystem fs_;
  DependencyScan scan_;
};

std::set<const Node*> GraphvizTest::GetLookupNodes(
    std::initializer_list<std::string> names) {
  std::set<const Node*> ret;
  for (auto i : names) {
    auto temp = GetLookupNode(i);
    EXPECT_NE(temp, nullptr) << "node " << i << " does not exist";
    ret.insert(temp);
  }
  return ret;
}

template <class T>
void GraphvizTest::check(const T& opt,
                         std::initializer_list<const Node*> Targets,
                         std::initializer_list<std::string> names, int Nr) {
  GraphVizUUT toTest(opt(Targets));
  const exportLinks links = toTest.getLinks();
  EXPECT_EQ(flattenNodes(links), GetLookupNodes(names));
  if (Nr >= 0) {
    EXPECT_EQ(linkNumber(links), static_cast<std::size_t>(Nr));
  }
}

template <class T>
void GraphvizTest::checkCycle(const T& opt, std::initializer_list<const Node*> Targets,
                            std::initializer_list<std::string> names, int Nr) {
  GraphVizUUT test(opt(Targets));
  const exportLinks links = test.getLinks();
  EXPECT_EQ(flattenNodesCycle(links), GetLookupNodes(names));
   if (Nr >= 0) {
    EXPECT_EQ(linkNumberCycle(links), static_cast<std::size_t>(Nr));
  }
}

/// class will add a scope for googletest. Prints the file and line number in
/// case of failure
struct ScopeTestGraphVizBase {
  ScopeTestGraphVizBase(const char* file, int line, GraphvizTest* c)
      : file_(file), line_(line), GraphvizTest_(c) {}

  const char* file_;
  const int line_;
  GraphvizTest* GraphvizTest_;
};

struct ScopeTestGraphVizCycle : public ScopeTestGraphVizBase {
  ScopeTestGraphVizCycle(const char* file, int line, GraphvizTest* c)
      : ScopeTestGraphVizBase(file, line, c) {}

  template <class T>
  const ScopeTestGraphVizCycle& operator()(
      T opt, std::initializer_list<const Node*> Targets,
      std::initializer_list<std::string> names, int Nr = -1) const {
    std::string scope("Input Nodes: ");
    for (auto i : Targets) {
      scope += i->path() + " ";
    }
    SCOPED_TRACE(::testing::Message()
                 << " called from " << file_ << ":" << line_ << "\n"
                 << scope);
    GraphvizTest_->checkCycle(opt, Targets, names, Nr);
    return *this;
  }
};

struct ScopeTestGraphViz : public ScopeTestGraphVizBase {
  ScopeTestGraphViz(const char* file, int line, GraphvizTest* c)
      : ScopeTestGraphVizBase(file, line, c) {}

  template <class T>
  const ScopeTestGraphViz& operator()(
      T opt, std::initializer_list<const Node*> Targets,
      std::initializer_list<std::string> names, int Nr = -1) const {
    std::string scope("Input Nodes: ");
    for (auto i : Targets) {
      scope += i->path() + " ";
    }
    SCOPED_TRACE(::testing::Message()
                 << " called from " << file_ << ":" << line_ << "\n"
                 << scope);
    GraphvizTest_->check(opt, Targets, names, Nr);
    return *this;
  }
};

#define TEST_VIZ ScopeTestGraphViz(__FILE__, __LINE__, this)
#define TEST_VIZ_CYCLE ScopeTestGraphVizCycle(__FILE__, __LINE__, this)
#define DEFINE_VARIABLE(a) Node* a = GetNode(#a)

TEST_F(GraphvizTest, Basic) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "build A B: cat in\n"
    "build outB: cat B\n"
    "build outA: cat A\n"
    "build out: cat outB outA\n"
    "build out2: cat out\n"));

  DEFINE_VARIABLE(outB);
  DEFINE_VARIABLE(outA);
  DEFINE_VARIABLE(out2);
  DEFINE_VARIABLE(out);
  DEFINE_VARIABLE(in);

  std::set<Node*> nodes{ outB };
  DepLoader::Load(&state_, &fs_, nullptr, nodes, true,
                                      false);

  // ninja -t graph Target
  auto plain = groupFactory();
  // ninja -t graph -s Target
  auto sib = groupFactory();
  sib.opt_.input_siblings_ = false;


  TEST_VIZ(plain, { out2 }, { "out", "outB", "outA", "A", "B", "in", "out2" }, 12);
  TEST_VIZ(plain, { out },  { "out", "outB", "outA", "A", "B", "in" }, 10);
  TEST_VIZ(plain, { outB }, { "outB", "A", "B", "in" }, 5);
  TEST_VIZ(plain, { outA }, { "outA", "A", "B", "in" }, 5);
  TEST_VIZ(plain, { outA, outB }, { "outA", "outB", "A", "B", "in" }, 7);

  TEST_VIZ(sib, { outA }, { "outA", "A", "in" }, 4);
  TEST_VIZ(sib, { outB }, { "outB", "B", "in" }, 4);
  TEST_VIZ(sib, { out2 }, { "out", "outB", "outA", "A", "B", "in", "out2" }, 12);

  auto sibR = sib;
  sibR.opt_.reverse_ = true;
  auto plainR = plain;
  plainR.opt_.reverse_ = true;

  TEST_VIZ(plainR, { in }, { "out", "outB", "outA", "A", "B", "in", "out2" }, 12);
  TEST_VIZ(sibR  , { in }, { "out", "outB", "outA", "A", "B", "in", "out2" }, 12);
}

TEST_F(GraphvizTest, RelationsOfTargets) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
     "build A B: cat in\n"
     "build outB: cat B\n"
     "build outA: cat A\n"
     "build out: cat outB outA\n"
     "build outNo: cat out\n"));

  DEFINE_VARIABLE(A);
  DEFINE_VARIABLE(B);
  DEFINE_VARIABLE(in);
  DEFINE_VARIABLE(outB);
  DEFINE_VARIABLE(outNo);

  // ninja -t graph -s Target
  auto rel = groupFactory();
  rel.opt_.relations_ = true;

  std::set<Node*> nodes{ outNo, A };
  DepLoader::Load(&state_, &fs_, nullptr, nodes, true, false);

  TEST_VIZ(rel, { outNo, in },
           { "A", "B", "in", "outB", "outA", "outB", "outNo", "out" }, 12);
  TEST_VIZ(rel, { outNo, A }, { "outA", "out", "A", "outNo" }, 6);
  TEST_VIZ(rel, { outNo, B }, { "outB", "out", "B", "outNo" }, 6);
  TEST_VIZ(rel, { outB, B }, { "outB", "B" }, 2);
  TEST_VIZ(rel, { outB, A }, {}, 0);
  TEST_VIZ(rel, { outB, A }, {}, 0);

  TEST_VIZ(rel, { outNo, A, B },
           { "A", "B", "outB", "outA", "outB", "outNo", "out" }, 9);
}

// cycles shall be displayed, but should not cause eternal loops...
TEST_F(GraphvizTest, GraphsWithCycle) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "build A1: cat A0\n"
    "build A2: cat A1\n"
    "build A3: cat A2\n"
    "build A4: cat A3\n"
    "build A5: cat A4\n"
    "build A0: cat A4\n"
  ));

  DEFINE_VARIABLE(A0);
  DEFINE_VARIABLE(A2);
  DEFINE_VARIABLE(A3);
  DEFINE_VARIABLE(A4);
  DEFINE_VARIABLE(A5);

  // ninja -t graph Target
  auto plain = groupFactory();
  // ninja -t graph -r Target
  auto rel = groupFactory();
  rel.opt_.relations_ = true;
  // ninja -t graph -R Target
  auto plainR = plain;
  plainR.opt_.reverse_ = true;

  std::set<Node*> nodes{ A0 };
  DepLoader::Load(&state_, &fs_, nullptr, nodes, true, false);

  TEST_VIZ(rel, { A0, A3 }, { "A0", "A1", "A2", "A3", "A4" }, 10);
  TEST_VIZ(rel, { A5, A4 }, { "A0", "A1", "A2", "A3", "A4", "A5" }, 12);
  TEST_VIZ(rel, { A2 }, {}, 0);

  TEST_VIZ(plain, { A0 }, { "A0", "A1", "A2", "A3", "A4" }, 10);
  TEST_VIZ(plain, { A5 }, { "A0", "A1", "A2", "A3", "A4", "A5" }, 12);

  TEST_VIZ(plainR, { A0 }, { "A0", "A1", "A2", "A3", "A4", "A5" }, 12);
  TEST_VIZ(plainR, { A4 }, { "A0", "A1", "A2", "A3", "A4", "A5" }, 12);
  TEST_VIZ(plainR, { A5 }, {}, 0);

  TEST_VIZ_CYCLE(rel, { A5, A4 }, { "A0", "A1", "A2", "A3", "A4" }, 10);
  TEST_VIZ_CYCLE(rel, { A0, A3 }, { "A0", "A1", "A2", "A3", "A4" }, 10);
  TEST_VIZ_CYCLE(rel, { A2 }, {});

  TEST_VIZ_CYCLE(plain, { A5 }, { "A0", "A1", "A2", "A3", "A4" }, 10);
  TEST_VIZ_CYCLE(plain, { A0,}, { "A0", "A1", "A2", "A3", "A4" }, 10);

  TEST_VIZ_CYCLE(plainR, { A0 }, { "A0", "A1", "A2", "A3", "A4" }, 10);
  TEST_VIZ_CYCLE(plainR, { A4 }, { "A0", "A1", "A2", "A3", "A4" }, 10);
  TEST_VIZ_CYCLE(plainR, { A5 }, {  });
}

TEST_F(GraphvizTest, Depth) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "build B1 B2: cat A\n"
    "build C1 C2: cat B1\n"
    "build C3 C4: cat B2\n"
    "build D: cat C1 C2 C3 C4\n"
  ));

  DEFINE_VARIABLE(D);
  DEFINE_VARIABLE(A);

  auto depth = [](int i) {
    groupFactory temp;
    temp.opt_.depth = i;
    return temp;
  };

  auto depthR = [depth](int i) {
    auto temp = depth(i);
    temp.opt_.reverse_= true;
    return temp;
  };

  std::set<Node*> nodes{ D };
  DepLoader::Load(&state_, &fs_, nullptr, nodes, true,
                                      false);

  TEST_VIZ(depth(0), { D }, { "D", "C1", "C2", "C3", "C4"}, 5);
  TEST_VIZ(depth(1), { D }, { "D", "C1", "C2", "C3", "C4", "B1", "B2" }, 11);
  TEST_VIZ(depth(2), { D }, { "D", "C1", "C2", "C3", "C4", "B1", "B2" ,"A"}, 14);

  TEST_VIZ(depthR(0), { A }, { "A", "B1", "B2"}, 3);
  TEST_VIZ(depthR(1), { A }, { "A", "C1", "C2", "C3", "C4", "B1", "B2" }, 9);
  TEST_VIZ(depthR(2), { A }, { "A", "C1", "C2", "C3", "C4", "B1", "B2" ,"D"}, 14);
}

// test graph that splits and merges
TEST_F(GraphvizTest, Depth2) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "build X1   : cat X0\n"
    "build X2   : cat X1\n"
    "build X3   : cat X2\n"
    "build B0 C0: cat X3\n"      // long path first
 // "build C0 B0: cat X3\n"      // short path first
    "build B1   : cat B0\n"
    "build Y3   : cat B1 C0\n"   // long path first
  // "build Y3   : cat C0 B1\n"  // short path first
    "build Y2   : cat Y3\n"
    "build Y1   : cat Y2\n"
    "build Y0   : cat Y1\n"
  ));

  DEFINE_VARIABLE(Y0);
  DEFINE_VARIABLE(X0);

   // ninja -t graph Target
  groupFactory depth;
  depth.opt_.depth = 6;
  auto depthR = depth;
  depthR.opt_.reverse_ = true;

  TEST_VIZ(depth,  { Y0 }, {  "X1", "X2", "X3", "B0", "B1", "C0", "Y3", "Y2", "Y1", "Y0" });
  TEST_VIZ(depthR, { X0 }, {  "Y1", "Y2", "Y3", "B0", "B1", "C0", "X3", "X2", "X1", "X0" });
}

// dedicated links shall not be exported
TEST_F(GraphvizTest, links) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "build A : cat C | E || D\n"
  ));

  auto links = [](bool order, bool expl, bool implicit) {
    return groupFactory{ [order, expl, implicit]() {
      graph::Option opt;
      opt.exportOrderOnlyLinks_ = order;
      opt.exportExplicitLinks_ = expl;
      opt.exportImplicitLinks_ = implicit;
      return opt;
    }() };
  };

  DEFINE_VARIABLE(A);

  TEST_VIZ(links(1, 0, 0), { A }, { "A", "D" });
  TEST_VIZ(links(0, 1, 0), { A }, { "A", "C" });
  TEST_VIZ(links(0, 0, 1), { A }, { "A", "E" });
  TEST_VIZ(links(1, 1, 1), { A }, { "A", "C", "D", "E" });
}

TEST_F(GraphvizTest, DyndepLoadImplicit) {
  AssertParse(&state_,
"rule r\n"
"  command = unused\n"
"build out1: r in || dd\n"
"  dyndep = dd\n"
"build out2: r in\n"
  );
  fs_.Create("dd",
"ninja_dyndep_version = 1\n"
"build out1: dyndep | out2\n"
  );
  fs_.Create("r","");
  fs_.Create("in","");
  fs_.Create("out1","");
  fs_.Create("out2", "");

  groupFactory plain;

  DEFINE_VARIABLE(out1);
  DEFINE_VARIABLE(out2);

  TEST_VIZ(plain, { out1, out2 }, { "out1", "out2", "in", "dd" }, 5);
  TEST_VIZ(plain, { out1 }, { "out1", "in", "dd" }, 3);

  std::set<Node*> nodes{ out1, out2 };
  DepLoader::Load(&state_, &fs_, nullptr, nodes, true,
                                      false);
  TEST_VIZ(plain, { out1, out2 }, { "out1", "out2", "in", "dd" }, 6);
  TEST_VIZ(plain, { out1 }, { "out1", "out2", "in", "dd" }, 6);
}

TEST_F(GraphvizTest, DepLoaderGenerated) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "rule cc\n"
    "  command = cc $in\n"
    "build foo.o: cc foo.c\n"
     "  depfile = foo.o.d\n"
    "build X.h: cc X.h.in\n"));

  fs_.Create("foo.c", "");
  fs_.Create("X.h.in", "");
  fs_.Create("foo.o.d", "foo.o: blah.h bar.h X.h\n");

  groupFactory plain;
  // ninja -t graph --no-gen-depload Target
  groupFactory depGen;
  depGen.opt_.exportGenDepLoader_ = false;

  auto foo_o = GetNode("foo.o");

  TEST_VIZ(plain, { foo_o }, { "foo.o", "foo.c" }, 2);

  std::set<Node*> nodes{ foo_o };
  DepLoader::Load(&state_, &fs_, nullptr, nodes, true,
                                      true);

  TEST_VIZ(plain,  { foo_o }, { "foo.o", "foo.c", "X.h", "X.h.in", "bar.h", "blah.h" });
  TEST_VIZ(depGen, { foo_o }, { "foo.o", "foo.c", "X.h", "X.h.in" });
}

TEST_F(GraphvizTest, DynDepLoaderGenerated) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
        "rule touch\n"
        "  command = touch $out\n"
        "rule cp\n"
        "  command = cp $in $out\n"
        "build in: touch\n"
        "build out: touch || dd\n"
        "  dyndep = dd\n"
  ));
  fs_.Create("dd",
             "ninja_dyndep_version = 1\n"
             "build out: dyndep | in in2\n");
  fs_.Create("in2", "");
  fs_.Create("out", "");

  DEFINE_VARIABLE(out);

  groupFactory plain;
  // ninja -t graph -D Target
  groupFactory depGen;
  depGen.opt_.exportGenDepLoader_ = false;

  TEST_VIZ(plain, { out }, { "dd", "out", });

  std::set<Node*> nodes{ out };
  DepLoader::Load(&state_, &fs_, nullptr, nodes, true,
                                      true);

  TEST_VIZ(plain,  { out }, { "dd", "out", "in", "in2" });
  TEST_VIZ(depGen, { out }, { "dd", "out", "in" });
}

TEST_F(GraphvizTest, Regex) {
  ASSERT_NO_FATAL_FAILURE(AssertParse(&state_,
    "build B1 B2: cat A\n"
    "build C1 C2: cat B1\n"
    "build C3 C4: cat B2\n"
    "build D: cat C1 C2 C3 C4\n"
  ));

  DEFINE_VARIABLE(D);
  DEFINE_VARIABLE(A);

  groupFactory reg;
  reg.opt_.regexExclude_ = "D";

  TEST_VIZ(reg, { D }, { "A", "B1", "B2", "C1", "C2", "C3", "C4" });
  reg.opt_.regexExclude_ = "C4";
  TEST_VIZ(reg, { D }, { "A", "B1", "B2", "C1", "C2", "C3" , "D" });
  reg.opt_.regexExclude_ = ".*B.*";
  TEST_VIZ(reg, { D }, { "C1", "C2", "C3", "C4", "D" });

  reg.opt_.reverse_ = true;
  reg.opt_.regexExclude_ = "D";
  TEST_VIZ(reg, { A }, { "A", "B1", "B2", "C1", "C2", "C3", "C4" });
  reg.opt_.regexExclude_ = "C1";
  TEST_VIZ(reg, { A }, { "A", "B1", "B2", "D" , "C2", "C3", "C4" });
  reg.opt_.regexExclude_ = ".*C.*";
  TEST_VIZ(reg, { A }, { "A", "B1", "B2",});
}
