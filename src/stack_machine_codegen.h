#ifndef STACK_MACHINE_CODEGEN_H
#define STACK_MACHINE_CODEGEN_H

#include "output_ir.h"
#include "clang/AST/Decl.h"
#include <string>
#include <vector>

namespace cps {

// Unified helper for generating explicit-stack iterative code.
//
// It centralises the repetitive boilerplate that appears in every stack-based
// transformation rule: frame/entry struct definitions, stack/values vectors,
// the "pop entry / unpack frame" loop skeleton, and the marker-vs-frame
// branch structure.  All generated identifiers use the "__cps_" prefix so they
// cannot collide with user code.
//
// Everything is emitted as output-IR nodes: top-level items (includes,
// structs, enums) go to the IRBuilder passed to the emit* methods, function
// body statements are appended to IRBlocks.
class StackMachineCodegen {
public:
  struct Field {
    std::string Type;
    std::string Name;
    // If non-empty, the constructor parameter gets this default value.
    std::string Default;
    // Fields marked NoUnpack are stored in the frame but are not rebound as
    // local variables when a frame is popped (e.g. enum tags or marker flags).
    bool NoUnpack = false;
  };

  StackMachineCodegen(std::string BaseName, std::string RetType);

  void addInclude(const std::string &Header);
  void addParam(const clang::ParmVarDecl *P);
  void addLocal(const clang::VarDecl *VD);
  void addTagField(const std::string &EnumName);
  void addPlainField(const std::string &Type, const std::string &Name,
                     const std::string &Default = "",
                     bool NoUnpack = false);

  const std::string &baseName() const { return BaseName_; }
  std::string frameName() const;
  std::string entryName() const;
  std::string entryTagName() const;
  static std::string valuesName() { return "__cps_values"; }
  static std::string stackName() { return "__cps_stack"; }
  static std::string curName() { return "__cps_cur"; }
  static std::string entryVarName() { return "__cps_entry"; }

  void emitBanner(IRBuilder &b, const std::string &Kind,
                  const std::string &FuncName);
  void emitIncludes(IRBuilder &b);
  void emitFrameStruct(IRBuilder &b);
  // Emits the entry tag enum followed by the stack-entry struct.
  void emitStackEntryStruct(IRBuilder &b);

  // Statement-level emission into a function body block.
  void emitStackDecl(IRBlock *body);
  void emitValuesDecl(IRBlock *body);

  // Emit an explicit-stack loop that distinguishes marker entries from frame
  // entries. MarkerBody and FrameBody receive the block of their respective
  // branch (after the frame has been unpacked into locals).
  template <typename MarkerFn, typename FrameFn>
  void emitLoop(IRBlock *body, MarkerFn &&MarkerBody, FrameFn &&FrameBody) {
    auto loopBody = IRBuilder::block();
    IRBuilder::add(loopBody.get(),
                   IRBuilder::var("auto", entryVarName(),
                                  IRExpr(stackName() + ".back()")));
    IRBuilder::add(loopBody.get(),
                   IRBuilder::expr(IRExpr(stackName() + ".pop_back()")));

    auto markerBlk = IRBuilder::block();
    IRBuilder::add(markerBlk.get(),
                   IRBuilder::var("auto", curName(),
                                  IRExpr(entryVarName() + ".frame")));
    emitUnpackCurrent(markerBlk.get());
    MarkerBody(markerBlk.get());

    auto frameBlk = IRBuilder::block();
    IRBuilder::add(frameBlk.get(),
                   IRBuilder::var("auto", curName(),
                                  IRExpr(entryVarName() + ".frame")));
    emitUnpackCurrent(frameBlk.get());
    FrameBody(frameBlk.get());

    IRBuilder::add(
        loopBody.get(),
        IRBuilder::if_(IRExpr(entryVarName() + ".tag == " + entryTagName() +
                              "::Marker"),
                       std::move(markerBlk), std::move(frameBlk)));
    IRBuilder::add(body, IRBuilder::while_(IRExpr("!" + stackName() +
                                                  ".empty()"),
                                           std::move(loopBody)));
  }

  // Loop variant for rules that do not need a marker/values mechanism
  // (e.g. BinaryStackRule).
  template <typename Fn>
  void emitSimpleLoop(IRBlock *body, Fn &&Body) {
    auto loopBody = IRBuilder::block();
    IRBuilder::add(loopBody.get(),
                   IRBuilder::var("auto", curName(),
                                  IRExpr(stackName() + ".back()")));
    IRBuilder::add(loopBody.get(),
                   IRBuilder::expr(IRExpr(stackName() + ".pop_back()")));
    emitUnpackCurrent(loopBody.get());
    Body(loopBody.get());
    IRBuilder::add(body, IRBuilder::while_(IRExpr("!" + stackName() +
                                                  ".empty()"),
                                           std::move(loopBody)));
  }

  void emitUnpackCurrent(IRBlock *blk) const;

private:
  static std::string prefix() { return "__cps_"; }

  std::string BaseName_;
  std::string RetType_;
  std::vector<std::string> Includes_;
  std::vector<Field> Fields_;
};

} // namespace cps

#endif // STACK_MACHINE_CODEGEN_H
