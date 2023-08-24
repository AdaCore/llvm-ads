//===-- llvm-ads.cpp - Generate Ada spec from bitcode --------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include <system_error>
using namespace llvm;

static cl::OptionCategory DisCategory("llvm-ads options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("[input bitcode]..."),
                                          cl::cat(DisCategory), cl::Required);

static cl::opt<std::string> OutputFilename(cl::Positional, cl::Required,
                                           cl::desc("[output spec]..."),
                                           cl::cat(DisCategory), cl::Required);

namespace {
struct LLVMDisDiagnosticHandler : public DiagnosticHandler {
  char *Prefix;
  LLVMDisDiagnosticHandler(char *PrefixPtr) : Prefix(PrefixPtr) {}
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    raw_ostream &OS = errs();
    OS << Prefix << ": ";
    switch (DI.getSeverity()) {
    case DS_Error:
      WithColor::error(OS);
      break;
    case DS_Warning:
      WithColor::warning(OS);
      break;
    case DS_Remark:
      OS << "remark: ";
      break;
    case DS_Note:
      WithColor::note(OS);
      break;
    }

    DiagnosticPrinterRawOStream DP(OS);
    DI.print(DP);
    OS << '\n';

    if (DI.getSeverity() == DS_Error)
      exit(1);
    return true;
  }
};
} // namespace

void printSanitized(const StringRef &S, raw_ostream &Out) {
  std::string s = S.str();

  // For some reason, names can have dots. Replace with underscores.
  for (size_t i = 0; i < s.length(); ++i) {
    switch (s[i]) {
    case '.':
    case ':':
    case '/':
    case '-':
      s[i] = '_';
    }
  }

  size_t offset = s.find_first_not_of("_");
  std::string s2 = s.substr(offset, -1);

  if (s2 != "in") {
    Out << s2;
  } else {
    Out << "inn";
  }
}

std::string toName(Type *t) {
  switch (t->getTypeID()) {
  case Type::VoidTyID:
    return "void";
  case Type::HalfTyID:
    return "half";
  case Type::BFloatTyID:
    return "bfloat";
  case Type::FloatTyID:
    return "float";
  case Type::DoubleTyID:
    return "double";
  case Type::X86_FP80TyID:
    return "x86_fp80";
  case Type::FP128TyID:
    return "fp128";
  case Type::PPC_FP128TyID:
    return "ppc_fp128";
  case Type::LabelTyID:
    return "label";
  case Type::MetadataTyID:
    return "metadata";
  case Type::X86_MMXTyID:
    return "x86_mmx";
  case Type::X86_AMXTyID:
    return "x86_amx";
  case Type::TokenTyID:
    return "token";
  case Type::IntegerTyID:
    return "i" + std::to_string(cast<IntegerType>(t)->getBitWidth());
  case Type::PointerTyID:
    return toName(t->getPointerElementType()) + "ptr";
  case Type::ArrayTyID:
    return toName(t->getArrayElementType()) + "arr";
  case Type::StructTyID: {
    std::string result = "";

    for (unsigned int i = 0; i < t->getNumContainedTypes(); ++i) {
      result += toName(t->getContainedType(i));
    }

    return result;
  }
  default:
    errs() << "unmanaged type: ";
    t->print(errs(), true);
    assert(false);
  }
}

