//===- OpenCilkBacktrackABI.cpp - Replace Tapir with serial projection ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the OpenCilkBacktrackABI interface, which is used to convert Tapir
// instructions into their serial projection.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Tapir/OpenCilkBacktrackABI.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/TapirTaskInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/Utils/TapirUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Linker/Linker.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include <sstream>
#include <string>
#include <algorithm>
#include <llvm/Support/raw_ostream.h>
#include "llvm/ADT/STLExtras.h"

using namespace llvm;

#define DEBUG_TYPE "opencilkbacktrackabi"
#define USE_INVOKE_FOR_SPAWN 0
#define OUTPUT_SPAWN_LABELS 0
#define SLOW_PATH_NOT_CLONE 0
#define SPAWN_CALLING_CONV CallingConv::PreserveNone


extern cl::opt<bool> DebugABICalls;

#define CILKRTS_FUNC(name) Get__cilkrts_##name()

static cl::opt<bool> UseOpenCilkRuntimeBC(
  "use-opencilk-bt-runtime-bc", cl::init(true),
  cl::desc("Use a bitcode file for the OpenCilk backtrack runtime ABI"), cl::Hidden);

static cl::opt<std::string> ClOpenCilkRuntimeBCPath(
  "opencilk-bt-runtime-bc-path", cl::init(""),
  cl::desc("Path to the bitcode file for the OpenCilk backtrack runtime ABI"),
  cl::Hidden);

class processedSpawn {
public:
  BasicBlock * slow_landing;
  BasicBlock * fast_landing;
  BasicBlock * steal_path;
  BasicBlock * return_path;
  BasicBlock * continuation;
  Function * SpawnHelperFast;
  Function * SpawnHelperSlow;
};

struct CilkRTSFnDesc {
  StringRef FnName;
  FunctionType *FnType;
  FunctionCallee &FnCallee;
};


void OpenCilkBacktrackABI::setOptions(const TapirTargetOptions &Options) {
  if (!isa<OpenCilkABIOptions>(Options))
    return;

  const OpenCilkABIOptions &OptionsCast = cast<OpenCilkABIOptions>(Options);

  // Get the path to the runtime bitcode file.
  RuntimeBCPath = OptionsCast.getRuntimeBCPath();
}


void OpenCilkBacktrackABI::prepareModule() {
  LLVMContext &C = M.getContext();
  Type *Int8Ty = Type::getInt8Ty(C);
  Type *Int16Ty = Type::getInt16Ty(C);
  Type *Int32Ty = Type::getInt32Ty(C);
  Type *Int64Ty = Type::getInt64Ty(C);

  if (UseOpenCilkRuntimeBC) {
    // If a runtime bitcode path is given via the command line, use it.
    if ("" != ClOpenCilkRuntimeBCPath)
      RuntimeBCPath = ClOpenCilkRuntimeBCPath;

    if ("" == RuntimeBCPath)
      C.emitError("OpenCilkBacktrackABI: No OpenCilk bitcode ABI file given.");

    LLVM_DEBUG(dbgs() << "Using external bitcode file for OpenCilk ABI: "
               << RuntimeBCPath << "\n");
    SMDiagnostic SMD;

    // Parse the bitcode file.  This call imports structure definitions, but not
    // function definitions.
    std::unique_ptr<Module> ExternalModule = parseIRFile(RuntimeBCPath, SMD, C);

    if (!ExternalModule)
      C.emitError("OpenCilkBacktrackABI: Failed to parse bitcode ABI file: " +
                  Twine(RuntimeBCPath));

    // Strip any debug info from the external module.  For convenience, this
    // Tapir target synthesizes some helper functions, like
    // __cilk_parent_epilogue, that contain calls to these functions, but don't
    // necessarily have debug info.  As a result, debug info in the external
    // module causes failures during subsequent inlining.
    StripDebugInfo(*ExternalModule);

    // Link the external module into the current module, copying over global
    // values.
    //
    // TODO: Consider restructuring the import process to use
    // Linker::Flags::LinkOnlyNeeded to copy over only the necessary contents
    // from the external module.
    bool Fail = Linker::linkModules(
                  M, std::move(ExternalModule), Linker::Flags::None,
    [](Module & M, const StringSet<> &GVS) {
      for (StringRef GVName : GVS.keys()) {
        LLVM_DEBUG(dbgs() << "Linking global value " << GVName << "\n");
        if (Function *Fn = M.getFunction(GVName)) {
          if (!Fn->isDeclaration())
            // We set the function's linkage as available_externally, so
            // that subsequent optimizations can remove these definitions
            // from the module.  We don't want this module redefining any of
            // these symbols, even if they aren't inlined, because the
            // OpenCilk runtime library will provide those definitions
            // later.
            Fn->setLinkage(Function::AvailableExternallyLinkage);
        }
      }
    });
    if (Fail)
      C.emitError("OpenCilkBacktrackABI: Failed to link bitcode ABI file: " +
                  Twine(RuntimeBCPath));
  }

  // Get or create local definitions of Cilk RTS structure types.
  const char *StackFrameName = "struct.__cilkrts_stack_frame";
  StackFrameTy = StructType::lookupOrCreate(C, StackFrameName);
  WorkerTy = StructType::lookupOrCreate(C, "struct.__cilkrts_worker");

  PointerType *StackFramePtrTy = PointerType::getUnqual(StackFrameTy);
  Type *VoidTy = Type::getVoidTy(C);

  // Define the types of the CilkRTS functions.
  FunctionType *CilkRTSFnTy =
    FunctionType::get(VoidTy, false);
  FunctionType *SlowSpawnFnTy = FunctionType::get(Int8Ty, false);
  FunctionType *Grainsize8FnTy = FunctionType::get(Int8Ty, {Int8Ty}, false);
  FunctionType *Grainsize16FnTy = FunctionType::get(Int16Ty, {Int16Ty}, false);
  FunctionType *Grainsize32FnTy = FunctionType::get(Int32Ty, {Int32Ty}, false);
  FunctionType *Grainsize64FnTy = FunctionType::get(Int64Ty, {Int64Ty}, false);

  // Create an array of CilkRTS functions, with their associated types and
  // FunctionCallee member variables in the OpenCilkBacktrackABI class.
  SmallVector<CilkRTSFnDesc, 1> CilkRTSFunctions({
    {"__cilk_sync", CilkRTSFnTy, CilkSync},
    {"__cilk_sync_nothrow", CilkRTSFnTy, CilkSyncNoThrow},
    {"__cilkrts_pop_frame", CilkRTSFnTy, CilkRTSPopFrame},
    {"__cilkrts_enter_frame", CilkRTSFnTy, CilkRTSEnterFrame},
    {"__cilkrts_mark_slow_spawn", SlowSpawnFnTy, CilkRTSEnterSlowSpawn},
    {"__cilkrts_enable_slow_spawn", CilkRTSFnTy, CilkRTSEnableSlowSpawn},
    {"__cilkrts_leave_slow_spawn", CilkRTSFnTy, CilkRTSLeaveSlowSpawn},
    {"__cilkrts_leave_frame", CilkRTSFnTy, CilkRTSLeaveFrame},
    {"__cilk_parent_epilogue", CilkRTSFnTy, CilkParentEpilogue},
    { "__cilkrts_cilk_for_grainsize_8", Grainsize8FnTy,
      CilkRTSCilkForGrainsize8
    },
    { "__cilkrts_cilk_for_grainsize_16", Grainsize16FnTy,
      CilkRTSCilkForGrainsize16
    },
    { "__cilkrts_cilk_for_grainsize_32", Grainsize32FnTy,
      CilkRTSCilkForGrainsize32
    },
    { "__cilkrts_cilk_for_grainsize_64", Grainsize64FnTy,
      CilkRTSCilkForGrainsize64
    },
  });

  if (UseOpenCilkRuntimeBC) {
    // Add attributes to internalized functions.
    for (CilkRTSFnDesc FnDesc : CilkRTSFunctions) {
      LLVM_DEBUG(dbgs() << "Adding attribute to " << FnDesc.FnName << "\n");
      assert(!FnDesc.FnCallee && "Redefining Cilk function");
      FnDesc.FnCallee = M.getOrInsertFunction(FnDesc.FnName, FnDesc.FnType);
      assert(isa<Function>(FnDesc.FnCallee.getCallee()) &&
             "Cilk function is not a function");
      Function *Fn = cast<Function>(FnDesc.FnCallee.getCallee());

      // Unless we're debugging, mark the function as always_inline.  This
      // attribute is required for some functions, but is helpful for all
      // functions.
      if (!DebugABICalls)
        Fn->addFnAttr(Attribute::AlwaysInline);
      else
        Fn->removeFnAttr(Attribute::AlwaysInline);
    }
  } else if (DebugABICalls) {
    if (StackFrameTy->isOpaque()) {
      // Create a dummy __cilkrts_stack_frame structure, for debugging purposes
      // only.
      StackFrameTy->setBody(Int64Ty);
    }
    // Create declarations of all CilkRTS functions, and add basic attributes to
    // those declarations.
    for (CilkRTSFnDesc FnDesc : CilkRTSFunctions) {
      assert(!FnDesc.FnCallee && "Redefining Cilk function");
      FnDesc.FnCallee = M.getOrInsertFunction(FnDesc.FnName, FnDesc.FnType);
      assert(isa<Function>(FnDesc.FnCallee.getCallee()) &&
             "Cilk function is not a function");
      Function *Fn = cast<Function>(FnDesc.FnCallee.getCallee());

      // Mark all CilkRTS functions nounwind, except for __cilk_sync.
      if ("__cilk_sync" == FnDesc.FnName)
        Fn->removeFnAttr(Attribute::NoUnwind);
      else
        Fn->setDoesNotThrow();
    }
  } else {
    // The OpenCilkBacktrackABI target requires the use of a bitcode ABI file to generate
    // correct code.
    C.emitError(
      "OpenCilkBacktrackABI: Bitcode ABI file required for correct code generation.");
  }
}


