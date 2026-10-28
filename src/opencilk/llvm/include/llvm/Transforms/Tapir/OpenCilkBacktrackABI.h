//===- OpenCilkBacktrackABI.h - Generate the IR for doing OpenCilk Backtrack based stealing ------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the OpenCilk Backtracking backend, which is used to allow for stealing using DWARF-based backtracking.
//
//===----------------------------------------------------------------------===//
#ifndef OPENCILK_BACKTRACK_ABI_H_
#define OPENCILK_BACKTRACK_ABI_H_

#include "llvm/Transforms/Tapir/LoweringUtils.h"

namespace llvm {

struct CilkBacktrackSpawnHelper {
  Function* slow_helper;
  Function* fast_helper;
};

class OpenCilkBacktrackABI : public TapirTarget {

  SmallPtrSet<CallBase *, 8> CallsToInline;
  StringRef RuntimeBCPath = "";

  // Cilk RTS data types
  StructType *StackFrameTy = nullptr;
  StructType *WorkerTy = nullptr;

  // Opaque Cilk RTS functions
  FunctionCallee CilkRTSEnterFrame = nullptr;
  FunctionCallee CilkRTSEnterSlowSpawn = nullptr;
  FunctionCallee CilkRTSEnableSlowSpawn = nullptr;
  FunctionCallee CilkRTSLeaveSlowSpawn = nullptr;
  FunctionCallee CilkRTSEnterFrameHelper = nullptr;
  FunctionCallee CilkRTSDetach = nullptr;
  FunctionCallee CilkRTSLeaveFrame = nullptr;
  FunctionCallee CilkRTSLeaveFrameHelper = nullptr;
  FunctionCallee CilkPrepareSpawn = nullptr;
  FunctionCallee CilkSync = nullptr;
  FunctionCallee CilkRTSPopFrame = nullptr;
  FunctionCallee CilkSyncNoThrow = nullptr;
  FunctionCallee CilkParentEpilogue = nullptr;
  FunctionCallee CilkHelperEpilogue = nullptr;
  FunctionCallee CilkRTSEnterLandingpad = nullptr;
  FunctionCallee CilkRTSPauseFrame = nullptr;
  FunctionCallee CilkHelperEpilogueExn = nullptr;
  FunctionCallee CilkRTSCilkForGrainsize8 = nullptr;
  FunctionCallee CilkRTSCilkForGrainsize16 = nullptr;
  FunctionCallee CilkRTSCilkForGrainsize32 = nullptr;
  FunctionCallee CilkRTSCilkForGrainsize64 = nullptr;

  FunctionCallee GetCilkSyncFn() {
    return CilkSync;
  }
  FunctionCallee GetCilkSyncNoThrowFn() {
    return CilkSyncNoThrow;
  }

  FunctionCallee GetCilkRTSPopFrame() {
    return CilkRTSPopFrame;
  }

  FunctionCallee Get__cilkrts_enter_frame() {
    return CilkRTSEnterFrame;
  }

  FunctionCallee Get__cilkrts_enter_slow_spawn() {
    return CilkRTSEnterSlowSpawn;
  }

  FunctionCallee Get__cilkrts_enable_slow_spawn() {
    return CilkRTSEnableSlowSpawn;
  }

  FunctionCallee Get__cilkrts_leave_slow_spawn() {
    return CilkRTSLeaveSlowSpawn;
  }

  FunctionCallee Get__cilkrts_leave_frame() {
    return CilkRTSLeaveFrame;
  }

  FunctionCallee GetCilkParentEpilogueFn() {
    return CilkParentEpilogue;
  }

  FunctionCallee Get__cilkrts_cilk_for_grainsize_8() {
    return CilkRTSCilkForGrainsize8;
  }
  FunctionCallee Get__cilkrts_cilk_for_grainsize_16() {
    return CilkRTSCilkForGrainsize16;
  }
  FunctionCallee Get__cilkrts_cilk_for_grainsize_32() {
    return CilkRTSCilkForGrainsize32;
  }
  FunctionCallee Get__cilkrts_cilk_for_grainsize_64() {
    return CilkRTSCilkForGrainsize64;
  }


Function * createFastBacktrackSpawnHelper(Function *F, Module *M);
Function * createSlowBacktrackSpawnHelper(Function *F, Module *M);
Function * createBacktrackSpawnHelper(Function *F, Module *M, bool is_slow, SmallVectorImpl<Function *> &NewSpawnHelpers);

public:
  OpenCilkBacktrackABI(Module &M) : TapirTarget(M) {}
  ~OpenCilkBacktrackABI() {}

  void setOptions(const TapirTargetOptions &Options) override final;
  void prepareModule() override final;

  Value *lowerGrainsizeCall(CallInst *GrainsizeCall) override final;
  void lowerSync(SyncInst &inst) override final;

  bool shouldDoOutlining(const Function &F) const override final {
    return true;
  }

  bool shouldDoNonTaskframeOutlining(const Function &F) const {
    return true; 
  }
  ValueMap<const Function *, CilkBacktrackSpawnHelper> func_helper_map;
  BasicBlock* generateInvokeFlow(BasicBlock* BB, BasicBlock* stealPath_BB, BasicBlock* pop_BB, StringRef& functionName, uint spawn_count, const Twine & path, bool is_slow, CallInst* original_CI, std::vector<BasicBlock *> &BB_SlowCloneUpdateList, SmallVectorImpl<Function *> &NewSpawnHelpers);
  BasicBlock * changeToCallBrAndSplitBasicBlockFast(CallInst *CI, BasicBlock *landingPad_BB, BasicBlock *pop_BB, StringRef& functionName, uint spawn_count, const Twine & path, std::vector<BasicBlock *> &BB_SlowCloneUpdateList, SmallVectorImpl<Function *> &NewSpawnHelpers);
  BasicBlock * changeToCallBrAndSplitBasicBlockSlow(CallInst *CI, BasicBlock *landingPad_BB, BasicBlock *pop_BB, StringRef& functionName, uint spawn_count, const Twine & path, std::vector<BasicBlock *> &BB_SlowCloneUpdateList, SmallVectorImpl<Function *> &NewSpawnHelpers);
  BasicBlock * changeToCallBrAndSplitBasicBlock(CallInst *CI, BasicBlock *landingPad_BB, BasicBlock *pop_BB, StringRef& functionName, uint spawn_count, const Twine & path, bool is_slow, std::vector<BasicBlock *> &BB_SlowCloneUpdateList, SmallVectorImpl<Function *> &NewSpawnHelpers);

  bool preProcessFunction(Function &F, TaskInfo &TI,
                          bool ProcessingTapirLoops) override final;
  void postProcessFunction(Function &F,
                           bool ProcessingTapirLoops) override final {}
  void postProcessFunction(Function &F, SmallVectorImpl<Function *> &NewHelpers,
                           bool ProcessingTapirLoops) override final;

  void postProcessHelper(Function &F) override final {}

  void preProcessOutlinedTask(Function &F, Instruction *DetachPt,
                              Instruction *TaskFrameCreate, bool IsSpawner,
                              BasicBlock *TFEntry) override final {}
  void postProcessOutlinedTask(Function &F, Instruction *DetachPt,
                               Instruction *TaskFrameCreate, bool IsSpawner,
                               BasicBlock *TFEntry) override final {}
  void preProcessRootSpawner(Function &F, BasicBlock *TFEntry) override final {}

  void postProcessRootSpawner(Function &F, BasicBlock *TFEntry) override final {
  }
  void processSubTaskCall(TaskOutlineInfo &TOI,
                          DominatorTree &DT) override final {}
};

}  // end of llvm namespace

#endif
