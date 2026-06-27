#include "cps_generator.h"
#include "code_emitter.h"
#include "transformation_rule.h"
#include "transformation_rules.h"
#include "transformation_rules_helpers.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace llvm;

namespace cps {

// ============================================================
// AST printing helpers
// ============================================================

std::string PrintExpr(const Expr *E, const ASTContext *Ctx) {
  std::string s;
  llvm::raw_string_ostream os(s);
  E->printPretty(os, nullptr, Ctx->getPrintingPolicy());
  os.flush();
  return s;
}

std::string PrintStmt(const Stmt *S, const ASTContext *Ctx) {
  std::string s;
  llvm::raw_string_ostream os(s);
  S->printPretty(os, nullptr, Ctx->getPrintingPolicy());
  os.flush();
  return s;
}

std::string Trim(const std::string &S) {
  size_t a = 0;
  while (a < S.size() && std::isspace(static_cast<unsigned char>(S[a])))
    ++a;
  size_t b = S.size();
  while (b > a && std::isspace(static_cast<unsigned char>(S[b - 1])))
    --b;
  return S.substr(a, b - a);
}

std::string StripOuterParens(std::string s) {
  while (true) {
    s = Trim(s);
    if (s.size() < 2 || s.front() != '(' || s.back() != ')')
      break;
    int depth = 0;
    bool canStrip = true;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '(')
        ++depth;
      else if (s[i] == ')') {
        --depth;
        if (depth == 0 && i != s.size() - 1) {
          canStrip = false;
          break;
        }
      }
    }
    if (!canStrip)
      break;
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

std::string PrintExprWithReplacements(
    const Expr *E,
    const std::unordered_map<const Expr *, std::string> &Repls,
    const ASTContext *Ctx) {
  if (!E)
    return "";

  auto It = Repls.find(E);
  if (It != Repls.end())
    return It->second;

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    return "(" + PrintExprWithReplacements(BO->getLHS(), Repls, Ctx) + " " +
           BO->getOpcodeStr().str() + " " +
           PrintExprWithReplacements(BO->getRHS(), Repls, Ctx) + ")";
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    std::string op = UO->getOpcodeStr(UO->getOpcode()).str();
    std::string sub = PrintExprWithReplacements(UO->getSubExpr(), Repls, Ctx);
    if (!UO->isPostfix())
      return op + "(" + sub + ")";
    return "(" + sub + ")" + op;
  }

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    std::string s;
    if (const Expr *Callee = CE->getCallee()) {
      s += PrintExprWithReplacements(Callee, Repls, Ctx);
    }
    s += "(";
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (i > 0)
        s += ", ";
      s += PrintExprWithReplacements(CE->getArg(i), Repls, Ctx);
    }
    s += ")";
    return s;
  }

  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    return "(" + PrintExprWithReplacements(CO->getCond(), Repls, Ctx) + " ? " +
           PrintExprWithReplacements(CO->getTrueExpr(), Repls, Ctx) + " : " +
           PrintExprWithReplacements(CO->getFalseExpr(), Repls, Ctx) + ")";
  }

  if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    return PrintExprWithReplacements(ASE->getBase(), Repls, Ctx) + "[" +
           PrintExprWithReplacements(ASE->getIdx(), Repls, Ctx) + "]";
  }

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
    return PrintExprWithReplacements(ME->getBase(), Repls, Ctx) +
           (ME->isArrow() ? "->" : ".") +
           ME->getMemberNameInfo().getAsString();
  }

  if (const ParenExpr *PE = dyn_cast<ParenExpr>(E)) {
    return "(" + PrintExprWithReplacements(PE->getSubExpr(), Repls, Ctx) + ")";
  }

  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    return PrintExprWithReplacements(ICE->getSubExpr(), Repls, Ctx);
  }

  if (const CStyleCastExpr *CCE = dyn_cast<CStyleCastExpr>(E)) {
    return "(" + CCE->getTypeAsWritten().getAsString() + ")" +
           "(" + PrintExprWithReplacements(CCE->getSubExpr(), Repls, Ctx) + ")";
  }

  return PrintExpr(E, Ctx);
}

// ============================================================
// Saved argument analysis
// ============================================================