/// Lower a call to get the grainsize of a Tapir loop.
Value *OpenCilkBacktrackABI::lowerGrainsizeCall(CallInst *GrainsizeCall) {
  LLVM_DEBUG({dbgs() << "Lowering grainsize call " << *GrainsizeCall << "\n"; dbgs().flush();});
  Value *Limit;
  if (GrainsizeCall->getNumOperands()) {
    Limit = GrainsizeCall->getArgOperand(0);
  } else {
    Limit = ConstantInt::get(GrainsizeCall->getType(), 2048);
  }
  IRBuilder<> Builder(GrainsizeCall);

  // Select the appropriate __cilkrts_grainsize function, based on the type.
  FunctionCallee CilkRTSGrainsizeCall;
  if (GrainsizeCall->getType()->isIntegerTy(8))
    CilkRTSGrainsizeCall = CILKRTS_FUNC(cilk_for_grainsize_8);
  else if (GrainsizeCall->getType()->isIntegerTy(16))
    CilkRTSGrainsizeCall = CILKRTS_FUNC(cilk_for_grainsize_16);
  else if (GrainsizeCall->getType()->isIntegerTy(32))
    CilkRTSGrainsizeCall = CILKRTS_FUNC(cilk_for_grainsize_32);
  else if (GrainsizeCall->getType()->isIntegerTy(64))
    CilkRTSGrainsizeCall = CILKRTS_FUNC(cilk_for_grainsize_64);
  else
    llvm_unreachable("No CilkRTSGrainsize call matches type for Tapir loop.");

  Value *Grainsize = Builder.CreateCall(CilkRTSGrainsizeCall, Limit);

  // Replace uses of grainsize intrinsic call with this grainsize value.
  GrainsizeCall->replaceAllUsesWith(Grainsize);
  return Grainsize;
}


void OpenCilkBacktrackABI::lowerSync(SyncInst &SI) {
}

bool OpenCilkBacktrackABI::preProcessFunction(Function &F, TaskInfo &TI,
                                     bool ProcessingTapirLoops) {
  // if (Processed.contains(&F))
  //   return false;

  // if (ProcessingTapirLoops)
  //   // Don't do any preprocessing when outlining Tapir loops.
  //   return false;

  // // Find all Tapir-runtime calls in this function that may be translated to
  // // enter_frame/leave_frame calls.
  // GetTapirRTCalls(TI.getRootTask()->getEntrySpindle(), true, TI);

  // if (!TI.isSerial() || TapirRTCalls[&F.getEntryBlock()].empty())
  //   return false;

  // MarkSpawner(F);
  // LowerTapirRTCalls(F, &F.getEntryBlock());
  return false;
}

bool inCloneList(BasicBlock * target, std::vector<BasicBlock *> BB_CloneList) {
  for (unsigned int BB_iter = 0; BB_iter < BB_CloneList.size(); BB_iter++) {
    if (target == BB_CloneList[BB_iter]) return true;
  }
  return false;
}

void popFirstInstruction(llvm::BasicBlock *BB) {
    if (!BB->empty()) {
        llvm::Instruction *FirstInst = &*(BB->begin());
        FirstInst->eraseFromParent();
    }
}

void popLastInstruction(llvm::BasicBlock *BB) {
    if (!BB->empty()) {
        llvm::Instruction *LastInst = &*(--BB->end());
        LastInst->eraseFromParent();
    }
}

Function * OpenCilkBacktrackABI::createFastBacktrackSpawnHelper(Function *F, Module *M) {
  LLVMContext &Context = M->getContext();

  // Construct the new function name
  std::string NewFuncName = "__fast_backtrack_spawn_helper_" + F->getName().str();

  // Clone the function type but not the body
  FunctionType *FTy = F->getFunctionType();
  Function *NewFunc = Function::Create(FTy, F->getLinkage(), NewFuncName, M);

  // Copy the function attributes, then set no-omit-frame-pointer
  NewFunc->copyAttributesFrom(F);
  NewFunc->addFnAttr(Attribute::NoInline);
  NewFunc->addFnAttr("no-frame-pointer-elim", "true");
  NewFunc->addFnAttr("frame-pointer", "all");

  // Generate entry block
  BasicBlock *BB = BasicBlock::Create(Context, "entry", NewFunc);

  std::vector<Value *> ArgVals;
  for (Argument &Arg : NewFunc->args())
    ArgVals.push_back(&Arg);

  CallInst::Create(Get__cilkrts_enter_frame(), "", BB);
  CallInst *OriginalCall = CallInst::Create(FTy, F, ArgVals, "", BB);
  OriginalCall->setCallingConv(F->getCallingConv());
  // Create return instruction based on the return type (should be voidtype, but want this in case we add returns later)
  CallInst::Create(Get__cilkrts_leave_frame(), "", BB);

  if (FTy->getReturnType()->isVoidTy()) {
    ReturnInst::Create(Context, BB);
  } else {
    assert(false && "Shouldn't be spawning functions that return non-voidType!");
    ReturnInst::Create(Context, OriginalCall, BB);
  }

  llvm::InlineFunctionInfo ifi;
  llvm::InlineFunction(*OriginalCall, ifi);

  return NewFunc;
}


Function * OpenCilkBacktrackABI::createSlowBacktrackSpawnHelper(Function *F, Module *M) {
  LLVMContext &Context = M->getContext();

  // Construct the new function name
  std::string NewFuncName = "__slow_backtrack_spawn_helper_" + F->getName().str();

  // Clone the function type but not the body
  FunctionType *FTy = F->getFunctionType();
  Function *NewFunc = Function::Create(FTy, F->getLinkage(), NewFuncName, M);

  // Copy the function attributes, then set no-omit-frame-pointer
  NewFunc->copyAttributesFrom(F);
  NewFunc->addFnAttr(Attribute::NoInline);
  NewFunc->addFnAttr("no-frame-pointer-elim", "true");
  NewFunc->addFnAttr("frame-pointer", "all");

  // Generate entry block
  BasicBlock *BB = BasicBlock::Create(Context, "entry", NewFunc);

  std::vector<Value *> ArgVals;
  for (Argument &Arg : NewFunc->args())
    ArgVals.push_back(&Arg);

  CallInst::Create(Get__cilkrts_enable_slow_spawn(), "", BB);
  CallInst *OriginalCall = CallInst::Create(FTy, F, ArgVals, "", BB);
  OriginalCall->setCallingConv(SPAWN_CALLING_CONV);
  // Create return instruction based on the return type (should be voidtype, but want this in case we add returns later)
  CallInst::Create(Get__cilkrts_leave_slow_spawn(), "", BB);

  if (FTy->getReturnType()->isVoidTy()) {
    ReturnInst::Create(Context, BB);
  } else {
    assert(false && "Shouldn't be spawning functions that return non-voidType!");
    ReturnInst::Create(Context, OriginalCall, BB);
  }

  llvm::InlineFunctionInfo ifi;
  llvm::InlineFunction(*OriginalCall, ifi);

  return NewFunc;
}

