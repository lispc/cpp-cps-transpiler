#ifndef STACK_MACHINE_CODEGEN_H
#define STACK_MACHINE_CODEGEN_H

#include "code_emitter.h"
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

  StackMachineCodegen(CodeEmitter &Emitter, std::string BaseName,
                      std::string RetType);

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
  static std::string valuesName() { return "__cps_values"; }
  static std::string stackName() { return "__cps_stack"; }
  static std::string curName() { return "__cps_cur"; }
  static std::string entryVarName() { return "__cps_entry"; }

  void emitBanner(const std::string &Kind, const std::string &FuncName);
  void emitIncludes();
  void emitFrameStruct();
  void emitStackEntryStruct();

  void emitStackDecl();
  void emitValuesDecl();

  // Manually open/close an explicit-stack loop that distinguishes marker
  // entries from frame entries.  Between beginLoop() and endLoop() the caller
  // should emit exactly one marker branch (emitMarkerBranch) and one frame
  // branch (emitFrameBranch).
  void beginLoop();
  void endLoop();

  template <typename Fn>
  void emitMarkerBranch(Fn &&Body) {
    Emitter_.block("if (" + entryVarName() + ".tag == " + entryName() +
                       "::Tag::Marker)",
                   [&](CodeEmitter &w) {
                     w.line("auto " + curName() + " = " + entryVarName() +
                            ".frame;");
                     emitUnpackCurrent(w);
                     Body(w);
                   });
  }

  template <typename Fn>
  void emitFrameBranch(Fn &&Body) {
    Emitter_.block("else", [&](CodeEmitter &w) {
      w.line("auto " + curName() + " = " + entryVarName() + ".frame;");
      emitUnpackCurrent(w);
      Body(w);
    });
  }

  // Loop variant for rules that do not need a marker/values mechanism
  // (e.g. BinaryStackRule).
  template <typename Fn>
  void emitSimpleLoop(Fn &&Body) {
    Emitter_.block("while (!" + stackName() + ".empty())",
                   [&](CodeEmitter &w) {
                     w.line("auto " + curName() + " = " + stackName() +
                            ".back();");
                     w.line(stackName() + ".pop_back();");
                     emitUnpackCurrent(w);
                     Body(w);
                   });
  }

  void emitUnpackCurrent(CodeEmitter &w) const;

private:
  static std::string prefix() { return "__cps_"; }

  CodeEmitter &Emitter_;
  std::string BaseName_;
  std::string RetType_;
  std::vector<std::string> Includes_;
  std::vector<Field> Fields_;
};

} // namespace cps

#endif // STACK_MACHINE_CODEGEN_H