bool NeedsSavedArg(
    const Expr *E, const std::vector<CallExpr *> &Holes,
    size_t HoleIdx,
    const std::unordered_set<std::string> &ParamNames) {
  if (ExprUsesParams(E, ParamNames))
    return true;
  for (size_t i = HoleIdx + 1; i < Holes.size(); ++i) {
    for (unsigned a = 0; a < Holes[i]->getNumArgs(); ++a) {
      if (ExprUsesParams(Holes[i]->getArg(a), ParamNames))
        return true;
    }
  }
  return false;
}

// ============================================================
// Code generation state helpers
// ============================================================

std::string NormalizeTypeName(const std::string &TypeStr) {
  // Clang prints C++ bool as "_Bool" in some contexts, which is not valid
  // C++ without <stdbool.h>. Normalize it to the proper C++ keyword.
  if (TypeStr == "_Bool")
    return "bool";
  return TypeStr;
}

std::string GetParamStorageType(const ParmVarDecl *PVD) {
  QualType T = PVD->getType();
  if (T->isReferenceType())
    T = T.getNonReferenceType();
  return NormalizeTypeName(T.getAsString());
}

std::string BuildFunctionSignature(const FunctionDecl *FD,
                                   const std::string &RetType) {
  std::string sig = RetType + " " + FD->getNameAsString() + "(";
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (i > 0)
      sig += ", ";
    sig += NormalizeTypeName(FD->getParamDecl(i)->getType().getAsString()) +
           " " + FD->getParamDecl(i)->getNameAsString();
  }
  sig += ")";
  return sig;
}

std::string ArgCtorDefun(const std::vector<std::string> &ParamValues,
                         const GenContext &Ctx) {
  std::string s = Ctx.ArgType + "(";
  for (size_t i = 0; i < ParamValues.size(); ++i) {
    if (i > 0) s += ", ";
    s += ParamValues[i];
  }
  s += ")";
  return s;
}

std::string Indent(const std::string &s, int n) {
  std::string prefix(n, ' ');
  std::string result;
  bool first = true;
  std::istringstream iss(s);
  std::string line;
  while (std::getline(iss, line)) {
    if (!first) result += "\n";
    result += prefix + line;
    first = false;
  }
  return result;
}

std::string ReplaceParamsWithCur(const std::string &S,
                                 const std::vector<std::string> &Params) {
  std::string result = S;
  for (const auto &p : Params)
    result = ReplaceWholeWord(result, p, "cur." + p);
  return result;
}

std::string ReplaceParamWithLiteral(const std::string &S,
                                    const std::string &Param,
                                    const std::string &Literal) {
  return ReplaceWholeWord(S, Param, Literal);
}

void EmitStmts(CodeEmitter &e, const std::vector<const Stmt *> &Stmts,
               const ASTContext *Ctx) {
  for (const Stmt *S : Stmts) {
    std::string line = PrintStmt(S, Ctx);
    // Clang's printPretty does not append a semicolon when printing an Expr
    // that happens to be used as a full statement. Add it manually.
    if (isa<Expr>(S) && !line.empty() && line.back() != ';')
      line += ';';
    e.line(line);
  }
}

void EmitUnpacksDefun(CodeEmitter &e, const std::string &ArgName,
                      const GenContext &Ctx) {
  for (const auto &p : Ctx.ParamNames) {
    e.line("auto " + p + " = " + ArgName + "." + p + ";");
  }
}

std::string EmitFrameStruct(CodeEmitter &e, const FunctionDecl *FD,
                            const GenContext &Ctx) {
  std::string frameName = Ctx.FuncName + "Frame";
  e.block("struct " + frameName, [&](CodeEmitter &b) {
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      b.line(GetParamStorageType(FD->getParamDecl(i)) + " " +
             FD->getParamDecl(i)->getNameAsString() + ";");
    }
    std::string ctor = frameName + "(";
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        ctor += ", ";
      ctor += GetParamStorageType(FD->getParamDecl(i)) + " " +
              FD->getParamDecl(i)->getNameAsString();
    }
    ctor += ") : ";
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        ctor += ", ";
      std::string pName = FD->getParamDecl(i)->getNameAsString();
      ctor += pName + "(" + pName + ")";
    }
    ctor += " {}";
    b.line(ctor);
  }, ";");
  e.nl();
  return frameName;
}

// ============================================================
// Body analysis
// ============================================================

namespace {

const Expr *ExtractReturnExpr(const Stmt *S) {
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S))
    return RS->getRetValue();
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->size() == 1)
      return ExtractReturnExpr(CS->body_begin()[0]);
  }
  return nullptr;
}

