#include "stack_machine_codegen.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "clang/AST/Decl.h"
#include <string>
#include <vector>

namespace cps {

using namespace clang;

StackMachineCodegen::StackMachineCodegen(CodeEmitter &Emitter,
                                         std::string BaseName,
                                         std::string RetType)
    : Emitter_(Emitter), BaseName_(std::move(BaseName)),
      RetType_(std::move(RetType)), Includes_({"vector"}) {}

void StackMachineCodegen::addInclude(const std::string &Header) {
  Includes_.push_back(Header);
}

void StackMachineCodegen::addParam(const ParmVarDecl *P) {
  Fields_.push_back({GetParamStorageType(P), P->getNameAsString(), "", false});
}

void StackMachineCodegen::addLocal(const VarDecl *VD) {
  Fields_.push_back({NormalizeTypeName(VD->getType().getAsString()),
                     VD->getNameAsString(), "", false});
}

void StackMachineCodegen::addTagField(const std::string &EnumName) {
  Fields_.push_back({EnumName, "tag", "", /*NoUnpack=*/true});
}

void StackMachineCodegen::addPlainField(const std::string &Type,
                                        const std::string &Name,
                                        const std::string &Default,
                                        bool NoUnpack) {
  Fields_.push_back({Type, Name, Default, NoUnpack});
}

std::string StackMachineCodegen::frameName() const {
  return prefix() + BaseName_ + "Frame";
}

std::string StackMachineCodegen::entryName() const {
  return prefix() + BaseName_ + "Entry";
}

void StackMachineCodegen::emitBanner(const std::string &Kind,
                                     const std::string &FuncName) {
  Emitter_.raw("// === Generated " + Kind + " code for function: " + FuncName +
               " ===\n\n");
}

void StackMachineCodegen::emitIncludes() {
  for (const auto &h : Includes_)
    Emitter_.line("#include <" + h + ">");
  if (!Includes_.empty())
    Emitter_.nl();
}

void StackMachineCodegen::emitFrameStruct() {
  Emitter_.block("struct " + frameName(), [&](CodeEmitter &b) {
    for (const auto &f : Fields_)
      b.line(f.Type + " " + f.Name + ";");

    std::string ctor = frameName() + "(";
    std::string init;
    for (const auto &f : Fields_) {
      if (!init.empty()) {
        ctor += ", ";
      }
      ctor += f.Type + " " + f.Name + "_";
      if (!f.Default.empty())
        ctor += " = " + f.Default;
      if (!init.empty())
        init += ", ";
      init += f.Name + "(" + f.Name + "_)";
    }
    ctor += ")";
    if (!init.empty())
      ctor += " : " + init;
    ctor += " {}";
    if (!Fields_.empty())
      b.line(ctor);
  }, ";");
  Emitter_.nl();
}

void StackMachineCodegen::emitStackEntryStruct() {
  Emitter_.block("struct " + entryName(), [&](CodeEmitter &b) {
    b.line("enum class Tag { Frame, Marker } tag;");
    b.line(frameName() + " frame;");
    b.line("int count;");
    b.line(entryName() + "(" + frameName() + " f) : tag(Tag::Frame), " +
           "frame(std::move(f)), count(0) {}");
    b.line(entryName() + "(int c, " + frameName() +
           " f) : tag(Tag::Marker), frame(std::move(f)), count(c) {}");
  }, ";");
  Emitter_.nl();
}

void StackMachineCodegen::emitStackDecl() {
  Emitter_.line("std::vector<" + entryName() + "> " + stackName() + ";");
}

void StackMachineCodegen::emitValuesDecl() {
  Emitter_.line("std::vector<" + RetType_ + "> " + valuesName() + ";");
}

void StackMachineCodegen::beginLoop() {
  Emitter_.line("while (!" + stackName() + ".empty()) {");
  Emitter_.inc();
  Emitter_.line("auto " + entryVarName() + " = " + stackName() + ".back();");
  Emitter_.line(stackName() + ".pop_back();");
}

void StackMachineCodegen::endLoop() {
  Emitter_.dec();
  Emitter_.line("}");
}

void StackMachineCodegen::emitUnpackCurrent(CodeEmitter &w) const {
  for (const auto &f : Fields_) {
    if (f.NoUnpack)
      continue;
    w.line("auto " + f.Name + " = " + curName() + "." + f.Name + ";");
  }
}

} // namespace cps