#define ENABLE_SLOW_SPAWN 0

Function * OpenCilkBacktrackABI::createBacktrackSpawnHelper(Function *F, Module *M, bool is_slow, SmallVectorImpl<Function *> &NewSpawnHelpers) {
  Function *NewFunc = NULL;
  if (!func_helper_map.count(F)) {
    func_helper_map[F] = CilkBacktrackSpawnHelper();
    func_helper_map[F].fast_helper = NULL;
    func_helper_map[F].slow_helper = NULL;
  }

  if (is_slow && ENABLE_SLOW_SPAWN) {
    if (func_helper_map[F].slow_helper == NULL) {
      NewFunc = createSlowBacktrackSpawnHelper(F, M);
      func_helper_map[F].slow_helper = NewFunc;
      NewSpawnHelpers.push_back(NewFunc);
    } else {
      NewFunc = func_helper_map[F].slow_helper;
    }
  } else {
    if (func_helper_map[F].fast_helper == NULL) {
      NewFunc = createFastBacktrackSpawnHelper(F, M);
      func_helper_map[F].fast_helper = NewFunc;
      NewSpawnHelpers.push_back(NewFunc);
    } else {
      NewFunc = func_helper_map[F].fast_helper;
    }
  }
  NewFunc->setCallingConv(SPAWN_CALLING_CONV);
  llvm::verifyFunction(*NewFunc, &(outs()));
  return NewFunc;
}


BasicBlock * OpenCilkBacktrackABI::changeToCallBrAndSplitBasicBlockFast(CallInst *CI,
    BasicBlock *landingPad_BB, BasicBlock *pop_BB, StringRef& functionName, uint spawn_count, const Twine & path, std::vector<BasicBlock *> &BB_SlowCloneUpdateList, SmallVectorImpl<Function *> &NewSpawnHelpers) {

  BasicBlock *BB = CI->getParent();

  // Convert this function call into an invoke instruction.  First, split the
  // basic block.
  BasicBlock *Split =
    BB->splitBasicBlock(CI->getIterator(), CI->getName() + ".noexc");
  popLastInstruction(BB);

  // Delete the unconditional branch inserted by splitBasicBlock

  // Create the new invoke instruction.
  SmallVector<Value *, 8> InvokeArgs(CI->args());
  SmallVector<OperandBundleDef, 1> OpBundles;

  CI->getOperandBundlesAsDefs(OpBundles);

  // Note: we're round tripping operand bundles through memory here, and that
  // can potentially be avoided with a cleverer API design that we do not have
  // as of this time.

  SmallVector<BasicBlock *, 8> IndirectDests({landingPad_BB, pop_BB, Split});

  CallBrInst *II =
    CallBrInst::Create(CI->getFunctionType(), createBacktrackSpawnHelper(CI->getCalledFunction(), CI->getCalledFunction()->getParent(), false, NewSpawnHelpers), Split,
                       IndirectDests, InvokeArgs, OpBundles, CI->getName(), BB);
  II->setDebugLoc(CI->getDebugLoc());
  II->setCallingConv(SPAWN_CALLING_CONV);
  II->setAttributes(CI->getAttributes());

  LLVM_DEBUG(dbgs() << "\tCallBr " << *II << "\n";);
  LLVM_DEBUG(dbgs() << "\t\tConventions " << (II->getCallingConv()) << "\n";);

  // Make sure that anything using the call now uses the invoke!  This also
  // updates the CallGraph if present, because it uses a WeakTrackingVH.
  CI->replaceAllUsesWith(II);

  // Delete the original call
  popFirstInstruction(Split);




  std::ostringstream oss;
  oss << "";
#if OUTPUT_SPAWN_LABELS
  oss << "__CILK_SPAWN_PREFIX__" << functionName.str() << "_" << spawn_count << "_" << path.str() << ":";
#endif

  Type *VoidTy = Type::getVoidTy(BB->getContext());
  InlineAsm* inline_label = InlineAsm::get(FunctionType::get(VoidTy, false), oss.str(), "", true);
  LLVM_DEBUG(dbgs() << "Creating inline asm label " << *inline_label << "\n";);
  CallInst* asm_lbl = CallInst::Create(inline_label->getFunctionType(), inline_label, Twine(), &*(Split->getFirstInsertionPt()));
  return Split;
}


BasicBlock * OpenCilkBacktrackABI::changeToCallBrAndSplitBasicBlockSlow(CallInst *CI,
    BasicBlock *landingPad_BB, BasicBlock *pop_BB, StringRef& functionName, uint spawn_count, const Twine & path, std::vector<BasicBlock *> &BB_SlowCloneUpdateList, SmallVectorImpl<Function *> &NewSpawnHelpers) {

  BasicBlock *BB = CI->getParent();

  // Convert this function call into an invoke instruction.  First, split the
  // basic block.
  BasicBlock *Split =
    BB->splitBasicBlock(CI->getIterator(), CI->getName() + ".noexc");
  popLastInstruction(BB);

  // Delete the unconditional branch inserted by splitBasicBlock

  // Create the new invoke instruction.
  SmallVector<Value *, 8> InvokeArgs(CI->args());
  SmallVector<OperandBundleDef, 1> OpBundles;

  CI->getOperandBundlesAsDefs(OpBundles);

  // Note: we're round tripping operand bundles through memory here, and that
  // can potentially be avoided with a cleverer API design that we do not have
  // as of this time.

  SmallVector<BasicBlock *, 8> IndirectDests({landingPad_BB, pop_BB, Split});

  CallBrInst *II =
    CallBrInst::Create(CI->getFunctionType(), createBacktrackSpawnHelper(CI->getCalledFunction(), CI->getCalledFunction()->getParent(), true, NewSpawnHelpers), Split,
                       IndirectDests, InvokeArgs, OpBundles, CI->getName(), BB);
  II->setDebugLoc(CI->getDebugLoc());
  II->setCallingConv(SPAWN_CALLING_CONV);
  II->setAttributes(CI->getAttributes());

  LLVM_DEBUG(dbgs() << "\tCallBr " << *II << "\n";);
  LLVM_DEBUG(dbgs() << "\t\tConventions " << (II->getCallingConv()) << "\n";);

  // Make sure that anything using the call now uses the invoke!  This also
  // updates the CallGraph if present, because it uses a WeakTrackingVH.
  CI->replaceAllUsesWith(II);

  // Delete the original call
  popFirstInstruction(Split);


  CallInst *EnterCall = CallInst::Create(Get__cilkrts_enter_slow_spawn(), "", II);
  BasicBlock* CallBrBB = BB->splitBasicBlock(II->getIterator(), II->getName() + ".noexc");
  popLastInstruction(BB);
  ICmpInst *CheckEnter = new ICmpInst(*BB, ICmpInst::ICMP_EQ, EnterCall, ConstantInt::get(Type::getInt8Ty(BB->getParent()->getParent()->getContext()), 1), "check_enter");
  BranchInst::Create(landingPad_BB, CallBrBB, CheckEnter, BB);
  EnterCall->setDebugLoc(II->getDebugLoc());
  BB_SlowCloneUpdateList.push_back(CallBrBB);

  std::ostringstream oss;
  oss << "";
#if OUTPUT_SPAWN_LABELS
  oss << "__CILK_SPAWN_PREFIX__" << functionName.str() << "_" << spawn_count << "_" << path.str() << ":";
#endif
  Type *VoidTy = Type::getVoidTy(BB->getContext());
  InlineAsm* inline_label = InlineAsm::get(FunctionType::get(VoidTy, false), oss.str(), "", true);
  LLVM_DEBUG(dbgs() << "Creating inline asm label " << *inline_label << "\n";);
  CallInst* asm_lbl = CallInst::Create(inline_label->getFunctionType(), inline_label, Twine(), &*(Split->getFirstInsertionPt()));
  return Split;
}

