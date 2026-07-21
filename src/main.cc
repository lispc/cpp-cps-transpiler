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

#include <algorithm>
#include <functional>
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

// Callee lists preserve first-discovery order so that SCC traversal (and
// therefore the emitted output) is deterministic.
using FuncGraph =
    std::unordered_map<const FunctionDecl *,
                       std::vector<const FunctionDecl *>>;

static void CollectCallees(
    const Stmt *S, std::vector<const FunctionDecl *> &Callees,
    std::unordered_set<const FunctionDecl *> &Seen,
    const std::unordered_set<const FunctionDecl *> &Defined) {
  if (!S)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      const FunctionDecl *Can = Callee->getCanonicalDecl();
      if (Defined.count(Can) && Seen.insert(Can).second)
        Callees.push_back(Can);
    }
  }
  for (const Stmt *Child : S->children())
    CollectCallees(Child, Callees, Seen, Defined);
}

// Simple Tarjan SCC over function declarations.  Nodes are visited in the
// given Order (source order) so the resulting SCC list is deterministic.
static std::vector<std::vector<const FunctionDecl *>>
FindSCCs(const FuncGraph &Graph,
         const std::vector<const FunctionDecl *> &Order) {
  std::vector<std::vector<const FunctionDecl *>> SCCs;
  std::unordered_map<const FunctionDecl *, int> Index;
  std::unordered_map<const FunctionDecl *, int> LowLink;
  std::unordered_set<const FunctionDecl *> OnStack;
  std::vector<const FunctionDecl *> Stack;
  int idx = 0;

  std::function<void(const FunctionDecl *)> strongconnect =
      [&](const FunctionDecl *F) {
    Index[F] = idx;
    LowLink[F] = idx;
    ++idx;
    Stack.push_back(F);
    OnStack.insert(F);

    auto It = Graph.find(F);
    if (It != Graph.end()) {
      for (const FunctionDecl *Next : It->second) {
        if (!Graph.count(Next))
          continue; // not in graph (not a defined function)
        if (!Index.count(Next)) {
          strongconnect(Next);
          LowLink[F] = std::min(LowLink[F], LowLink[Next]);
        } else if (OnStack.count(Next)) {
          LowLink[F] = std::min(LowLink[F], Index[Next]);
        }
      }
    }

    if (LowLink[F] == Index[F]) {
      std::vector<const FunctionDecl *> SCC;
      while (true) {
        const FunctionDecl *W = Stack.back();
        Stack.pop_back();
        OnStack.erase(W);
        SCC.push_back(W);
        if (W == F)
          break;
      }
      SCCs.push_back(SCC);
    }
  };

  for (const FunctionDecl *F : Order) {
    if (!Index.count(F))
      strongconnect(F);
  }
  return SCCs;
}

// === AST Consumer ===
class CPSConsumer : public ASTConsumer {
public:
  explicit CPSConsumer(const TranspilerOptions &Opts) : Opts(Opts) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    auto Functions = collectFunctions(Context);
    // Sort by source position so that SCC discovery order (and hence the
    // order of emitted code blocks) is deterministic.
    const SourceManager &SM = Context.getSourceManager();
    std::sort(Functions.begin(), Functions.end(),
              [&SM](const FunctionDecl *A, const FunctionDecl *B) {
                return SM.getFileOffset(A->getBeginLoc()) <
                       SM.getFileOffset(B->getBeginLoc());
              });
    auto CanonToDef = buildCanonToDef(Functions);
    auto CallGraph = buildCallGraph(Functions);
    std::vector<const FunctionDecl *> Order;
    for (const FunctionDecl *FD : Functions)
      Order.push_back(FD->getCanonicalDecl());
    auto SCCs = FindSCCs(CallGraph, Order);

    std::vector<std::string> generated;
    for (const auto &SCC : SCCs) {
      if (SCC.size() == 1) {
        auto It = CanonToDef.find(SCC[0]);
        if (It != CanonToDef.end())
          processSingleFunction(It->second, CallGraph, generated);
      } else {
        processMutualGroup(SCC, CanonToDef, generated);
      }
    }

