//===-- Dependency.h - Memory allocation dependency -------------*- C++ -*-===//
//
//               The Tracer-X KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations for the flow-insensitive dependency
/// analysis to compute the allocations upon which the unsatisfiability core
/// depends, which is used in computing the interpolant.
///
//===----------------------------------------------------------------------===//

#ifndef KLEE_DEPENDENCY_H
#define KLEE_DEPENDENCY_H

#include "AddressSpace.h"
#include "klee/Config/Version.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#else
#include <llvm/Function.h>
#include <llvm/Instruction.h>
#include <llvm/Instructions.h>
#include <llvm/Value.h>
#endif

#include "llvm/Support/raw_ostream.h"

#include <vector>
#include <stack>

namespace klee {
class Dependency;

class ShadowArray {
  static std::map<const Array *, const Array *> shadowArray;

  static UpdateNode *getShadowUpdate(const UpdateNode *chain,
                                     std::set<const Array *> &replacements);

public:
  static ref<Expr> createBinaryOfSameKind(ref<Expr> originalExpr,
                                          ref<Expr> newLhs, ref<Expr> newRhs);

  static void addShadowArrayMap(const Array *source, const Array *target);

  static ref<Expr> getShadowExpression(ref<Expr> expr,
                                       std::set<const Array *> &replacements);

  static std::string getShadowName(std::string name) {
    return "__shadow__" + name;
  }
};

class Allocation {

  protected:
    bool core;

    llvm::Value *site;

    ref<Expr> address;

    Allocation(llvm::Value *_site, ref<Expr> &_address)
        : core(false), site(_site), address(_address) {}

  public:
    enum Kind {
      Unknown,
      Environment,
      Versioned
    };

    virtual Kind getKind() const { return Unknown; }

    virtual ~Allocation() {}

    virtual bool hasAllocationSite(llvm::Value *_site,
                                   ref<Expr> &_address) const {
      return site == _site && address == _address;
    }

    virtual void print(llvm::raw_ostream& stream) const;

    bool hasConstantAddress() { return llvm::isa<ConstantExpr>(address.get()); }

    uint64_t getUIntAddress() {
      return llvm::dyn_cast<ConstantExpr>(address.get())->getZExtValue();
    }

    static bool classof(const Allocation *allocation) { return true; }

    llvm::Value *getSite() const { return site; }

    ref<Expr> getAddress() const { return address; }

    void setAsCore() { core = true; }

    bool isCore() { return core; }

    void dump() const {
      print(llvm::errs());
      llvm::errs() << "\n";
    }
  };

  class VersionedAllocation : public Allocation {
  public:
    VersionedAllocation(llvm::Value *_site, ref<Expr> &_address)
        : Allocation(_site, _address) {}

    ~VersionedAllocation() {}

    Kind getKind() const { return Versioned; }

    static bool classof(const Allocation *allocation) {
      return allocation->getKind() == Versioned;
    }

    static bool classof(const VersionedAllocation *allocation) { return true; }

    void print(llvm::raw_ostream& stream) const;
  };

  class EnvironmentAllocation : public Allocation {
    // We use the first site as the canonical allocation
    // for all environment allocations
    static llvm::Value *canonicalAllocation;
  public:
    EnvironmentAllocation(llvm::Value *_site, ref<Expr> &_address)
        : Allocation(!canonicalAllocation ? (canonicalAllocation = _site)
                                          : canonicalAllocation,
                     _address) {}

    ~EnvironmentAllocation() {}

    Kind getKind() const { return Environment; }

    bool hasAllocationSite(llvm::Value *site, ref<Expr> &_address) const;

    static bool classof(const Allocation *allocation) {
      return allocation->getKind() == Environment;
    }

    static bool classof(const EnvironmentAllocation *allocation) {
      return true;
    }

    void print(llvm::raw_ostream &stream) const;
  };

  class VersionedValue {

    llvm::Value *value;

    const ref<Expr> valueExpr;

    /// @brief to indicate if any unsatisfiability core
    /// depends on this value
    bool core;

