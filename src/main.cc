#include "cps_generator.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

// === Command line options ===
static llvm::cl::OptionCategory TranspilerCategory("cps-transpiler options");
static cl::opt<std::string> OutputFile("o", cl::desc("Output file"),
                                        cl::value_desc("filename"),
                                        cl::cat(TranspilerCategory));

// === AST Visitor: Detect recursive functions ===
class CPSVisitor : public RecursiveASTVisitor<CPSVisitor> {
public:
  explicit CPSVisitor(ASTContext *Context) : Context(Context) {}

  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (!FD->hasBody()) return true;
    if (!FD->isThisDeclarationADefinition()) return true;

    std::string Name = FD->getNameAsString();
    if (isRecursive(FD)) {
      llvm::outs() << "[Detected recursive function] " << Name << "\n";
      std::string generated = cps::GenerateCPS(FD);
      if (!generated.empty()) {
        GeneratedCode.push_back(generated);
      }
    }
    return true;
  }

  const std::vector<std::string> &getGenerated() const { return GeneratedCode; }

private:
  ASTContext *Context;
  std::vector<std::string> GeneratedCode;

  bool isRecursive(FunctionDecl *FD) {
    const std::string Name = FD->getNameAsString();
    return containsSelfCall(FD->getBody(), Name);
  }

  bool containsSelfCall(const Stmt *S, const std::string &Name) {
    if (!S) return false;
    if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
      if (const FunctionDecl *Callee = CE->getDirectCallee()) {
        if (Callee->getNameAsString() == Name) return true;
      }
    }
    for (const Stmt *Child : S->children()) {
      if (containsSelfCall(Child, Name)) return true;
    }
    return false;
  }
};

// === AST Consumer ===
class CPSConsumer : public ASTConsumer {
public:
  explicit CPSConsumer(ASTContext *Context) : Visitor(Context) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());

    llvm::outs() << "\n// ================================\n";
    llvm::outs() << "// Generated CPS + Trampoline code\n";
    llvm::outs() << "// ================================\n\n";
    for (const auto &code : Visitor.getGenerated()) {
      llvm::outs() << code << "\n\n";
    }
  }

private:
  CPSVisitor Visitor;
};

// === Frontend Action ===
class CPSFrontendAction : public ASTFrontendAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                  StringRef file) override {
    return std::make_unique<CPSConsumer>(&CI.getASTContext());
  }
};

// === Main ===
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(
      argc, argv, TranspilerCategory, cl::Optional,
      "CPS Transpiler: convert recursive C++ functions to iterative CPS style\n");
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(newFrontendActionFactory<CPSFrontendAction>().get());
}