bool IsVoidReturn(const Stmt *S) {
  if (isa<ReturnStmt>(S))
    return true;
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->size() == 1)
      return IsVoidReturn(CS->body_begin()[0]);
  }
  return false;
}

BaseCase MakeBaseCase(const Expr *Cond, const Expr *Value,
                      const ASTContext *Ctx) {
  BaseCase bc;
  bc.CondExpr = Cond;
  bc.ValueExpr = Value;
  bc.CondStr = Cond ? StripOuterParens(PrintExpr(Cond, Ctx)) : "";
  bc.ValueStr = Value ? StripOuterParens(PrintExpr(Value, Ctx)) : "";
  return bc;
}

void FlattenIfElse(const Stmt *S, BodyAnalysis &BA, const ASTContext *Ctx) {
  if (!S)
    return;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    const Expr *BaseExpr = ExtractReturnExpr(IfS->getThen());
    if (BaseExpr)
      BA.BaseCases.push_back(MakeBaseCase(IfS->getCond(), BaseExpr, Ctx));
    if (const Stmt *Else = IfS->getElse())
      FlattenIfElse(Else, BA, Ctx);
    return;
  }
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
    BA.RecExpr = RS->getRetValue();
    BA.IsRecursive = true;
  }
}

namespace {

// Collect all case values from a possibly nested chain of CaseStmts
// (e.g., "case 0: case 1: return ...").
std::vector<const Expr *> CollectCaseValues(const CaseStmt *Case) {
  std::vector<const Expr *> values;
  const Stmt *Sub = Case;
  while (const CaseStmt *CS = dyn_cast<CaseStmt>(Sub)) {
    values.push_back(CS->getLHS());
    if (CS->getRHS())
      values.push_back(CS->getRHS());
    Sub = CS->getSubStmt();
  }
  return values;
}

// Get the actual body after stripping nested CaseStmts.
const Stmt *GetCaseBody(const CaseStmt *Case) {
  const Stmt *Sub = Case;
  while (const CaseStmt *CS = dyn_cast<CaseStmt>(Sub))
    Sub = CS->getSubStmt();
  return Sub;
}

} // anonymous namespace

bool ExtractSwitchCases(const SwitchStmt *SS, BodyAnalysis &BA,
                        const ASTContext *Ctx) {
  if (!SS)
    return false;
  const Expr *Cond = SS->getCond();
  if (!Cond)
    return false;
  std::string condStr = PrintExpr(Cond, Ctx);

  const CompoundStmt *CS = dyn_cast<CompoundStmt>(SS->getBody());
  if (!CS)
    return false;

  const Expr *pendingValue = nullptr;
  std::string pendingValueStr;
  std::vector<const Expr *> pendingCases;

  for (const Stmt *Sub : CS->body()) {
    if (const CaseStmt *Case = dyn_cast<CaseStmt>(Sub)) {
      std::vector<const Expr *> caseVals = CollectCaseValues(Case);
      const Expr *ret = ExtractReturnExpr(GetCaseBody(Case));
      for (const Expr *cv : caseVals)
        pendingCases.push_back(cv);
      if (ret) {
        pendingValue = ret;
        pendingValueStr = PrintExpr(ret, Ctx);
      }
      if (pendingValue) {
        for (const Expr *cv : pendingCases) {
          BaseCase bc;
          bc.ValueExpr = pendingValue;
          bc.CondStr = "(" + condStr + " == " + PrintExpr(cv, Ctx) + ")";
          bc.ValueStr = pendingValueStr;
          BA.BaseCases.push_back(bc);
        }
        pendingCases.clear();
      }
    } else if (const DefaultStmt *Def = dyn_cast<DefaultStmt>(Sub)) {
      const Expr *ret = ExtractReturnExpr(Def->getSubStmt());
      if (!ret)
        return false;
      BA.RecExpr = ret;
      BA.IsRecursive = true;
    }
  }
  return BA.IsRecursive;
}

bool IsReturnOrIfReturnOrSwitch(const Stmt *S) {
  if (isa<ReturnStmt>(S))
    return true;
  if (isa<IfStmt>(S))
    return true;
  if (isa<SwitchStmt>(S))
    return true;
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->size() == 1)
      return IsReturnOrIfReturnOrSwitch(CS->body_begin()[0]);
  }
  return false;
}

} // anonymous namespace