  public:
    VersionedValue(llvm::Value *value, ref<Expr> valueExpr)
        : value(value), valueExpr(valueExpr), core(false) {}

    ~VersionedValue() {}

    bool hasValue(llvm::Value *value) const { return this->value == value; }

    ref<Expr> getExpression() const { return valueExpr; }

    void setAsCore() { core = true; }

    bool isCore() const { return core; }

    llvm::Value *getValue() const { return value; }

    void print(llvm::raw_ostream& stream) const;

    void dump() const {
      print(llvm::errs());
      llvm::errs() << "\n";
    }
  };

  class PointerEquality {
    // value equals allocation (pointer)
    const VersionedValue *value;
    Allocation *allocation;

  public:
    PointerEquality(const VersionedValue *value, Allocation *allocation)
        : value(value), allocation(allocation) {}

    ~PointerEquality() {}

    Allocation *equals(const VersionedValue *value) const {
      return this->value == value ? allocation : 0;
    }

    void print(llvm::raw_ostream& stream) const;

    void dump() const {
      print(llvm::errs());
      llvm::errs() << "\n";
    }
  };

  class FlowsTo {
    // target depends on source
    VersionedValue* source;
    VersionedValue* target;

    // Store-load via allocation site
    Allocation *via;

  public:
    FlowsTo(VersionedValue *source, VersionedValue *target)
        : source(source), target(target), via(0) {}

    FlowsTo(VersionedValue *source, VersionedValue *target, Allocation *via)
        : source(source), target(target), via(via) {}

    ~FlowsTo() {}

    VersionedValue *getSource() const { return this->source; }

    VersionedValue *getTarget() const { return this->target; }

    Allocation *getAllocation() const { return this->via; }

    void print(llvm::raw_ostream& sream) const;

    void dump() const {
      print(llvm::errs());
      llvm::errs() << "\n";
    }
  };

  class AllocationGraph {

    class AllocationNode {
      Allocation *allocation;
      std::vector<AllocationNode *> ancestors;
      uint64_t level;

    public:
      AllocationNode(Allocation *allocation, uint64_t _level)
          : allocation(allocation), level(_level) {
        allocation->setAsCore();
      }

      ~AllocationNode() { ancestors.clear(); }

      Allocation *getAllocation() const { return allocation; }

      void addParent(AllocationNode *node) {
        // The user should ensure that we don't store a duplicate
        ancestors.push_back(node);
      }

      std::vector<AllocationNode *> getParents() const { return ancestors; }

      uint64_t getLevel() const { return level; }
    };

    std::vector<AllocationNode *> sinks;
    std::vector<AllocationNode *> allNodes;

    void print(llvm::raw_ostream &stream, std::vector<AllocationNode *> nodes,
               std::vector<AllocationNode *> &printed,
               const unsigned tabNum) const;

    /// consumeSinkNode - Given an allocation, delete all sinks having such
    /// allocation, and replace them as sinks with their parents.
    ///
    /// \param The allocation to match a sink node with.
    void consumeSinkNode(Allocation *allocation);

  public:
    AllocationGraph() {}

    ~AllocationGraph() {
      for (std::vector<AllocationNode *>::iterator it = allNodes.begin(),
                                                   itEnd = allNodes.end();
           it != itEnd; ++it) {
        delete *it;
      }
      allNodes.clear();
    }

    bool isVisited(Allocation *alloc);

    void addNewSink(Allocation *candidateSink);

    void addNewEdge(Allocation *source, Allocation *target);

    std::set<Allocation *> getSinkAllocations() const;

    std::set<Allocation *>
    getSinksWithAllocations(std::vector<Allocation *> valuesList) const;

    /// consumeNodesWithAllocations - Given a set of allocations, delete all
    /// sinks having an allocation in the set, and replace them as sinks with
    /// their parents.
    ///
    /// \param The allocation to match the sink nodes with.
    void consumeSinksWithAllocations(std::vector<Allocation *> allocationsList);

    void dump() const {
      this->print(llvm::errs());
      llvm::errs() << "\n";
    }

    void print(llvm::raw_ostream &stream) const;
  };

