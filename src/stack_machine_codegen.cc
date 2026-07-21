#include "stack_machine_codegen.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "clang/AST/Decl.h"
#include <string>
#include <vector>

namespace cps {

using namespace clang;

StackMachineCodegen::StackMachineCodegen(std::string BaseName,
                                         std::string RetType)
    : BaseName_(std::move(BaseName)), RetType_(std::move(RetType)),
      Includes_({"vector"}) {}

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

std::string StackMachineCodegen::entryTagName() const {
  return prefix() + BaseName_ + "EntryTag";
}

void StackMachineCodegen::emitBanner(IRBuilder &b, const std::string &Kind,
                                     const std::string &FuncName) {
  b.comment("=== Generated " + Kind + " code for function: " + FuncName +
            " ===");
}

void StackMachineCodegen::emitIncludes(IRBuilder &b) {
  for (const auto &h : Includes_)
    b.include(h);
}

void StackMachineCodegen::emitFrameStruct(IRBuilder &b) {
  IRStructData data;
  data.name = frameName();
  std::vector<IRCtorParam> params;
  std::vector<std::pair<std::string, std::string>> init;
  for (const auto &f : Fields_) {
    data.fields.emplace_back(f.Type, f.Name);
    params.emplace_back(f.Type, f.Name + "_", f.Default);
    init.emplace_back(f.Name, f.Name + "_");
  }
  if (!Fields_.empty())
    data.ctors.emplace_back(std::move(params), std::move(init));
  b.structDef(std::move(data));
}

void StackMachineCodegen::emitStackEntryStruct(IRBuilder &b) {
  b.enumDef(entryTagName(), {"Frame", "Marker"});

  IRStructData data;
  data.name = entryName();
  data.fields.emplace_back(entryTagName(), "tag");
  data.fields.emplace_back(frameName(), "frame");
  data.fields.emplace_back("int", "count");

  {
    std::vector<IRCtorParam> params;
    params.emplace_back(frameName(), "f");
    std::vector<std::pair<std::string, std::string>> init = {
        {"tag", entryTagName() + "::Frame"},
        {"frame", "std::move(f)"},
        {"count", "0"},
    };
    data.ctors.emplace_back(std::move(params), std::move(init));
  }
  {
    std::vector<IRCtorParam> params;
    params.emplace_back("int", "c");
    params.emplace_back(frameName(), "f");
    std::vector<std::pair<std::string, std::string>> init = {
        {"tag", entryTagName() + "::Marker"},
        {"frame", "std::move(f)"},
        {"count", "c"},
    };
    data.ctors.emplace_back(std::move(params), std::move(init));
  }
  b.structDef(std::move(data));
}

void StackMachineCodegen::emitStackDecl(IRBlock *body) {
  IRBuilder::add(body, IRBuilder::var("std::vector<" + entryName() + ">",
                                      stackName()));
}

void StackMachineCodegen::emitValuesDecl(IRBlock *body) {
  IRBuilder::add(body, IRBuilder::var("std::vector<" + RetType_ + ">",
                                      valuesName()));
}

void StackMachineCodegen::emitUnpackCurrent(IRBlock *blk) const {
  for (const auto &f : Fields_) {
    if (f.NoUnpack)
      continue;
    IRBuilder::add(blk, IRBuilder::var("auto", f.Name,
                                       IRExpr(curName() + "." + f.Name)));
  }
}

} // namespace cps