BasicBlock * OpenCilkBacktrackABI::changeToCallBrAndSplitBasicBlock(CallInst *CI,
    BasicBlock *landingPad_BB, BasicBlock *pop_BB, StringRef& functionName, uint spawn_count, const Twine & path, bool is_slow, std::vector<BasicBlock *> & BB_SlowCloneUpdateList, SmallVectorImpl<Function *> &NewSpawnHelpers) {
  if (is_slow && ENABLE_SLOW_SPAWN) {
    return changeToCallBrAndSplitBasicBlockSlow(CI, landingPad_BB, pop_BB, functionName, spawn_count, path, BB_SlowCloneUpdateList, NewSpawnHelpers);
  } else {
    return changeToCallBrAndSplitBasicBlockFast(CI, landingPad_BB, pop_BB, functionName, spawn_count, path, BB_SlowCloneUpdateList, NewSpawnHelpers);
  }
}


BasicBlock* OpenCilkBacktrackABI::generateInvokeFlow(BasicBlock* BB, BasicBlock* stealPath_BB, BasicBlock* pop_BB, StringRef& functionName, uint spawn_count, const Twine & path, bool is_slow, CallInst * targetCI, std::vector<BasicBlock *> &BB_SlowCloneUpdateList, SmallVectorImpl<Function *> &NewSpawnHelpers) {
  Function* F = BB->getParent();
  BasicBlock * landingPad_BB = BasicBlock::Create(BB->getContext(), "landingpad_bb", BB->getParent());

#if USE_INVOKE_FOR_SPAWN
  F->removeFnAttr(Attribute::NoUnwind);
  if (!F->hasPersonalityFn()) {
    FunctionCallee PersFn = getDefaultPersonalityFn(F->getParent());
    F->setPersonalityFn(cast<Constant>(PersFn.getCallee()));
  }
  LandingPadInst *LPadInst = LandingPadInst::Create(IntegerType::get(BB->getContext(), 32), 0, "", landingPad_BB);
  //LPadInst->addClause(Constant::getNullValue(IntegerType::get(BB->getContext(), 32)));
  LPadInst->setCleanup(true);
#endif
  BranchInst * brInst = BranchInst::Create(stealPath_BB, landingPad_BB);
#if USE_INVOKE_FOR_SPAWN
  changeToInvokeAndSplitBasicBlock(targetCI, landingPad_BB);
#else
  changeToCallBrAndSplitBasicBlock(targetCI, landingPad_BB, pop_BB, functionName, spawn_count, path, is_slow, BB_SlowCloneUpdateList, NewSpawnHelpers);
#endif


  return landingPad_BB;
}

bool vectorContainsPtr(SmallVectorImpl<BasicBlock *> &PHIBlocks, BasicBlock * BB) {
  for (BasicBlock * BB_check : PHIBlocks) {
    if (BB_check == BB) return true;
  }
  return false;
}

void generateNameDot(Function &F, std::vector<Instruction *> &definitions, Instruction * fast_I, Instruction * slow_I, std::vector<User *> &slow_users, DominatorTree& updateddominatorTree) {
  LLVM_DEBUG(dbgs() << "\t\t-- NAME DOT FILE --  " << fast_I->getNameOrAsOperand() << "\n";);
  LLVM_DEBUG(dbgs() << "digraph \"NameDot for " << *fast_I << "\"{" <<  "\n";);
  int last_index = 0;
  for (BasicBlock & BB : F) {
    bool use = false;
    std::string BB_name = BB.getName().str();
    std::replace(BB_name.begin(), BB_name.end(), '.', '_');
    std::replace(BB_name.begin(), BB_name.end(), '-', '_');
    LLVM_DEBUG(dbgs() << BB_name);
    LLVM_DEBUG(dbgs() << " [style=filled, label=\"" << BB.getName().str() << "\\n";);
    for (Instruction &I : BB) {
      std::string str;
      llvm::raw_string_ostream(str) << I;
      if (str.find('\n') != std::string::npos) {
        str.replace(str.find('\n'), 1, "\\n");
      }
      while (str.find('"') != std::string::npos) {
        str.replace(str.find('"'), 1, "\'");
      }
      if (std::count(slow_users.begin(), slow_users.end(), (User *) &I) || &I == slow_I) {
        use = true;
        LLVM_DEBUG(dbgs() << str << "[");
        Instruction* least_dominant = fast_I;
        for (Instruction* definition : definitions) {
          if (updateddominatorTree.dominates(definition, &I)) {
            if (updateddominatorTree.dominates(least_dominant, definition)) {
              least_dominant = definition;
            }
          }
        }
        last_index = std::find(definitions.begin(), definitions.end(), least_dominant) - definitions.begin();
        LLVM_DEBUG(dbgs() << least_dominant->getNameOrAsOperand());
        LLVM_DEBUG(dbgs() << "]\\n");
      }
    }
    LLVM_DEBUG(dbgs() <<  "\",fillcolor=";);
    if (use) {
      char color_code[20];
      sprintf(color_code, "#%06x", (0x555555) + last_index * ((0xAAAAAA) / definitions.size()));
      LLVM_DEBUG(dbgs() <<  "\"" << color_code << "\"";);
    } else {
      LLVM_DEBUG(dbgs() <<  "grey";);
    }
    LLVM_DEBUG(dbgs() << "]\n");
  }
  for (BasicBlock & BB : F) {
    std::string BB_name = BB.getName().str();
    std::replace(BB_name.begin(), BB_name.end(), '.', '_');
    std::replace(BB_name.begin(), BB_name.end(), '-', '_');
    for (BasicBlock * Pred_BB : predecessors(&BB)) {
      std::string pred_name = Pred_BB->getName().str();
      std::replace(pred_name.begin(), pred_name.end(), '.', '_');
      std::replace(pred_name.begin(), pred_name.end(), '-', '_');
      LLVM_DEBUG(dbgs() << pred_name << " -> " << BB_name << "\n";);
    }
  }
  LLVM_DEBUG(dbgs() << "}" <<  "\n";);
  LLVM_DEBUG(dbgs() << "\t\t-- END NAME DOT FILE --  " << fast_I->getName() << "\n";);
}


void generateDefDot(Function & F, SmallPtrSetImpl<BasicBlock *> &DefBlocks, SmallPtrSetImpl<BasicBlock *> &LiveInBlocks, Instruction * fast_I, Instruction * slow_I, SmallVectorImpl<BasicBlock *> &PHIBlocks) {
  LLVM_DEBUG(dbgs() << "\t\t-- DOT FILE --  " << fast_I->getNameOrAsOperand() << "\n";);
  LLVM_DEBUG(dbgs() << "digraph \"DefDot for " << *fast_I << "\"{" <<  "\n";);
  LLVM_DEBUG(dbgs() << "subgraph cluster_legend {label=\"Legend\";labelloc=top;fontsize=12;Def  [label=\"Definition\", style=filled, fillcolor=green];Phi  [label=\"Phi\",        style=filled, fillcolor=red];LivePhi  [label=\"Live Phi\",        style=filled, fillcolor=purple];Live [label=\"Live\",       style=filled, fillcolor=blue];Not  [label=\"Not\",        style=filled, fillcolor=grey];}";);
  for (BasicBlock & BB : F) {
    std::string BB_name = BB.getName().str();
    std::replace(BB_name.begin(), BB_name.end(), '.', '_');
    std::replace(BB_name.begin(), BB_name.end(), '-', '_');
    LLVM_DEBUG(dbgs() << BB_name);
    LLVM_DEBUG(dbgs() << " [style=filled, label=\"" << BB.getName() << "\\n{\\n";);
    if (fast_I->getParent() == &BB) LLVM_DEBUG(dbgs() << *fast_I << "\\n";);
    if (slow_I->getParent() == &BB) LLVM_DEBUG(dbgs() << *slow_I << "\\n";);
    LLVM_DEBUG(dbgs() << "}{" << &BB << "}\\n";);
    LLVM_DEBUG(dbgs() <<  "}\",fillcolor=";);
    if (DefBlocks.count(&BB)) {
      LLVM_DEBUG(dbgs() <<  "green";);
    } else if (vectorContainsPtr(PHIBlocks, &BB) && LiveInBlocks.count(&BB)) {
      LLVM_DEBUG(dbgs() <<  "purple";);
    }
      else if (vectorContainsPtr(PHIBlocks, &BB)) {
      LLVM_DEBUG(dbgs() <<  "red";);
    }
    else if (LiveInBlocks.count(&BB)) {
      LLVM_DEBUG(dbgs() <<  "blue";);
    } else {
      LLVM_DEBUG(dbgs() <<  "grey";);
    }
    LLVM_DEBUG(dbgs() << "]\n");
  }
  for (BasicBlock & BB : F) {
    std::string BB_name = BB.getName().str();
    std::replace(BB_name.begin(), BB_name.end(), '.', '_');
    std::replace(BB_name.begin(), BB_name.end(), '-', '_');
    for (BasicBlock * Pred_BB : predecessors(&BB)) {
      std::string pred_name = Pred_BB->getName().str();
      std::replace(pred_name.begin(), pred_name.end(), '.', '_');
      std::replace(pred_name.begin(), pred_name.end(), '-', '_');
      LLVM_DEBUG(dbgs() << pred_name << " -> " << BB_name << "\n";);
    }
  }
  LLVM_DEBUG(dbgs() << "}" <<  "\n";);
  LLVM_DEBUG(dbgs() << "\t\t-- END DOT FILE --  " << fast_I->getName() << "\n";);
}