  /// \brief Dependency - implementation of field-insensitive value
  ///        dependency for computing allocations the unsatisfiability core
  ///        depends upon, which is used to compute the interpolant.
  ///
  /// Following is the analysis rules to compute value dependency relations
  /// useful for computing the interpolant. Given a finite symbolic execution
  /// path, the computation of the relations terminates. The analysis rules
  /// serve as a guide to the implementation.
  ///
  /// Problems solved:
  /// 1. Components of program states upon which the unsatisfiability core
  ///    depends need to be computed. These components may not be represented
  ///    in the constraints.
  /// 2. To gain more subsumption, we need to store interpolation at more
  ///    program points. More specifically, here we would like to compute the
  ///    instructions that are related to the unsatisfiability core in order
  ///    to compute the right interpolant. That is, given a constraint c(x0)
  ///    in the core, we want to compute the set of state update statements S
  ///    from which we compose the state update function f_S where the next
  ///    state x' = f_S(x0), such that the interpolant after the state update
  ///    is exists x0 . c(x0) /\ x' = f_S(x0)
  ///
  /// Solution:
  /// The dependency computation is based on shadow data structure
  /// representing the following:
  ///
  /// Domains:
  /// VersionedValue -> LLVM values (i.e., variables) with versioning index
  /// VersionedAllocation -> Memory allocations with versioning index
  ///
  /// Basic Relations:
  /// stores(VersionedAlllocation, VersionedValue) - Memory state
  /// depends(VersionedValue, VersionedValue) - Value dependency: The output
  ///    of the analysis.
  /// equals(VersionedValue, VersionedAllocation) - Pointer value equality
  ///
  /// Derived Relations:
  /// Transitive Closure of depends
  ///
  /// depends*(v, v)
  /// depends*(v, v') /\ v != v' iff depends(v, v') /\ depends*(v', v'')
  ///
  /// Indirection Relation
  ///
  /// ind(v, m, 0) iff depends*(v, v') /\ equals(v', m)
  /// ind(v, m, i) /\ i>=1 iff
  ///    depends*(v, v') /\ stores(v'', v') /\ ind(v'', m, i-1)
  ///
  /// In the following abstract operational semantics of LLVM instructions,
  /// R and R' represent the abstract states before and after the execution.
  /// An abstract state is a set having as elements ground substitutions of
  /// the above relations. Below, v and its primed versions represent
  /// VersionedValue elements whereas m and its primed versions represent
  /// VersionedAllocation elements.
  ///
  /// Allocation: v = alloca
  ///
  /// ---------------------------------------------------
  /// R --> R U {equals(succ(v), m) | R |/- equals(_, m)}
  ///
  /// Here succ(v) denotes the next (new) version of v.
  ///
  /// Store: store v', v
  ///
  /// ----------------------------------------------------
  /// R --> R U { stores(succ(m),v) | R |- ind(v', m, 0) }
  ///
  /// Here we use succ(m) to denote the next version of m as this was a
  /// destructive update.
  ///
  /// -------------------------------------------------------------
  /// R --> R U { stores(succ(ind(m,i)), v) | R |- ind(v', m, i), i > 0 }
  ///
  /// Here ind(m,i) is an abstract memory location representing any
  /// memory location that is i-step-reachable via indirection from m.
  ///
  /// R |/- ind(v, _, _)
  /// --------------------------
  /// R --> R U {stores(UNK, v)}
  ///
  /// Here UNK represents an unknown memory location. We assume that
  /// UNK cannot be versioned (non-destructive update applies to it).
  ///
  /// R |- ind(v, UNK_ENV_PTR, _)
  /// ---------------------------
  /// R --> {}
  ///
  /// Storing into the environment results in an error, as the environment
  /// should only be read. Here, we also assume that UNK_ENV_PTR holds.
  ///
  /// Environment Load: v = load @_environ
  ///
  /// ----------------------------------------
  /// R --> R U {equals(succ(v), UNK_ENV_PTR)}
  ///
  /// Load: v = load v'
  ///
  /// Here the rules are not mutually exclusive such that we avoid using set
  /// union to denote abstract states after the execution.
  ///
  /// R |- ind(v', latest(m), 0) /\ stores(latest(m), v''')
  /// R' |- depends(succ(v), v''')
  /// -----------------------------------------------------
  /// R --> R'
  ///
  /// Here latest(m) is only the latest version of allocation m.
  ///
  /// R |- ind(v', m, i) /\ i > 0 /\ stores(m, v''')
  /// R' |- depends(succ(v), v''')
  /// ----------------------------------------------
  /// R --> R'
  ///
  /// R |- ind(v', UNK_ENV_PTR, _)
  /// R' |- depends(succ(v), UNK_ENV)
  /// -------------------------------
  /// R --> R'
  ///
  /// R |/- ind(v', _, _)          R' |- stores(UNK, succ(v))
  /// -------------------------------------------------------
  /// R --> R'
  ///
  /// R |- stores(UNK, v'')                R' |- depends(v, v'')
  /// ----------------------------------------------------------
  /// R --> R'
  ///
  /// Here, any stores to an unknown address would be loaded.
  ///
  /// Getelementptr: v = getelementptr v', idx
  ///
  /// --------------------------------
  /// R --> R U {depends(succ(v), v')}
  ///
  /// Unary Operation: v = UNARY_OP(v') (including binary operation with 1
  /// constant argument)
  ///
  /// --------------------------------
  /// R --> R U {depends(succ(v), v')}
  ///
  /// Binary Operation: v = BINARY_OP(v', v'')
  ///
  /// -------------------------------------------------------
  /// R --> R U {depends(succ(v), v'), depends(succ(v), v'')}
  ///
  /// Phi Node: v = PHI(v'1, ..., v'n)
  ///
  /// -------------------------------------------------------------
  /// R --> R U {depends(succ(v), v'1), ..., depends(succ(v), v'n)}
  ///
  class Dependency {

