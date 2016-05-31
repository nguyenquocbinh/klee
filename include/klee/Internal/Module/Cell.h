//===-- Cell.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CELL_H
#define KLEE_CELL_H

#include <klee/Expr.h>
#include <klee/Taint.h>

#include "llvm/Support/raw_ostream.h"

namespace klee {
  class MemoryObject;

  struct Cell {
    ref<Expr> value;
    TaintSet taint;

    void print(llvm::raw_ostream &stream) const {
    	value->print(stream);
    	stream << " with taint set = " << taint << "\n";
    }

    void dump() const {
    	print(llvm::errs());
    }
  };
}

#endif