void printType(Type *t, raw_ostream &Out) {
  // Partly stolen from AsmWriter.cpp:print
  // Needs some adjusting for Ada types...
  switch (t->getTypeID()) {
  case Type::VoidTyID:
  case Type::HalfTyID:
  case Type::BFloatTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID:
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
  case Type::LabelTyID:
  case Type::MetadataTyID:
  case Type::X86_MMXTyID:
  case Type::X86_AMXTyID:
  case Type::TokenTyID:
  case Type::IntegerTyID:
    Out << toName(t);
    return;
  case Type::PointerTyID:
    Out << "access ";
    printType(t->getPointerElementType(), Out);
    return;
  case Type::ArrayTyID:
    Out << "array ";

    if (t->getArrayNumElements() > 0) {
      Out << "(1.." << t->getArrayNumElements() << ")";
    } else {
      Out << "(Integer range <>)";
    }

    Out << " of ";
    printType(t->getArrayElementType(), Out);
    return;
  case Type::StructTyID:
    if (!((StructType *)t)->isLiteral() &&
        !((StructType *)t)->getName().empty()) {
      printSanitized(((StructType *)t)->getName(), Out);
    } else {
      Out << toName(t);
    }

    return;
  default:
    errs() << "unmanaged type: ";
    t->print(errs(), true);
    assert(false);
  }
}

void printArrType(ArrayType &ATy, raw_ostream &Out) {
  Out << "type " << toName((Type *)&ATy) << " is ";
  printType((Type *)&ATy, Out);
  Out << ";";
}

void printStructType(StructType &STy, raw_ostream &Out) {
  StructType::element_iterator It = STy.element_begin();
  std::string s = "";

  for (StructType::element_iterator End = STy.element_end(); It != End; ++It) {
    Type *T = *It;

    while (T->getTypeID() == Type::PointerTyID) {
      T = T->getPointerElementType();
    }

    if (T->getTypeID() == Type::StructTyID && !((StructType *)*It)->hasName()) {
      printStructType(*(StructType *)T, Out);
      Out << "\n";
    } else if (T->getTypeID() == Type::ArrayTyID) {
      printArrType(*(ArrayType *)T, Out);
      Out << "\n";
    }
  }

  Out << "type ";

  if (STy.hasName()) {
    printSanitized(STy.getName(), Out);
  } else {
    Out << toName(&STy);
  }

  Out << " is record\n";
  It = STy.element_begin();
  int x = 0;

  for (StructType::element_iterator End = STy.element_end(); It != End; ++It) {
    Type *T = *It;
    Out << "e" << x++ << " : ";

    while (T->getTypeID() == Type::PointerTyID) {
      Out << "access ";
      T = T->getPointerElementType();
    }

    if (T->getTypeID() == Type::ArrayTyID ||
        T->getTypeID() == Type::StructTyID) {
      Out << toName(T);
    } else {
      printType(T, Out);
    }

    Out << ";\n";
  }

  Out << "end record;";
}

void printTypes(std::unique_ptr<Module> &M, raw_ostream &Out) {
  TypeFinder types;
  types.run(*M, false);

  for (const auto STy : types) {
    // STy->getName()'s doc says to never use getName() on literals
    if (STy->isLiteral())
      continue;
    // Opaque types don't have a body yet - ignore them
    if (STy->isOpaque())
      continue;

    printStructType(*STy, Out);
    Out << "\n";
  }
}

void printGlobal(const GlobalVariable &GV, raw_ostream &Out) {
  if (!GV.hasName())
    return;

  printSanitized(GV.getName(), Out);
  Out << " : ";
  printType(GV.getType(), Out);
  Out << " with Import, External_Name => \"" << GV.getName() << '"';
}

void printArgument(const Argument &FA, raw_ostream &Out) {
  if (FA.hasName()) {
    printSanitized(FA.getName(), Out);
  } else {
    Out << "a" << FA.getArgNo();
  }

  Out << " : ";
  printType(FA.getType(), Out);
}

void printFunction(const Function &F, raw_ostream &Out) {
  if (F.getReturnType()->getTypeID() == Type::VoidTyID) {
    Out << "procedure";
  } else {
    Out << "function";
  }

  Out << " ";
  printSanitized(F.getName(), Out);

  if (F.getFunctionType()->getNumParams() > 0) {
    Out << " (";
    for (const Argument &Arg : F.args()) {
      if (Arg.getArgNo() > 0)
        Out << "; ";
      printArgument(Arg, Out);
    }
    Out << ")";
  }

  if (F.getReturnType()->getTypeID() != Type::VoidTyID) {
    Out << " return " << toName(F.getReturnType());
  }

  Out << " with Import, External_Name => \"" << F.getName() << "\";";
}