bool AnalyzeBody(const Stmt *Body, BodyAnalysis &BA,
                 const ASTContext *Ctx,
                 const std::string &FuncName,
                 bool IsVoid) {
  BA = BodyAnalysis();
  const CompoundStmt *CS = dyn_cast<CompoundStmt>(Body);
  if (!CS)
    return false;

  size_t idx = 0;
  while (idx < CS->size()) {
    const Stmt *S = CS->body_begin()[idx];
    if (IsReturnOrIfReturnOrSwitch(S))
      break;
    BA.LeadingStmts.push_back(S);
    ++idx;
  }

  while (idx < CS->size()) {
    const Stmt *S = CS->body_begin()[idx];
    if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
      const Expr *BaseExpr = ExtractReturnExpr(IfS->getThen());
      // A void base case "if (cond) return;" has no value expression and is
      // valid for void functions; non-void functions require a return value.
      if (!BaseExpr && !IsVoidReturn(IfS->getThen()))
        return false;
      BA.BaseCases.push_back(MakeBaseCase(IfS->getCond(), BaseExpr, Ctx));
      if (const Stmt *Else = IfS->getElse()) {
        FlattenIfElse(Else, BA, Ctx);
        ++idx;
        break;
      }
      ++idx;
      continue;
    }
    if (const SwitchStmt *SS = dyn_cast<SwitchStmt>(S)) {
      if (!ExtractSwitchCases(SS, BA, Ctx))
        return false;
      ++idx;
      break;
    }
    if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
      const Expr *Ret = RS->getRetValue();
      // Normalize "return cond ? base : rec;" into a base case + recursive expr.
      bool splitTernary = false;
      if (Ret) {
        if (const ConditionalOperator *CO =
                dyn_cast<ConditionalOperator>(Ret->IgnoreParenImpCasts())) {
          const Expr *TrueE = CO->getTrueExpr()->IgnoreParenImpCasts();
          const Expr *FalseE = CO->getFalseExpr()->IgnoreParenImpCasts();
          bool trueRec = ContainsRecursiveCall(TrueE, FuncName);
          bool falseRec = ContainsRecursiveCall(FalseE, FuncName);
          if (trueRec && !falseRec) {
            BA.BaseCases.push_back(
                MakeBaseCase(CO->getCond(), FalseE, Ctx));
            BA.RecExpr = TrueE;
            BA.IsRecursive = true;
            splitTernary = true;
          } else if (!trueRec && falseRec) {
            BA.BaseCases.push_back(
                MakeBaseCase(CO->getCond(), TrueE, Ctx));
            BA.RecExpr = FalseE;
            BA.IsRecursive = true;
            splitTernary = true;
          }
        }
      }
      if (!splitTernary) {
        BA.RecExpr = Ret;
        BA.IsRecursive = true;
      }
      ++idx;
      break;
    }
    // For void tail-recursive functions, the recursive "return" may be an
    // expression statement containing a direct recursive call.
    if (IsVoid) {
      if (const Expr *E = dyn_cast<Expr>(S)) {
        E = E->IgnoreParenImpCasts();
        if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
          if (const FunctionDecl *Callee = CE->getDirectCallee()) {
            if (Callee->getNameAsString() == FuncName) {
              BA.RecExpr = E;
              BA.IsRecursive = true;
              ++idx;
              break;
            }
          }
        }
      }
    }
    BA.MiddleStmts.push_back(S);
    ++idx;
  }

  if (idx != CS->size())
    return false;

  return !BA.BaseCases.empty() && BA.IsRecursive;
}

// ============================================================
// Public API
// ============================================================

