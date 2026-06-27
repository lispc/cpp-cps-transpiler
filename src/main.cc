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
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

// === Command line options ===
static llvm::cl::OptionCategory TranspilerCategory("cps-transpiler options");
static cl::opt<std::string> OutputFile("o", cl::desc("Output file"),
                                        cl::value_desc("filename"),
                                        cl::cat(TranspilerCategory));

// === AST Visitor: collect function definitions ===
class FunctionCollector : public RecursiveASTVisitor<FunctionCollector> {
public:
  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (!FD->hasBody()) return true;
    if (!FD->isThisDeclarationADefinition()) return true;
    Functions.push_back(FD);
    return true;
  }

  const std::vector<FunctionDecl *> &getFunctions() const { return Functions; }

private:
  std::vector<FunctionDecl *> Functions;
};

// === Mutual recursion analysis ===

static void CollectCallees(const Stmt *S,
                           std::unordered_set<std::string> &Callees) {
  if (!S) return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee())
      Callees.insert(Callee->getNameAsString());
  }
  for (const Stmt *Child : S->children())
    CollectCallees(Child, Callees);
}

// Simple Tarjan SCC for function names.
static std::vector<std::vector<std::string>>
FindSCCs(const std::unordered_map<std::string, std::unordered_set<std::string>> &Graph) {
  std::vector<std::vector<std::string>> SCCs;
  std::unordered_map<std::string, int> Index;
  std::unordered_map<std::string, int> LowLink;
  std::unordered_map<std::string, bool> OnStack;
  std::vector<std::string> Stack;
  int idx = 0;

  std::function<void(const std::string &)> strongconnect =
      [&](const std::string &Name) {
    Index[Name] = idx;
    LowLink[Name] = idx;
    ++idx;
    Stack.push_back(Name);
    OnStack[Name] = true;

    auto It = Graph.find(Name);
    if (It != Graph.end()) {
      for (const std::string &Next : It->second) {
        if (!Graph.count(Next))
          continue; // not in graph (not a defined function)
        if (!Index.count(Next)) {
          strongconnect(Next);
          LowLink[Name] = std::min(LowLink[Name], LowLink[Next]);
        } else if (OnStack[Next]) {
          LowLink[Name] = std::min(LowLink[Name], Index[Next]);
        }
      }
    }

    if (LowLink[Name] == Index[Name]) {
      std::vector<std::string> SCC;
      while (true) {
        std::string W = Stack.back();
        Stack.pop_back();
        OnStack[W] = false;
        SCC.push_back(W);
        if (W == Name) break;
      }
      SCCs.push_back(SCC);
    }
  };

  for (const auto &KV : Graph) {
    if (!Index.count(KV.first))
      strongconnect(KV.first);
  }
  return SCCs;
}

// === AST Consumer ===
class CPSConsumer : public ASTConsumer {
public:
  explicit CPSConsumer(ASTContext *Context) : Context(Context) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    FunctionCollector Collector;
    Collector.TraverseDecl(Context.getTranslationUnitDecl());

    std::vector<FunctionDecl *> Functions = Collector.getFunctions();
    std::unordered_map<std::string, FunctionDecl *> FuncByName;
    for (FunctionDecl *FD : Functions)
      FuncByName[FD->getNameAsString()] = FD;

    // Build call graph among defined functions.
    std::unordered_map<std::string, std::unordered_set<std::string>> CallGraph;
    for (FunctionDecl *FD : Functions) {
      std::unordered_set<std::string> Callees;
      CollectCallees(FD->getBody(), Callees);
      CallGraph[FD->getNameAsString()] = Callees;
    }

    auto SCCs = FindSCCs(CallGraph);

    for (const auto &SCC : SCCs) {
      if (SCC.size() == 1) {
        const std::string &Name = SCC[0];
        auto It = CallGraph.find(Name);
        if (It == CallGraph.end() || !It->second.count(Name))
          continue; // not self-recursive
        FunctionDecl *FD = FuncByName[Name];
        llvm::outs() << "[Detected recursive function] " << Name << "\n";
        std::string generated = cps::GenerateCPS(FD);
        if (!generated.empty())
          GeneratedCode.push_back(generated);
      } else {
        // Mutual recursion group.
        std::vector<const FunctionDecl *> Group;
        for (const std::string &Name : SCC) {
          llvm::outs() << "[Detected recursive function] " << Name << "\n";
          Group.push_back(FuncByName[Name]);
        }
        std::string generated = cps::GenerateMutualCPS(Group);
        if (!generated.empty())
          GeneratedCode.push_back(generated);
      }
    }

    llvm::outs() << "\n// ================================\n";
    llvm::outs() << "// Generated iterative code\n";
    llvm::outs() << "// ================================\n\n";
    for (const auto &code : GeneratedCode) {
      llvm::outs() << code << "\n\n";
    }
  }

private:
  ASTContext *Context;
  std::vector<std::string> GeneratedCode;
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
      "cps-transpiler: convert recursive C++ functions to iterative code\n");
  if (!ExpectedParser) {
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  return Tool.run(newFrontendActionFactory<CPSFrontendAction>().get());
}