void printPrimitiveTypes(raw_ostream &Out) {
  Out << "subtype i1 is Interfaces.C.c_bool;\n";
  Out << "subtype i8 is Interfaces.C.char;\n";
  Out << "subtype u8 is Interfaces.C.unsigned_char;\n";
  Out << "subtype i16 is Interfaces.C.short;\n";
  Out << "subtype u16 is Interfaces.C.unsigned_short;\n";
  Out << "subtype i32 is Interfaces.C.int;\n";
  Out << "subtype u32 is Interfaces.C.unsigned;\n";
  Out << "subtype i64 is Interfaces.C.long;\n";
  Out << "subtype u64 is Interfaces.C.unsigned_long;\n";
  Out << "subtype double is Interfaces.C.double;\n";
}

void printWiths(raw_ostream &Out) { Out << "with Interfaces.C;\n"; }

void printModule(std::unique_ptr<Module> &M, std::string PackageName,
                 raw_ostream &Out) {

  if (!M->getSourceFileName().empty()) {
    Out << "-- Generated from " << M->getSourceFileName() << "\n";
  }

  printWiths(Out);

  Out << "package " << PackageName << " is\n";

  printPrimitiveTypes(Out);

  /* Out << "\n-- types\n"; */
  printTypes(M, Out);

  /* Out << "\n-- globals\n"; */
  /* for (const GlobalVariable &GV : M->globals()) { */
  /*   printGlobal(GV, Out); */
  /*   Out << ";\n"; */
  /* } */

  Out << "\n-- functions\n";
  for (const Function &F : *M) {
    printFunction(F, Out);
    Out << '\n';
  }

  /* for (const GlobalAlias &GA : M->aliases()) */
  /*   printAlias(&GA); */
  /* for (const GlobalIFunc &GI : M->ifuncs()) */
  /*   printIFunc(&GI); */

  Out << "end " << PackageName << ";";
}

static ExitOnError ExitOnErr;

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  ExitOnErr.setBanner(std::string(argv[0]) + ": error: ");

  cl::HideUnrelatedOptions({&DisCategory, &getColorCategory()});
  cl::ParseCommandLineOptions(argc, argv, "llvm .bc -> .ads generator\n");

  LLVMContext Context;
  Context.setDiagnosticHandler(
      std::make_unique<LLVMDisDiagnosticHandler>(argv[0]));
  Context.setOpaquePointers(false);

  std::unique_ptr<MemoryBuffer> MB =
      ExitOnErr(errorOrToExpected(MemoryBuffer::getFileOrSTDIN(InputFilename)));

  BitcodeFileContents IF = ExitOnErr(llvm::getBitcodeFileContents(*MB));

  const size_t N = IF.Mods.size();

  for (size_t I = 0; I < N; ++I) {
    BitcodeModule MB = IF.Mods[I];
    std::unique_ptr<Module> M =
        ExitOnErr(MB.getLazyModule(Context, true, true));
    ExitOnErr(M->materializeAll());

    BitcodeLTOInfo LTOInfo = ExitOnErr(MB.getLTOInfo());
    std::unique_ptr<ModuleSummaryIndex> Index;
    if (LTOInfo.HasSummary)
      Index = ExitOnErr(MB.getSummary());

    std::error_code EC;
    std::unique_ptr<ToolOutputFile> Out(
        new ToolOutputFile(OutputFilename, EC, sys::fs::OF_TextWithCRLF));

    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }

    size_t last_slash = OutputFilename.find_last_of("/", -1);
    size_t last_dot = OutputFilename.find_last_of(".", -1);
    assert(last_dot > last_slash);
    printModule(
        M, OutputFilename.substr(last_slash + 1, last_dot - last_slash - 1),
        Out->os());

    // Declare success.
    Out->keep();
  }

  return 0;
}