std::string GenerateCPS(const FunctionDecl *FD,
                        const std::string &ForceRule,
                        bool ExplainSelection) {
  if (!FD || !FD->hasBody())
    return "";

  GenContext Ctx;
  Ctx.FuncName = FD->getNameAsString();
  Ctx.ArgType = Ctx.FuncName + "Arg";
  Ctx.ASTCtx = &FD->getASTContext();
  Ctx.RetType = NormalizeTypeName(FD->getReturnType().getAsString());
  Ctx.ForceRule = ForceRule;
  Ctx.ExplainSelection = ExplainSelection;

  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    std::string pname = FD->getParamDecl(i)->getNameAsString();
    Ctx.ParamNames.push_back(pname);
    Ctx.ParamNameSet.insert(pname);
  }

  bool isVoid = FD->getReturnType()->isVoidType();
  BodyAnalysis BA;
  if (!AnalyzeBody(FD->getBody(), BA, Ctx.ASTCtx, Ctx.FuncName, isVoid)) {
    errs() << "[cps-transpiler] Function body not in supported shape "
              "(expected: leading-stmts? (if-return)* return recursive;)\n";
    return "";
  }

  auto rules = CreateDefaultRules();
  const TransformationRule *bestRule = nullptr;
  for (const auto &rule : rules) {
    if (!Ctx.ForceRule.empty() &&
        std::string(rule->name()) != Ctx.ForceRule) {
      continue;
    }
    if (rule->applies(FD, BA, Ctx)) {
      if (!bestRule || rule->cost() < bestRule->cost())
        bestRule = rule.get();
    }
  }

  if (Ctx.ExplainSelection) {
    if (bestRule) {
      llvm::outs() << "[Rule selection] " << Ctx.FuncName << " -> "
                   << bestRule->name() << "\n";
    } else {
      llvm::outs() << "[Rule selection] " << Ctx.FuncName
                   << " -> no applicable rule\n";
    }
  }

  if (bestRule)
    return bestRule->apply(FD, BA, Ctx);

  return "";
}

// ============================================================
// Mutual recursion support (basic tail-recursive groups)
// ============================================================

namespace {

std::string CommonPrefix(const std::vector<std::string> &Names) {
  if (Names.empty())
    return "";
  std::string prefix = Names[0];
  for (size_t i = 1; i < Names.size(); ++i) {
    size_t len = 0;
    while (len < prefix.size() && len < Names[i].size() &&
           prefix[len] == Names[i][len])
      ++len;
    prefix.resize(len);
  }
  // Trim trailing underscore if present for nicer naming.
  if (!prefix.empty() && prefix.back() == '_')
    prefix.pop_back();
  return prefix;
}

} // anonymous namespace

// Forward declaration.
std::string GenerateMutualGenericStackCPS(
    const std::vector<const FunctionDecl *> &FDs,
    const std::unordered_map<std::string, BodyAnalysis> &Analyses,
    const std::string &retType, const std::vector<std::string> &paramTypes,
    const std::vector<std::string> &paramNames);