BasicBlock* getCorrespondingBBFromFastSlowCloneLists(BasicBlock * target, std::vector<BasicBlock *> BB_FastCloneList, std::vector<BasicBlock *>BB_SlowCloneList) {
  for (unsigned int BB_iter = 0; BB_iter < BB_FastCloneList.size(); BB_iter++) {
    if (target == BB_FastCloneList[BB_iter]) return BB_SlowCloneList[BB_iter];
    if (target == BB_SlowCloneList[BB_iter]) return BB_FastCloneList[BB_iter];
  }
  return target;
}

BasicBlock * splitAndModifyDetatch(BasicBlock * BB, CallInst* OriginalCI) {
  llvm::BasicBlock::iterator I = BB->getFirstInsertionPt();
  CallInst *CI = NULL;
  for (; & (*I) != BB->getTerminator(); I++) {
    if (& (*I) == OriginalCI) {
      CI = (CallInst* ) & (*I);
      if (CI->getIntrinsicID() != Intrinsic::not_intrinsic) {

      } else {
        CI->setTailCall(false);
        LLVM_DEBUG(dbgs() << "\t\tCall " << *CI << "\n";);
        LLVM_DEBUG(dbgs() << "\t\tCall_arg_begin " << CI->arg_begin() << "\n";);
        LLVM_DEBUG(dbgs() << "\t\tCall_arg_end " << CI->arg_end() << "\n";);
        I++;
        return BB->splitBasicBlock(I, Twine(BB->getName()).concat(".return"));
        break;
      }
    }

    LLVM_DEBUG(dbgs() << "\tInstruction " << *I << "\n";);
    LLVM_DEBUG(dbgs() << "\t\tType " << I->getOpcodeName() << "\n";);
  }
  return NULL;
}


// Liveness check from PromoteMemoryToRegister
void getLiveBlocks(Instruction * slow_inst, SmallPtrSetImpl<BasicBlock *> &LiveInBlocks, SmallPtrSetImpl<BasicBlock *> &DefBlocks, DominatorTree & DT, std::vector<BasicBlock *> &BB_SlowCloneList) {
  // To determine liveness, we must iterate through the predecessors of blocks
  // where the def is live.  Blocks are added to the worklist if we need to
  // check their predecessors.  Start with all the using blocks.
  std::vector<BasicBlock *> LiveInBlockWorklist;
  for (llvm::User *U : slow_inst->users()) {
    for (BasicBlock * BB : BB_SlowCloneList) {
      if (!isa<Instruction>(U)) continue;
      Instruction *I = dyn_cast<Instruction>(U);
      if (isa<PHINode>(I)) {
        PHINode * PhiI = dyn_cast<PHINode>(I);
        for (BasicBlock* predecessor : PhiI->blocks()) {
          if (PhiI->getIncomingValueForBlock(predecessor) != slow_inst) continue;
          if (!is_contained(LiveInBlockWorklist, predecessor)) LiveInBlockWorklist.push_back(predecessor);
        }
      } else {
        if ((I->getParent() == BB) && !is_contained(LiveInBlockWorklist, BB)) LiveInBlockWorklist.push_back(BB);
      }
    }
  }


  // If any of the using blocks is also a definition block, check to see if the
  // definition occurs before or after the use.  If it happens before the use,
  // the value isn't really live-in.
  for (unsigned i = 0, e = LiveInBlockWorklist.size(); i != e; ++i) {
    BasicBlock *BB = LiveInBlockWorklist[i];
    if (!DefBlocks.count(BB))
      continue;
    // Okay, this is a block that both uses and defines the value.  If the first
    // reference to the value is as an operand, we know it's live-in
    for (BasicBlock::iterator I = BB->begin();; ++I) {
      bool isUsed = false;
      for (unsigned operand_num = 0, num_operands = I->getNumOperands(); operand_num != num_operands; ++operand_num) {
        if (I->getOperand(operand_num) == slow_inst) {
          isUsed = true;
          break;
        }
      }
      if (isUsed) break;
      if (slow_inst == dyn_cast<Instruction>(I)) {
        // We found a definition before a use.
        LiveInBlockWorklist[i] = LiveInBlockWorklist.back();
        LiveInBlockWorklist.pop_back();
        --i;
        --e;
        break;
      }
    }
  }
  //LLVM_DEBUG(dbgs() << "\n\nLIVE INSTRUCTION " << * slow_inst << "\n\n";);

  // Now that we have a set of blocks where the phi is live-in, recursively add
  // their predecessors until we find the full region the value is live.
  while (!LiveInBlockWorklist.empty()) {
    BasicBlock *BB = LiveInBlockWorklist.back();
    LiveInBlockWorklist.pop_back();

    // If the block isn't reachable from entry, we don't care about it.
    if (!DT.isReachableFromEntry(BB))
      continue;

    // The block really is live in here, insert it into the set.  If already in
    // the set, then it has already been processed.
    if (!LiveInBlocks.insert(BB).second)
      continue;
    //LLVM_DEBUG(dbgs() << "in block " << BB->getName() << (DT.dominates(slow_inst, BB) ? " -- dominated" : "-- not dominated") << " \n";);

    // Since the value is live into BB, it is either defined in a predecessor or
    // live into it to.  Add the preds to the worklist unless they are a
    // defining block.
    for (BasicBlock *P : predecessors(BB)) {
      // The value is not live into a predecessor if it defines the value.
      if (DefBlocks.count(P))
        continue;
      // Otherwise it is, add to the worklist.
      LiveInBlockWorklist.push_back(P);
    }
  }
}

/*  Backtracking Lowering Pass High Level Description
 *  Block Gathering Phase
 *      During the gathering phase we remove all of the detatch instructions and collect them to process later.
 *      Additionally, we create a list of the fast clone blocks.
 *  Cloning Phase
 *      During the cloning phase we create a clone of each basic block and add it to the slow clone block list.
 *      Additionally, we remap all instructions to use the cloned values instead of the originals.
 *      I.E. %sum.slow_clone = add %x, %y becomes %sum.slow_clone = add %x.slow_clone, %y.slow_clone
 *  Create PopPad
 *      This isn't really a phase, we just create a single poppad for the entire function, as the poppad just jumps back to the runtime
 *      so all spawns can use the same poppad.
 *  Detatch Phase
 *      In this phase we promote all of the detatches to CallBr and create the various spawn pads.
 *      For each detatch, we promote the fast and slow clone to a CallBr, linking them to the poppad and their corresponding spawn pad.
 *      Additionally, we add the return pad as an indirect target for each CallBr so that the compiler leaves that basic block and we can extract the address.
 *      There are also a couple of splits done on basic blocks to accomadate this process.
 *  Value Collection Phase
 *      In this phase, we go through each instruction in the fast clone and collect it if it dominates any spawnpad slow clone.
 *      If it dominates a spawn pad that means it must be accessible from the slow clone.
 *      Additionally, if the slow value definition is unreachable, then we just remap everything to the fast value.
 *  Value Processing Phase
 *    This phase goes through each collected value and first computes the iterated dominance frontier for the fast/slow definitions.
 *    It then creates a PHI-node at each block in the dominance frontier, and adds this to a collection of definitions.
 *    For each use of the slow value in the slow clone, it then finds the closest dominating definition and uses that.
 *    Currently the closest dominating definition is located by finding the dominating definition which isn't dominated by any others.
 *  Cleanup Phase
 *    This phase does the final updates to the slow clone, converting all syncs into actual syncs and returns into cilk returns.
 *    It then removes all unreachable blocks.
 *
 */