  public:
    typedef std::pair<ref<Expr>, ref<Expr> > AddressValuePair;
    typedef std::map<uint64_t, AddressValuePair> ConcreteStoreMap;
    typedef std::vector<AddressValuePair> SymbolicStoreMap;
    typedef std::map<llvm::Value *, ConcreteStoreMap> ConcreteStore;
    typedef std::map<llvm::Value *, SymbolicStoreMap> SymbolicStore;

    class Util {

    public:
      template <typename T>
      static void deletePointerVector(std::vector<T *> &list);

      template <typename Key, typename T>
      static void deletePointerMap(std::map<Key *, T *> &map);

      template <typename Key, typename T>
      static void
      deletePointerMapWithVectorValue(std::map<Key *, std::vector<T *> > &map);

      static bool isEnvironmentAllocation(llvm::Value *site);

      /// @brief Tests if an allocation site is main function's argument
      static bool isMainArgument(llvm::Value *site);
    };

  private:
    /// @brief Previous path condition
    Dependency *parentDependency;

    /// @brief Argument values to be passed onto callee
    std::vector<VersionedValue *> argumentValuesList;

    /// @brief Equality of value to address
    std::vector< PointerEquality *> equalityList;

    /// @brief The mapping of allocations/addresses to stored value
    std::map<Allocation *, VersionedValue *> storesMap;

    /// @brief Store the inverse map of both storesMap
    std::map<VersionedValue *, std::vector<Allocation *> > storageOfMap;

    /// @brief Flow relations from one value to another
    std::vector<FlowsTo *> flowsToList;

    std::vector< VersionedValue *> valuesList;

    std::vector<Allocation *> versionedAllocationsList;

    /// @brief allocations of this node and its ancestors
    /// that are needed for the core and dominates other allocations.
    std::set<Allocation *> coreAllocations;

    /// @brief the basic block of the last-executed instruction
    llvm::BasicBlock *incomingBlock;

    VersionedValue *getNewVersionedValue(llvm::Value *value,
                                         ref<Expr> valueExpr);

    Allocation *getInitialAllocation(llvm::Value *allocation,
                                     ref<Expr> &address);

    Allocation *getNewAllocationVersion(llvm::Value *allocation,
                                        ref<Expr> &address);