    warnMissingTargets(Functions);
    writeOutput(generated);
  }

private:
  TranspilerOptions Opts;

  std::vector<FunctionDecl *> collectFunctions(ASTContext &Context) {
    FunctionCollector Collector;
    Collector.TraverseDecl(Context.getTranslationUnitDecl());
    return Collector.getFunctions();
  }

  std::unordered_map<const FunctionDecl *, FunctionDecl *>
  buildCanonToDef(const std::vector<FunctionDecl *> &Functions) {
    std::unordered_map<const FunctionDecl *, FunctionDecl *> Map;
    for (FunctionDecl *FD : Functions)
      Map[FD->getCanonicalDecl()] = FD;
    return Map;
  }

  FuncGraph buildCallGraph(const std::vector<FunctionDecl *> &Functions) {
    std::unordered_set<const FunctionDecl *> Defined;
    for (FunctionDecl *FD : Functions)
      Defined.insert(FD->getCanonicalDecl());

    FuncGraph Graph;
    for (FunctionDecl *FD : Functions) {
      std::vector<const FunctionDecl *> Callees;
      std::unordered_set<const FunctionDecl *> Seen;
      CollectCallees(FD->getBody(), Callees, Seen, Defined);
      Graph[FD->getCanonicalDecl()] = std::move(Callees);
    }
    return Graph;
  }

  bool isTarget(const std::string &Name) const {
    return Opts.TargetFunctions.empty() || Opts.TargetFunctions.count(Name);
  }

  static std::string getSimpleName(const FunctionDecl *FD) {
    return FD->getNameAsString();
  }

  void processSingleFunction(
      FunctionDecl *FD, const FuncGraph &CallGraph,
      std::vector<std::string> &GeneratedCode) {
    const FunctionDecl *Can = FD->getCanonicalDecl();
    auto It = CallGraph.find(Can);
    if (It == CallGraph.end() ||
        std::find(It->second.begin(), It->second.end(), Can) ==
            It->second.end())
      return; // not self-recursive

    std::string Name = getSimpleName(FD);
    if (!isTarget(Name)) {
      llvm::outs() << "[Skipping] " << Name << " (not in --function list)\n";
      return;
    }

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
      const std::vector<const FunctionDecl *> &SCC,
      const std::unordered_map<const FunctionDecl *, FunctionDecl *> &CanonToDef,
      std::vector<std::string> &GeneratedCode) {
    std::vector<const FunctionDecl *> Group;
    for (const FunctionDecl *Can : SCC) {
      auto It = CanonToDef.find(Can);
      if (It == CanonToDef.end())
        continue;
      Group.push_back(It->second);
    }
    if (Group.empty())
      return;

    for (const FunctionDecl *FD : Group) {
      if (!isTarget(getSimpleName(FD))) {
        for (const FunctionDecl *F : Group)
          llvm::outs() << "[Skipping] " << getSimpleName(F)
                       << " (mutual group not fully in --function list)\n";
        return;
      }
    }

    for (const FunctionDecl *FD : Group)
      llvm::outs() << "[Detected recursive function] " << getSimpleName(FD)
                   << "\n";

    cps::CpsResult result = cps::GenerateMutualCPS(Group);
    if (cps::IsError(result)) {
      const cps::CpsError &err = cps::GetError(result);
      llvm::errs() << "[cps-transpiler] Failed to transform mutual group: ";
      for (size_t i = 0; i < Group.size(); ++i) {
        if (i > 0)
          llvm::errs() << ", ";
        llvm::errs() << getSimpleName(Group[i]);
      }
      llvm::errs() << ": " << err.Message << "\n";
    } else {
      GeneratedCode.push_back(cps::GetValue(result));
    }
  }

  void warnMissingTargets(const std::vector<FunctionDecl *> &Functions) const {
    if (Opts.TargetFunctions.empty())
      return;
    std::unordered_set<std::string> Names;
    for (FunctionDecl *FD : Functions)
      Names.insert(FD->getNameAsString());
    for (const std::string &Name : Opts.TargetFunctions) {
      if (!Names.count(Name))
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
