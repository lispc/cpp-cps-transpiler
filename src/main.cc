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
static cl::opt<std::string> ForceRule(
    "rule", cl::desc("Force a specific transformation rule (e.g., TuplingRule, GenericStackRule)"),
    cl::value_desc("rule-name"), cl::cat(TranspilerCategory));
static cl::opt<bool> ExplainSelection(
    "explain", cl::desc("Print which rule was selected for each function"),
    cl::init(false), cl::cat(TranspilerCategory));
static cl::list<std::string> TargetFunctions(
    "function", cl::desc("Only transform the specified functions (can be repeated)"),
    cl::value_desc("name"), cl::cat(TranspilerCategory));

// === Parsed transpiler options ===
struct TranspilerOptions {
  std::string OutputFile;
  std::string ForceRule;
  bool ExplainSelection = false;
  std::unordered_set<std::string> TargetFunctions;
};

static TranspilerOptions BuildOptions() {
  TranspilerOptions Opts;
  Opts.OutputFile = OutputFile;
  Opts.ForceRule = ForceRule;
  Opts.ExplainSelection = ExplainSelection;
  for (const auto &Name : ::TargetFunctions)
    Opts.TargetFunctions.insert(Name);
  return Opts;
}

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
  std::unordered_set<std::string> OnStack;
  std::vector<std::string> Stack;
  int idx = 0;

  std::function<void(const std::string &)> strongconnect =
      [&](const std::string &Name) {
    Index[Name] = idx;
    LowLink[Name] = idx;
    ++idx;
    Stack.push_back(Name);
    OnStack.insert(Name);

    auto It = Graph.find(Name);
    if (It != Graph.end()) {
      for (const std::string &Next : It->second) {
        if (!Graph.count(Next))
          continue; // not in graph (not a defined function)
        if (!Index.count(Next)) {
          strongconnect(Next);
          LowLink[Name] = std::min(LowLink[Name], LowLink[Next]);
        } else if (OnStack.count(Next)) {
          LowLink[Name] = std::min(LowLink[Name], Index[Next]);
        }
      }
    }

    if (LowLink[Name] == Index[Name]) {
      std::vector<std::string> SCC;
      while (true) {
        std::string W = Stack.back();
        Stack.pop_back();
        OnStack.erase(W);
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
  explicit CPSConsumer(const TranspilerOptions &Opts) : Opts(Opts) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    auto Functions = collectFunctions(Context);
    auto FuncByName = buildFunctionMap(Functions);
    auto CallGraph = buildCallGraph(Functions);
    auto SCCs = FindSCCs(CallGraph);

    std::vector<std::string> generated;
    for (const auto &SCC : SCCs) {
      if (SCC.size() == 1) {
        processSingleFunction(SCC[0], CallGraph, FuncByName, generated);
      } else {
        processMutualGroup(SCC, FuncByName, generated);
      }
    }

    warnMissingTargets(FuncByName);
    writeOutput(generated);
  }

private:
  TranspilerOptions Opts;

  std::vector<FunctionDecl *> collectFunctions(ASTContext &Context) {
    FunctionCollector Collector;
    Collector.TraverseDecl(Context.getTranslationUnitDecl());
    return Collector.getFunctions();
  }

  std::unordered_map<std::string, FunctionDecl *>
  buildFunctionMap(const std::vector<FunctionDecl *> &Functions) {
    std::unordered_map<std::string, FunctionDecl *> Map;
    for (FunctionDecl *FD : Functions)
      Map[FD->getNameAsString()] = FD;
    return Map;
  }

  std::unordered_map<std::string, std::unordered_set<std::string>>
  buildCallGraph(const std::vector<FunctionDecl *> &Functions) {
    std::unordered_map<std::string, std::unordered_set<std::string>> Graph;
    for (FunctionDecl *FD : Functions) {
      std::unordered_set<std::string> Callees;
      CollectCallees(FD->getBody(), Callees);
      Graph[FD->getNameAsString()] = Callees;
    }
    return Graph;
  }

  bool isTarget(const std::string &Name) const {
    return Opts.TargetFunctions.empty() || Opts.TargetFunctions.count(Name);
  }

  void processSingleFunction(
      const std::string &Name,
      const std::unordered_map<std::string, std::unordered_set<std::string>> &CallGraph,
      const std::unordered_map<std::string, FunctionDecl *> &FuncByName,
      std::vector<std::string> &GeneratedCode) {
    auto It = CallGraph.find(Name);
    if (It == CallGraph.end() || !It->second.count(Name))
      return; // not self-recursive
    if (!isTarget(Name)) {
      llvm::outs() << "[Skipping] " << Name << " (not in --function list)\n";
      return;
    }

    auto FDIt = FuncByName.find(Name);
    if (FDIt == FuncByName.end())
      return;
    FunctionDecl *FD = FDIt->second;
    llvm::outs() << "[Detected recursive function] " << Name << "\n";
    cps::CpsResult result =
        cps::GenerateCPS(FD, Opts.ForceRule, Opts.ExplainSelection);
    if (cps::IsError(result)) {
      const cps::CpsError &err = cps::GetError(result);
      llvm::errs() << "[cps-transpiler] Failed to transform " << Name
                   << ": " << err.Message << "\n";
    } else {
      GeneratedCode.push_back(cps::GetValue(result));
    }
  }

  void processMutualGroup(
      const std::vector<std::string> &SCC,
      const std::unordered_map<std::string, FunctionDecl *> &FuncByName,
      std::vector<std::string> &GeneratedCode) {
    for (const std::string &Name : SCC) {
      if (!isTarget(Name)) {
        for (const std::string &N : SCC)
          llvm::outs() << "[Skipping] " << N
                       << " (mutual group not fully in --function list)\n";
        return;
      }
    }

    std::vector<const FunctionDecl *> Group;
    for (const std::string &Name : SCC) {
      auto It = FuncByName.find(Name);
      if (It == FuncByName.end())
        return;
      llvm::outs() << "[Detected recursive function] " << Name << "\n";
      Group.push_back(It->second);
    }

    cps::CpsResult result = cps::GenerateMutualCPS(Group);
    if (cps::IsError(result)) {
      const cps::CpsError &err = cps::GetError(result);
      llvm::errs() << "[cps-transpiler] Failed to transform mutual group: ";
      for (size_t i = 0; i < SCC.size(); ++i) {
        if (i > 0) llvm::errs() << ", ";
        llvm::errs() << SCC[i];
      }
      llvm::errs() << ": " << err.Message << "\n";
    } else {
      GeneratedCode.push_back(cps::GetValue(result));
    }
  }

  void warnMissingTargets(
      const std::unordered_map<std::string, FunctionDecl *> &FuncByName) const {
    if (Opts.TargetFunctions.empty())
      return;
    for (const std::string &Name : Opts.TargetFunctions) {
      if (!FuncByName.count(Name))
        llvm::errs() << "[cps-transpiler] Warning: --function target not found: "
                     << Name << "\n";
    }
  }

  void writeOutput(const std::vector<std::string> &GeneratedCode) const {
    std::string output;
    output += "\n// ================================\n";
    output += "// Generated iterative code\n";
    output += "// ================================\n\n";
    for (const auto &code : GeneratedCode) {
      output += code;
      output += "\n\n";
    }

    if (!Opts.OutputFile.empty()) {
      std::error_code EC;
      llvm::raw_fd_ostream OS(Opts.OutputFile, EC, llvm::sys::fs::OF_Text);
      if (EC) {
        llvm::errs() << "Error opening output file: " << EC.message() << "\n";
        llvm::outs() << output;
        return;
      }
      OS << output;
      OS.flush();
      llvm::outs() << "Wrote generated code to " << Opts.OutputFile << "\n";
    } else {
      llvm::outs() << output;
    }
  }
};

// === Frontend Action ===
class CPSFrontendAction : public ASTFrontendAction {
public:
  explicit CPSFrontendAction(const TranspilerOptions &Opts) : Opts(Opts) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                  StringRef file) override {
    return std::make_unique<CPSConsumer>(Opts);
  }

private:
  TranspilerOptions Opts;
};

class CPSFrontendActionFactory : public FrontendActionFactory {
public:
  explicit CPSFrontendActionFactory(const TranspilerOptions &Opts) : Opts(Opts) {}

  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<CPSFrontendAction>(Opts);
  }

private:
  TranspilerOptions Opts;
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

  TranspilerOptions Opts = BuildOptions();
  CPSFrontendActionFactory Factory(Opts);
  return Tool.run(&Factory);
}