std::string GenerateMutualCPS(
    const std::vector<const FunctionDecl *> &FDs) {
  if (FDs.empty())
    return "";

  const ASTContext *Ctx = &FDs[0]->getASTContext();

  // Require identical signatures (same return type and parameter types).
  std::string retType = NormalizeTypeName(FDs[0]->getReturnType().getAsString());
  std::vector<std::string> paramTypes;
  std::vector<std::string> paramNames;
  for (unsigned i = 0; i < FDs[0]->getNumParams(); ++i) {
    paramTypes.push_back(
        NormalizeTypeName(FDs[0]->getParamDecl(i)->getType().getAsString()));
    paramNames.push_back(FDs[0]->getParamDecl(i)->getNameAsString());
  }
  for (size_t f = 1; f < FDs.size(); ++f) {
    if (NormalizeTypeName(FDs[f]->getReturnType().getAsString()) != retType)
      return "";
    if (FDs[f]->getNumParams() != paramTypes.size())
      return "";
    for (unsigned i = 0; i < FDs[f]->getNumParams(); ++i) {
      if (NormalizeTypeName(FDs[f]->getParamDecl(i)->getType().getAsString()) !=
          paramTypes[i])
        return "";
      if (FDs[f]->getParamDecl(i)->getNameAsString() != paramNames[i])
        return "";
    }
  }

  // Analyze each function body.
  std::unordered_map<std::string, BodyAnalysis> Analyses;
  for (const FunctionDecl *FD : FDs) {
    BodyAnalysis BA;
    bool isVoid = FD->getReturnType()->isVoidType();
    if (!AnalyzeBody(FD->getBody(), BA, Ctx, FD->getNameAsString(), isVoid))
      return "";
    Analyses[FD->getNameAsString()] = BA;
  }

  // Determine whether all mutual calls are tail calls. If not, fall back to
  // a generic-stack dispatcher.
  bool allTailCalls = true;
  for (const FunctionDecl *FD : FDs) {
    const BodyAnalysis &BA = Analyses[FD->getNameAsString()];
    const Expr *RecExpr = BA.RecExpr;
    const CallExpr *CE = dyn_cast<CallExpr>(RecExpr);
    if (!CE)
      allTailCalls = false;
    else if (!IsInTailPosition(CE, FD->getBody(), FD->getNameAsString()))
      allTailCalls = false;
    if (!allTailCalls)
      break;
  }

  if (!allTailCalls)
    return GenerateMutualGenericStackCPS(FDs, Analyses, retType, paramTypes,
                                         paramNames);

  CodeEmitter e;
  e.raw("// === Generated mutual-recursion code ===\n\n");

  // Enum tag.
  std::string enumName = FDs[0]->getNameAsString() + "MutualTag";
  e.raw("enum class " + enumName + " {\n");
  for (const FunctionDecl *FD : FDs)
    e.raw("  " + FD->getNameAsString() + ",\n");
  e.raw("};\n\n");

  // Dispatcher name: use common prefix if available, otherwise first function.
  std::vector<std::string> names;
  for (const FunctionDecl *FD : FDs)
    names.push_back(FD->getNameAsString());
  std::string prefix = CommonPrefix(names);
  std::string dispatcherName = prefix.empty() ? names[0] : prefix;
  dispatcherName += "_dispatch";

  // Dispatcher signature.
  std::string sig = retType + " " + dispatcherName +
                    "(" + enumName + " tag";
  for (size_t i = 0; i < paramNames.size(); ++i)
    sig += ", " + paramTypes[i] + " " + paramNames[i];
  sig += ")";

  e.block(sig, [&](CodeEmitter &b) {
    b.block("while (1)", [&](CodeEmitter &w) {
      w.block("switch (tag)", [&](CodeEmitter &sw) {
        for (const FunctionDecl *FD : FDs) {
          const BodyAnalysis &BA = Analyses[FD->getNameAsString()];
          sw.line("case " + enumName + "::" + FD->getNameAsString() + ": {");
          sw.inc();
          for (const auto &bc : BA.BaseCases) {
            sw.line("if (" + bc.CondStr + ") return " + bc.ValueStr + ";");
          }
          const CallExpr *CE = dyn_cast<CallExpr>(BA.RecExpr);
          std::string nextTag = CE->getDirectCallee()->getNameAsString();
          sw.line("tag = " + enumName + "::" + nextTag + ";");
          for (unsigned i = 0;
               i < FD->getNumParams() && i < CE->getNumArgs(); ++i) {
            sw.line("auto next_" + paramNames[i] + " = " +
                    PrintExpr(CE->getArg(i), Ctx) + ";");
          }
          for (unsigned i = 0;
               i < FD->getNumParams() && i < CE->getNumArgs(); ++i) {
            sw.line(paramNames[i] + " = next_" + paramNames[i] + ";");
          }
          sw.line("break;");
          sw.dec();
          sw.line("}");
        }
      });
    });
  });
  e.nl();

  // Wrapper functions.
  for (const FunctionDecl *FD : FDs) {
    std::string wrapperSig = retType + " " + FD->getNameAsString() + "(";
    for (size_t i = 0; i < paramNames.size(); ++i) {
      if (i > 0) wrapperSig += ", ";
      wrapperSig += paramTypes[i] + " " + paramNames[i];
    }
    wrapperSig += ")";
    e.block(wrapperSig, [&](CodeEmitter &b) {
      std::string call = "return " + dispatcherName +
                         "(" + enumName + "::" +
                         FD->getNameAsString();
      for (const auto &p : paramNames)
        call += ", " + p;
      call += ");";
      b.line(call);
    });
    e.nl();
  }

  return e.str();
}

// ============================================================
// Generic-stack mutual recursion dispatcher
// ============================================================

namespace {

void CollectGroupHoles(
    const Expr *E, const std::unordered_set<std::string> &GroupNames,
    std::vector<CallExpr *> &Holes) {
  if (!E)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (GroupNames.count(Callee->getNameAsString())) {
        Holes.push_back(const_cast<CallExpr *>(CE));
        return;
      }
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child))
      CollectGroupHoles(ChildExpr, GroupNames, Holes);
  }
}

bool ContainsGroupCall(
    const Expr *E, const std::unordered_set<std::string> &GroupNames) {
  if (!E)
    return false;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (GroupNames.count(Callee->getNameAsString()))
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ContainsGroupCall(dyn_cast_or_null<Expr>(Child), GroupNames))
      return true;
  }
  return false;
}

} // anonymous namespace