void OpenCilkBacktrackABI::postProcessFunction(Function & F, SmallVectorImpl<Function *> &NewSpawnHelpers,
    bool ProcessingTapirLoops = false) {
  StringRef functionName(F.getName());
  if (ProcessingTapirLoops)
    // Don't do any postprocessing when outlining Tapir loops.
    return;
  ValueToValueMapTy VMap;
  std::vector<BasicBlock *> BB_FastCloneList = {};
  std::vector<BasicBlock *> BB_SlowCloneList = {};
  std::vector<BasicBlock *> BB_SlowCloneUpdateList = {};
  std::vector<CallInst *> toDetatch = {};
  /*
   *  Gathering Phase
   *      During the gathering phase we remove all of the detatch instructions and collect them to process later.
   *      Additionally, we create a list of the fast clone blocks.
   */


#if SLOW_PATH_NOT_CLONE
  IRBuilder<> Builder(&F.getEntryBlock());
  AllocaInst *spawnFlag = Builder.CreateAlloca(Type::getInt32Ty(F.getContext()), 0, "spawnFlag");
#endif

  DominatorTree initDominatorTree(F);

  for (Function *H : NewSpawnHelpers) {
    if (!H) continue;
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (CallInst *CI = dyn_cast<CallInst>(&I)) {
          if (CI->getCalledFunction() == H) {
            CI->setCallingConv(SPAWN_CALLING_CONV );
            CI->getCalledFunction()->setCallingConv(SPAWN_CALLING_CONV );
            toDetatch.push_back(CI);
          }
        }
      }
    }
  }
  while (NewSpawnHelpers.size()) {
    NewSpawnHelpers.pop_back();
  }

  if (toDetatch.empty()) return;

  for (BasicBlock &BB : F) {
    BB_FastCloneList.push_back(&BB);
  }

  /*
   *  Cloning Phase
   *      During the cloning phase we create a clone of each basic block and add it to the slow clone block list.
   *      Additionally, we remap all instructions to use the cloned values instead of the originals.
   *      I.E. %sum.slow_clone = add %x, %y becomes %sum.slow_clone = add %x.slow_clone, %y.slow_clone
   */

  for (BasicBlock *BB : BB_FastCloneList) {
    LLVM_DEBUG(dbgs() << "Cloning " << *BB << "\n";);
    BasicBlock * CBB = CloneBasicBlock(BB, VMap, ".slow_clone", &F);
    LLVM_DEBUG(dbgs() << "Cloned " << *CBB << "\n";);
    BB_SlowCloneList.push_back(CBB);
    BB_SlowCloneUpdateList.push_back(CBB);
    VMap[BB] = CBB;
  }


  for (BasicBlock *SBB : BB_SlowCloneList) {
    for (Instruction &I : *SBB) {
      LLVM_DEBUG(dbgs() << "Remapping " << I << "\n";);
      RemapInstruction(&I, VMap, RemapFlags::RF_IgnoreMissingLocals | RemapFlags::RF_NoModuleLevelChanges);
    }
  }

  /*
   *  Create PopPad
   *      This isn't really a phase, we just create a single poppad for the entire function, as the poppad just jumps back to the runtime
   *      so all spawns can use the same poppad.
   */

  BasicBlock* pop_BB = BasicBlock::Create(F.getContext(), "pop_frame_to_runtime", &F);
  CallInst::Create(GetCilkRTSPopFrame(), "", pop_BB);
  BranchInst::Create(pop_BB, pop_BB);


  /*
   *  Detatch Phase
   *      In this phase we promote all of the detatches to CallBr and create the various landing pads.
   *      For each detatch, we promote the fast and slow clone to a CallBr, linking them to the poppad and their corresponding spawn pad.
   *      Additionally, we add the return pad as an indirect target for each CallBr so that the compiler leaves that basic block and we can extract the address.
   *      There are also a couple of splits done on basic blocks to accomadate this process.
   */
  std::vector<processedSpawn *> processedSpawns;
  uint spawn_count = 0;
  for (CallInst* Original_CI : toDetatch) {
#if SLOW_PATH_NOT_CLONE
    BasicBlock *BB = Original_CI->getParent();
    BasicBlock *SetFlagBB = BasicBlock::Create(F.getContext(), "setFlag", &F);
    new StoreInst(ConstantInt::get(Type::getInt32Ty(F.getContext()), 1), spawnFlag, SetFlagBB);
    BasicBlock* fast_landing = generateInvokeFlow(BB, SetFlagBB, pop_BB, functionName, spawn_count, "fast", false, Original_CI, BB_SlowCloneUpdateList, NewSpawnHelpers);
    BranchInst::Create(fast_landing, SetFlagBB);
    processedSpawn * pspawn = new processedSpawn();
    pspawn->slow_landing = NULL;
    pspawn->fast_landing = fast_landing;
    {
      Type *VoidTy = Type::getVoidTy(BB->getContext());
      InlineAsm* inline_label = InlineAsm::get(FunctionType::get(VoidTy, false), "", "", true);
      CallInst* asm_lbl = CallInst::Create(inline_label->getFunctionType(), inline_label, Twine(), &*(fast_landing->getFirstInsertionPt()));
    }
    pspawn->steal_path = SetFlagBB;
    processedSpawns.push_back(pspawn);
    spawn_count++;
#else
    CallInst* Clone_CI = dyn_cast<CallInst>(VMap[Original_CI]);
    BasicBlock *BB = Original_CI->getParent();
    // LLVM_DEBUG(dbgs() << "Attempting Detatch for " << * BB << "\n";);
    BasicBlock * BB_corr = getCorrespondingBBFromFastSlowCloneLists(BB, BB_FastCloneList, BB_SlowCloneList);
    // LLVM_DEBUG(dbgs() << "Splitting " << * BB_corr << "\n";);
    BasicBlock* BB_corr_split = splitAndModifyDetatch(BB_corr, Clone_CI);
    // LLVM_DEBUG(dbgs() << "Created " << * BB_corr_split << "\n";);
    BB_SlowCloneUpdateList.push_back(BB_corr_split);
    RemapInstruction(BB_corr_split->getTerminator(), VMap, RemapFlags::RF_IgnoreMissingLocals | RemapFlags::RF_NoModuleLevelChanges);
    BasicBlock* BB_det_cont = BB_corr_split->getSingleSuccessor();
    BasicBlock* BB_continuation = SplitEdge(BB_corr_split, BB_det_cont);
    // LLVM_DEBUG(dbgs() << "Split that edge into " << * BB_continuation << "\n";);
    BB_SlowCloneUpdateList.push_back(BB_continuation);
    BasicBlock* stealPath_BB = CloneBasicBlock(BB_corr_split, VMap, ".steal_path", &F);
    BB_SlowCloneUpdateList.push_back(stealPath_BB);
    // LLVM_DEBUG(dbgs() << "Attempting Invoke Generation for " << * BB << "\n";);
    BasicBlock* slow_landing = generateInvokeFlow(BB_corr, stealPath_BB, pop_BB, functionName, spawn_count, "slow", true, Clone_CI, BB_SlowCloneUpdateList, NewSpawnHelpers);
    BB_SlowCloneUpdateList.push_back(slow_landing);
    BasicBlock* fast_landing = generateInvokeFlow(BB, stealPath_BB, pop_BB, functionName, spawn_count, "fast", false, Original_CI, BB_SlowCloneUpdateList, NewSpawnHelpers);
    // RemapInstruction(BB_corr_split->getTerminator(), VMap, RemapFlags::RF_IgnoreMissingLocals | RemapFlags::RF_NoModuleLevelChanges);
    // RemapInstruction(stealPath_BB->getTerminator(), VMap, RemapFlags::RF_IgnoreMissingLocals | RemapFlags::RF_NoModuleLevelChanges);
    processedSpawn * pspawn = new processedSpawn();
    pspawn->slow_landing = slow_landing;
    {
      Type *VoidTy = Type::getVoidTy(BB->getContext());
      InlineAsm* inline_label = InlineAsm::get(FunctionType::get(VoidTy, false), "", "", true);
      CallInst* asm_lbl = CallInst::Create(inline_label->getFunctionType(), inline_label, Twine(), &*(slow_landing->getFirstInsertionPt()));
    }
    pspawn->fast_landing = fast_landing;
    {
      Type *VoidTy = Type::getVoidTy(BB->getContext());
      InlineAsm* inline_label = InlineAsm::get(FunctionType::get(VoidTy, false), "", "", true);
      CallInst* asm_lbl = CallInst::Create(inline_label->getFunctionType(), inline_label, Twine(), &*(fast_landing->getFirstInsertionPt()));
    }
    pspawn->steal_path = stealPath_BB;
    pspawn->return_path = BB_corr_split;


    pspawn->continuation = BB_continuation;
    processedSpawns.push_back(pspawn);
    spawn_count++;
#endif
  }
  DominatorTree dominatorTree(F);

  #if SLOW_PATH_NOT_CLONE
  #else
  /*
   *  Value Collection Phase
   *      In this phase, we go through each instruction in the fast clone and collect it if it dominates any spawnpad slow clone.
   *      If it dominates a spawn pad that means it must be accessible from the slow clone.
   *      Additionally, if the slow value definition is unreachable, then we just remap everything to the fast value.
   */
  //First we go through all of the fast clones and check whether they have instructions which dominate a spawn point.
  std::vector<Instruction *> fast_to_port;
  for (BasicBlock *FBB : BB_FastCloneList) {
    for (Instruction &I : *FBB) {
      if (I.getType()->isVoidTy()) continue;
      if (!I.getType()->isSingleValueType()) continue;
      Value * slow_val = VMap[&I];
      Instruction * slow_I = dyn_cast<Instruction>(slow_val);

      //If the slow instruction is unreachable from entry, we can just replace all uses in the slow clone with the fast version.
      if (!dominatorTree.isReachableFromEntry(slow_I->getParent())) {
        LLVM_DEBUG(dbgs() << "UNREACHABLE REMAP PASS " << *slow_I << "\n";);
        ValueToValueMapTy phiVMap;
        phiVMap[slow_I] = &I;
        for (BasicBlock *live_BB : BB_SlowCloneUpdateList) {
          LLVM_DEBUG(dbgs() << "\tBlock " << live_BB->getName() << "\n";);
          for (Instruction &remapI : *live_BB) {
            LLVM_DEBUG(dbgs() << "\t\tRemaps " << remapI << "\n";);
            RemapInstruction(&remapI, phiVMap, RemapFlags::RF_IgnoreMissingLocals | RemapFlags::RF_NoModuleLevelChanges);
            LLVM_DEBUG(dbgs() << "\t\tTo " << remapI << "\n";);
          }
        }
      } else {
        for (processedSpawn* pspawn : processedSpawns) {
          if (dominatorTree.dominates(&I, &*(pspawn->fast_landing->getFirstInsertionPt()))) {
            fast_to_port.push_back(&I);
            break;
          }
        }
      }
    }
  }
  for (Instruction * I : fast_to_port) {
    DominatorTree dominatorTree2(F);
    Value * slow_val = VMap[I];
    Instruction * slow_I = dyn_cast<Instruction>(slow_val);


    bool is_dominated_by_slow_def = true;
    for (User * slow_user : slow_I->users()) {
      if (isa<Instruction>(slow_user)) {
        Instruction * slow_user_I = dyn_cast<Instruction>(slow_user);
        if (isa<PHINode>(slow_user)) {
          PHINode* slow_phi = dyn_cast<PHINode>(slow_user);
          for (BasicBlock* predecessor : slow_phi->blocks()) {
            if (slow_phi->getIncomingValueForBlock(predecessor) != slow_val) continue;
            if (!dominatorTree2.dominates(slow_I, predecessor) && !(slow_I->getParent() == predecessor)) {
              LLVM_DEBUG(dbgs() << "Slow instruction " << *slow_I << " doesn't dominate phi predecessor " << predecessor->getName() << "\n";);
              is_dominated_by_slow_def = false;
              break;
            }
          }
        } else {
          if (!dominatorTree2.dominates(slow_I, slow_user_I)) {
            LLVM_DEBUG(dbgs() << "Slow instruction " << *slow_I << " doesn't dominate use " << *slow_user_I << "\n";);
            is_dominated_by_slow_def = false;
            break;
          }

        }
      }
    }
    if (is_dominated_by_slow_def) continue;

    LLVM_DEBUG(dbgs() << "Instruction dominates fast landing " << *I << "\n";);
    SmallPtrSet<BasicBlock *, 32> DefBlocks;
    DefBlocks.insert(slow_I->getParent());
    DefBlocks.insert(I->getParent());
    SmallPtrSet<BasicBlock *, 32> LiveInBlocks;
    getLiveBlocks(slow_I, LiveInBlocks, DefBlocks, dominatorTree2, BB_SlowCloneList);
    ForwardIDFCalculator IDF(dominatorTree2);
    IDF.setLiveInBlocks(LiveInBlocks);
    IDF.setDefiningBlocks(DefBlocks);
    SmallVector<BasicBlock *, 32> PHIBlocks;
    IDF.calculate(PHIBlocks);
    std::vector<Instruction *> definitions = {I, slow_I};
    generateDefDot(F, DefBlocks, LiveInBlocks, I, slow_I, PHIBlocks);
    for (BasicBlock * PHI_BB : PHIBlocks) {
      LLVM_DEBUG(dbgs() << "IDF found " << PHI_BB->getName() << "\n";);
      PHINode * MergePHI = PHINode::Create(I->getType(), 2, Twine(I->getName()).concat(".mergephi"), &*PHI_BB->getFirstInsertionPt());
      for (BasicBlock * Pred_BB : predecessors(PHI_BB)) {
        MergePHI->addIncoming(slow_I, Pred_BB);
      }
      LLVM_DEBUG(dbgs() << "PHI CREATE " << *MergePHI << "\n";);
      definitions.push_back(MergePHI);
    }
    std::vector<User *> slow_users;
    for (User * slow_user : slow_I->users()) {
      slow_users.push_back(slow_user);
    }
    for (User * slow_user : slow_users) {
      DominatorTree updateddominatorTree(F);

      //LLVM_DEBUG(dbgs() << "Updating User " << *slow_user << "\n";);
      if (isa<PHINode>(slow_user)) {
        PHINode* slow_phi = dyn_cast<PHINode>(slow_user);
        for (BasicBlock* predecessor : slow_phi->blocks()) {
          if (!updateddominatorTree.isReachableFromEntry(predecessor)) continue;
          if (slow_phi->getIncomingValueForBlock(predecessor) != slow_val) continue;
          Instruction* least_dominant = NULL;
          LLVM_DEBUG(dbgs() << "Finding least dominant pred for " << *slow_phi << "from predecessor " << predecessor->getName() << "\n";);
          for (Instruction* definition : definitions) {
            if (definition->getParent() == predecessor || updateddominatorTree.dominates(definition, predecessor)) {
              if (least_dominant) {
                if (updateddominatorTree.dominates(least_dominant, definition)) {
                  least_dominant = definition;
                }
              } else {
                least_dominant = definition;
              }
            }
          }
          if (!least_dominant) {
            LLVM_DEBUG(dbgs() << "Issue finding least dominant for " << *slow_phi << "\n";);
            LLVM_DEBUG(dbgs() << "LiveInBlocks [";);
            for (BasicBlock* liveBB : LiveInBlocks) {
              LLVM_DEBUG(dbgs() << liveBB->getName() << ", ";);
            }
            LLVM_DEBUG(dbgs() << "DefBlocks [";);
            for (BasicBlock* defBB : DefBlocks) {
              LLVM_DEBUG(dbgs() << defBB->getName() << ", ";);
            }
            LLVM_DEBUG(dbgs() << "]\nDefinitions [";);
            for (Instruction* definition : definitions) {
              LLVM_DEBUG(dbgs() << definition->getNameOrAsOperand() << " in block " << definition->getParent()->getName() << ", ";);
            }
            LLVM_DEBUG(dbgs() << "]\nIssue Current IR " << F << "\n";);
          }
          assert(least_dominant && "No Least Dominant Predecessor for PHI!");
          assert((least_dominant->getParent() == predecessor || updateddominatorTree.dominates(least_dominant, predecessor)) && "Least Dominant doesn't dominate predecessor!");
          LLVM_DEBUG(dbgs() << "Found least dominant pred for " << *slow_phi << " as " << *least_dominant << "\n";);
          slow_phi->setIncomingValueForBlock(predecessor, least_dominant);
        }
      } else if (isa<Instruction>(slow_user)) {
        Instruction* least_dominant = NULL;
        LLVM_DEBUG(dbgs() << "Finding least dominant pred for " << *slow_user << "\n";);
        for (Instruction* definition : definitions) {
          if (updateddominatorTree.dominates(definition, dyn_cast<Instruction>(slow_user))) {
            if (least_dominant) {
              if (updateddominatorTree.dominates(least_dominant, definition)) {
                least_dominant = definition;
              }
            } else {
              least_dominant = definition;
            }
          }
        }
        if (!least_dominant) F.dump();
        assert(least_dominant && "No Least Dominant Definition for use!");
        assert(updateddominatorTree.dominates(least_dominant, dyn_cast<Instruction>(slow_user)) && "Least Dominant Doesn't Dominate!");
        LLVM_DEBUG(dbgs() << "Found least dominant pred for " << *slow_user << " as " << *least_dominant << "\n";);
        ValueToValueMapTy phiVMap;
        phiVMap[slow_I] = least_dominant;
        RemapInstruction(dyn_cast<Instruction>(slow_user), phiVMap, RemapFlags::RF_IgnoreMissingLocals | RemapFlags::RF_NoModuleLevelChanges);
      }
      //generateNameDot(F, definitions, I, slow_I, slow_users, updateddominatorTree);
    }
  }
  #endif
  /*
   *  Cleanup Phase
   *    This phase does the final updates to the slow clone, converting all syncs into actual syncs and returns into cilk returns.
   *    It then removes all unreachable blocks.
   */

  for (BasicBlock *SBB : BB_SlowCloneUpdateList) {
    if (!dominatorTree.isReachableFromEntry(SBB)) {
      continue;
    }
    LLVM_DEBUG(dbgs() << "\nUpdating sync/return for " << SBB->getName() << "\n";);
    Instruction* I = SBB->getTerminator();
    if (isa<SyncInst>(I)) {
      LLVM_DEBUG(dbgs() << "Updating Sync Inst\n\t" << *I << "\n";);
      SyncInst* SI = dyn_cast<SyncInst>(I);

      BasicBlock *SyncCont = SI->getSuccessor(0);
      // Determine whether a sync.unwind immediately follows SI.

      CallBase *CB;
      if (F.doesNotThrow())
        CB = CallInst::Create(GetCilkSyncNoThrowFn(), "",
                              /*insert before*/ SI);
      else
        CB = CallInst::Create(GetCilkSyncFn(), "", /*insert before*/ SI);

      BranchInst::Create(SyncCont, CB->getParent());
      CB->setDebugLoc(SI->getDebugLoc());
      LLVM_DEBUG(dbgs() << "to\n\t" << *CB << "\n";);
      SI->eraseFromParent();
    } else if (isa<ReturnInst>(I)) {
      CallBase *Epilogue = CallInst::Create(GetCilkParentEpilogueFn(), "", I);
      Epilogue->setDebugLoc(I->getDebugLoc());
    }
  }
  for (BasicBlock *SBB : BB_FastCloneList) {
    LLVM_DEBUG(dbgs() << "\nUpdating sync/return for " << SBB->getName() << "\n";);
    Instruction* I = SBB->getTerminator();
    if (I && isa<SyncInst>(I)) {
      LLVM_DEBUG(dbgs() << "Updating Sync Inst\n\t" << *I << "\n";);
      SyncInst* SI = dyn_cast<SyncInst>(I);
#if SLOW_PATH_NOT_CLONE
      LoadInst *loadedVal = new LoadInst(
        Type::getInt32Ty(F.getContext()),
        spawnFlag,
        "loadedSpawnFlag", SI);
      ICmpInst *cmp = new ICmpInst(
          SI,
          ICmpInst::ICMP_EQ,
          loadedVal,
          ConstantInt::get(Type::getInt32Ty(F.getContext()),1),
          "flag_is_one"
      );
      BasicBlock* SyncBB = BasicBlock::Create(F.getContext(), "syncBB", &F);
      CallBase *CB;
      if (F.doesNotThrow())
        CB = CallInst::Create(GetCilkSyncNoThrowFn(), "", SyncBB);
      else
        CB = CallInst::Create(GetCilkSyncFn(), "", SyncBB);
      CB->setDebugLoc(SI->getDebugLoc());
      BranchInst::Create(SI->getSuccessor(0), SyncBB);
      ReplaceInstWithInst(SI, BranchInst::Create(SyncBB, SI->getSuccessor(0), cmp));
#else
      ReplaceInstWithInst(SI, BranchInst::Create(SI->getSuccessor(0)));
#endif
    }
#if SLOW_PATH_NOT_CLONE
   else if (I && isa<ReturnInst>(I)) {
    LoadInst *loadedVal = new LoadInst(
      Type::getInt32Ty(F.getContext()),
      spawnFlag,
      "loadedSpawnFlag", I);
    ICmpInst *cmp = new ICmpInst(
        I,
        ICmpInst::ICMP_EQ,
        loadedVal,
        ConstantInt::get(Type::getInt32Ty(F.getContext()),1),
        "flag_is_one"
    );
    BasicBlock* returnBB = BasicBlock::Create(F.getContext(), "returnVal", &F);
    BasicBlock* cilkreturnBB = BasicBlock::Create(F.getContext(), "cilkReturnVal", &F);
    CallBase *CB = CallInst::Create(GetCilkParentEpilogueFn(), "", cilkreturnBB);
    CB->setDebugLoc(I->getDebugLoc());
    BranchInst::Create(returnBB, cilkreturnBB);
    ReturnInst::Create(F.getContext(),returnBB);
    ReplaceInstWithInst(I, BranchInst::Create(cilkreturnBB, returnBB, cmp));
  }
#endif
  }
  //Clean up any remaining taskframe intrinsics
  SmallVector<Instruction *, 32> TFToDelete;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I)) {
        if (Intrinsic::taskframe_create == II->getIntrinsicID()) {
          // Record this taskframe.create for splitting.
          LLVM_DEBUG(dbgs() << "Pushing TFCreate " << *II << "\n");
          // Look for a detach instructions and taskframe.end intrinsics that
          // use this taskframe.
          for (User *U : II->users()) {
            if (IntrinsicInst *UI = dyn_cast<IntrinsicInst>(U)) {
              if (Intrinsic::taskframe_use == UI->getIntrinsicID()) {
                TFToDelete.push_back(UI);
              } else if (Intrinsic::taskframe_end == UI->getIntrinsicID()) {
                TFToDelete.push_back(UI);
              }
            } else if (Instruction *UI = dyn_cast<Instruction>(U)) {
              if (isTaskFrameResume(UI, II)) {
                TFToDelete.push_back(UI);
              }
            }
          }
          TFToDelete.push_back(II);
        }
      }
    }
  }
  for (Instruction* I : TFToDelete) {
    I->eraseFromParent();
  }
  EliminateUnreachableBlocks(F);
  llvm::verifyFunction(F, &(outs()));
}