    std::vector<Allocation *> getAllVersionedAllocations(bool coreOnly =
                                                             false) const;

    /// @brief Gets the latest version of the allocation.
    Allocation *getLatestAllocation(llvm::Value *allocation,
                                    ref<Expr> address) const;

    /// @brief similar to getLatestValue, but we don't check for whether
    /// the value is constant or not
    VersionedValue *getLatestValueNoConstantCheck(llvm::Value *value) const;

    void addPointerEquality(const VersionedValue *value,
                            Allocation *allocation);

    void updateStore(Allocation *allocation, VersionedValue *value);

    void addDependency(VersionedValue *source, VersionedValue *target);

    void addDependencyViaAllocation(VersionedValue *source,
                                    VersionedValue *target, Allocation *via);

    Allocation *resolveAllocation(VersionedValue *value);

    std::vector<Allocation *>
    resolveAllocationTransitively(VersionedValue *value);

    std::vector<VersionedValue *> stores(Allocation *allocation) const;

    /// @brief All values that flows to the target in one step, local
    /// to the current dependency / interpolation tree node
    std::vector<VersionedValue *> directLocalFlowSources(VersionedValue *target) const;

    /// @brief All values that flows to the target in one step
    std::vector<VersionedValue *> directFlowSources(VersionedValue *target) const;

    /// @brief All values that could flow to the target
    std::vector<VersionedValue *> allFlowSources(VersionedValue *target) const;

    /// @brief All the end sources that can flow to the target
    std::vector<VersionedValue *>
    allFlowSourcesEnds(VersionedValue *target) const;

    std::vector<VersionedValue *>
    populateArgumentValuesList(llvm::CallInst *site,
                               std::vector<ref<Expr> > &arguments);

    /// @brief Construct dependency due to load instruction
    bool buildLoadDependency(llvm::Value *fromValue, ref<Expr> fromValueExpr,
                             llvm::Value *toValue, ref<Expr> toValueExpr);

    /// @brief Direct allocation dependency local to an interpolation tree node
    std::map<VersionedValue *, Allocation *>
    directLocalAllocationSources(VersionedValue *target) const;

    /// @brief Direct allocation dependency
    std::map<VersionedValue *, Allocation *>
    directAllocationSources(VersionedValue *target) const;

    /// @brief Builds dependency graph between memory allocations
    void recursivelyBuildAllocationGraph(AllocationGraph *g,
                                         VersionedValue *value,
                                         Allocation *alloc) const;

    /// @brief Builds dependency graph between memory allocations
    void buildAllocationGraph(AllocationGraph *g, VersionedValue *value) const;

    /// @brief Implements the condition to update incoming basic block for phi
    /// nodes
    void updateIncomingBlock(llvm::Instruction *inst);

  public:
    Dependency(Dependency *prev);

    ~Dependency();

    Dependency *cdr() const;

    VersionedValue *getLatestValue(llvm::Value *value, ref<Expr> valueExpr);

    /// @brief Abstract dependency state transition with argument(s)
    void execute(llvm::Instruction *instr, std::vector<ref<Expr> > &args);

    std::pair<ConcreteStore, SymbolicStore>
    getStoredExpressions(std::set<const Array *> &replacements,
                         bool coreOnly) const;

    void bindCallArguments(llvm::Instruction *instr,
                           std::vector<ref<Expr> > &arguments);

    void bindReturnValue(llvm::CallInst *site, llvm::Instruction *inst,
                         ref<Expr> returnValue);

    void markAllValues(AllocationGraph *g, VersionedValue *value);

    void markAllValues(AllocationGraph *g, llvm::Value *value);

    void computeCoreAllocations(AllocationGraph *g);

    void dump() const {
      this->print(llvm::errs());
      llvm::errs() << "\n";
    }

    void print(llvm::raw_ostream& stream) const;

    void print(llvm::raw_ostream &stream, const unsigned tabNum) const;
  };


  std::string makeTabs(const unsigned tab_num);

  std::string appendTab(const std::string &prefix);

}

#endif