std::string GenerateMutualGenericStackCPS(
    const std::vector<const FunctionDecl *> &FDs,
    const std::unordered_map<std::string, BodyAnalysis> &Analyses,
    const std::string &retType, const std::vector<std::string> &paramTypes,
    const std::vector<std::string> &paramNames) {
  const ASTContext *Ctx = &FDs[0]->getASTContext();

  std::unordered_set<std::string> groupNames;
  for (const FunctionDecl *FD : FDs)
    groupNames.insert(FD->getNameAsString());

  // Precompute holes per function and combined expressions.
  std::unordered_map<std::string, std::vector<CallExpr *>> HolesByFunc;
  std::unordered_map<std::string, std::string> CombinedByFunc;
  for (const FunctionDecl *FD : FDs) {
    const std::string &name = FD->getNameAsString();
    const BodyAnalysis &BA = Analyses.at(name);
    std::vector<CallExpr *> holes;
    CollectGroupHoles(BA.RecExpr, groupNames, holes);
    if (holes.empty())
      return "";
    // Reject nested group calls inside hole arguments.
    for (CallExpr *CE : holes) {
      for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
        if (ContainsGroupCall(CE->getArg(i), groupNames))
          return "";
      }
    }
    HolesByFunc[name] = holes;
    std::unordered_map<const Expr *, std::string> repls;
    for (size_t i = 0; i < holes.size(); ++i)
      repls[holes[i]] = "v" + std::to_string(i);
    CombinedByFunc[name] =
        StripOuterParens(PrintExprWithReplacements(BA.RecExpr, repls, Ctx));
  }

  // Identify tail-call members: their recursive expression is exactly one
  // direct call to another group member. These can be dispatched without a
  // combine marker.
  std::unordered_map<std::string, bool> isTailByFunc;
  for (const FunctionDecl *FD : FDs) {
    const std::string &name = FD->getNameAsString();
    const auto &holes = HolesByFunc[name];
    isTailByFunc[name] =
        (holes.size() == 1 && holes[0] == Analyses.at(name).RecExpr);
  }

  CodeEmitter e;
  e.raw("// === Generated mutual-recursion code (generic stack) ===\n\n");
  e.line("#include <vector>");
  e.nl();

  // Function tag enum.
  std::string enumName = FDs[0]->getNameAsString() + "MutualTag";
  e.raw("enum class " + enumName + " {\n");
  for (const FunctionDecl *FD : FDs)
    e.raw("  " + FD->getNameAsString() + ",\n");
  e.raw("};\n\n");

  // Dispatcher name.
  std::vector<std::string> names;
  for (const FunctionDecl *FD : FDs)
    names.push_back(FD->getNameAsString());
  std::string prefix = CommonPrefix(names);
  std::string dispatcherName = prefix.empty() ? names[0] : prefix;
  dispatcherName += "_dispatch";

  // Frame struct.
  std::string frameName = dispatcherName + "Frame";
  e.block("struct " + frameName, [&](CodeEmitter &b) {
    b.line(enumName + " tag;");
    for (size_t i = 0; i < paramNames.size(); ++i)
      b.line(paramTypes[i] + " " + paramNames[i] + ";");
    std::string ctor = frameName + "(" + enumName + " t";
    for (size_t i = 0; i < paramNames.size(); ++i)
      ctor += ", " + paramTypes[i] + " " + paramNames[i];
    ctor += ") : tag(t)";
    for (const auto &p : paramNames)
      ctor += ", " + p + "(" + p + ")";
    ctor += " {}";
    b.line(ctor);
  }, ";");
  e.nl();

  // Stack entry struct.
  std::string entryName = dispatcherName + "StackEntry";
  e.block("struct " + entryName, [&](CodeEmitter &b) {
    b.line("enum class Tag { Frame, Marker } tag;");
    b.line(frameName + " frame;");
    b.line("int count;");
    b.line(entryName + "(" + frameName + " f) : tag(Tag::Frame), " +
           "frame(std::move(f)), count(0) {}");
    b.line(entryName + "(int c, " + frameName + " f) : tag(Tag::Marker), " +
           "frame(std::move(f)), count(c) {}");
  }, ";");
  e.nl();

  // Dispatcher signature.
  std::string sig = retType + " " + dispatcherName + "(" + enumName + " tag";
  for (size_t i = 0; i < paramNames.size(); ++i)
    sig += ", " + paramTypes[i] + " " + paramNames[i];
  sig += ")";

  e.block(sig, [&](CodeEmitter &b) {
    b.line("std::vector<" + entryName + "> stack;");
    {
      std::string init = "stack.emplace_back(" + frameName + "(tag";
      for (const auto &p : paramNames)
        init += ", " + p;
      init += "));";
      b.line(init);
    }
    b.line("std::vector<" + retType + "> values;");

    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("auto entry = stack.back();");
      w.line("stack.pop_back();");
      w.block("if (entry.tag == " + entryName + "::Tag::Marker)",
              [&](CodeEmitter &iw) {
        iw.line("auto cur = entry.frame;");
        for (const auto &p : paramNames)
          iw.line("auto " + p + " = cur." + p + ";");
        if (paramNames.empty())
          iw.line("(void)cur;");
        iw.line(enumName + " ftag = cur.tag;");
        for (size_t i = 0; i < FDs.size(); ++i) {
          const std::string &name = FDs[i]->getNameAsString();
          iw.line((i == 0 ? "if (" : "else if (") +
                  std::string("ftag == ") + enumName + "::" + name + ") {");
          iw.inc();
          size_t hcount = HolesByFunc.at(name).size();
          for (size_t j = 0; j < hcount; ++j) {
            iw.line(retType + " v" + std::to_string(j) +
                    " = values.back(); values.pop_back();");
          }
          iw.line("values.push_back(" + CombinedByFunc.at(name) + ");");
          iw.dec();
          iw.line("}");
        }
      });
      w.block("else", [&](CodeEmitter &iw) {
        iw.line("auto cur = entry.frame;");
        for (const auto &p : paramNames)
          iw.line("auto " + p + " = cur." + p + ";");
        iw.line(enumName + " ftag = cur.tag;");
        for (size_t fi = 0; fi < FDs.size(); ++fi) {
          const FunctionDecl *FD = FDs[fi];
          const std::string &name = FD->getNameAsString();
          const BodyAnalysis &BA = Analyses.at(name);
          iw.line((fi == 0 ? "if (" : "else if (") +
                  std::string("ftag == ") + enumName + "::" + name + ") {");
          iw.inc();
          EmitStmts(iw, BA.LeadingStmts, Ctx);
          for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
            std::string prefix = (bi == 0) ? "if (" : "else if (";
            const auto &bc = BA.BaseCases[bi];
            iw.line(prefix + bc.CondStr + ")");
            iw.inc();
            iw.line("values.push_back(" + bc.ValueStr + ");");
            iw.dec();
          }
          iw.line("else {");
          iw.inc();
          EmitStmts(iw, BA.MiddleStmts, Ctx);
          const std::vector<CallExpr *> &holes = HolesByFunc.at(name);
          if (isTailByFunc[name]) {
            // Tail-call member: directly push the callee frame, no marker.
            const CallExpr *CE = holes[0];
            std::string push = "stack.emplace_back(" + frameName + "(" +
                               enumName + "::" +
                               CE->getDirectCallee()->getNameAsString();
            for (unsigned a = 0;
                 a < FD->getNumParams() && a < CE->getNumArgs(); ++a)
              push += ", " + PrintExpr(CE->getArg(a), Ctx);
            push += "));";
            iw.line(push);
          } else {
            iw.line("stack.emplace_back(" + entryName + "(" +
                    std::to_string(holes.size()) + ", cur));");
            for (size_t hi = 0; hi < holes.size(); ++hi) {
              std::string push = "stack.emplace_back(" + frameName + "(" +
                                 enumName + "::" +
                                 holes[hi]->getDirectCallee()->getNameAsString();
              for (unsigned a = 0;
                   a < FD->getNumParams() && a < holes[hi]->getNumArgs(); ++a)
                push += ", " + PrintExpr(holes[hi]->getArg(a), Ctx);
              push += "));";
              iw.line(push);
            }
          }
          iw.dec();
          iw.line("}");
          iw.dec();
          iw.line("}");
        }
      });
    });
    b.line("return values.back();");
  });
  e.nl();

  // Wrapper functions.
  for (const FunctionDecl *FD : FDs) {
    std::string wrapperSig = retType + " " + FD->getNameAsString() + "(";
    for (size_t i = 0; i < paramNames.size(); ++i) {
      if (i > 0) wrapperSig += ", ";
      wrapperSig += paramTypes[i] + " " + paramNames[i];
    }
    wrapperSig += ")";
    e.block(wrapperSig, [&](CodeEmitter &b) {
      std::string call = "return " + dispatcherName +
                         "(" + enumName + "::" + FD->getNameAsString();
      for (const auto &p : paramNames)
        call += ", " + p;
      call += ");";
      b.line(call);
    });
    e.nl();
  }

  return e.str();
}

} // namespace cps
