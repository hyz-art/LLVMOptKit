#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
/*
 * Basic block 内部的 COPY 链穿透
不处理跨块 PHI 相关 COPY
不碰 outlined 函数上下文
 */

using namespace llvm;
#define DEBUG_TYPE "riscv-redundant-copy-elim"
namespace {

//    static std::optional<DestSourcePair> isCopyInstr(const MachineInstr &MI,
//                                                     const TargetInstrInfo &TII,
//                                                     bool UseCopyInstr) {
//        if (UseCopyInstr)
//            return TII.isCopyInstr(MI);
//
//        if (MI.isCopy())
//            return std::optional<DestSourcePair>(
//                    DestSourcePair{MI.getOperand(0), MI.getOperand(1)});
//
//        return std::nullopt;
//    }


class RISCVRedundantCopyElim : public MachineFunctionPass {
    public:
        static char ID;
        RISCVRedundantCopyElim() : MachineFunctionPass(ID) {}

        StringRef getPassName() const override {
            return "RISCV Redundant Copy Elimination";
        }

        bool runOnMachineFunction(MachineFunction &MF) override;

    private:
//    const MT3000InstrInfo *MII = nullptr;
//    const MT3000RegisterInfo *MRI = nullptr;
//    const auto &STI = MF.getSubtarget<RISCVSubtarget>();
//    MII = STI.getInstrInfo();
//    MRI = STI.getRegisterInfo();
        //找到源头寄存器
        Register getRootReg(Register Reg, const DenseMap <Register, Register> &CopyMap) const;

        //构建CopyMap ，遇见使用use进行替换，遇见def清除映射
        void forwardCopyPropagate(MachineBasicBlock &MBB, bool &Changed);
    };

    char RISCVRedundantCopyElim::ID = 0;

    //CoyMap动态追踪，只记录当前有效的copy
    Register RISCVRedundantCopyElim::getRootReg(
            Register Reg, const DenseMap<Register, Register> &CopyMap) const {
        while (CopyMap.count(Reg)) {
            Reg = CopyMap.find(Reg)->second;
        }
        return Reg;
    }

    //辅助找到源头,跨基本块，反向索引，防止中间对源寄存器的重新定义
    /*Register RISCVRedundantCopyElim::forwardUses(Register Reg, MachineRegisterInfo &MRI){
        while(Reg.isVirtual()){
            MachineInstr *Def=MRI.getVRegDef(Reg);
            if(!Def||!Def->isCopy()) break;
            Reg=Def->getOperand(1).getReg();//源操作数
        }
    }*/

    //向前传播
    void RISCVRedundantCopyElim::forwardCopyPropagate(MachineBasicBlock &MBB, bool &Changed) {
        DenseMap <Register, Register> CopyMap; // %dst -> %src
        //替换uses，构建CopyMap
        for (auto I = MBB.begin(); I != MBB.end(); ++I) {
            MachineInstr &MI = *I;
            //1.替换当前指令中所有use的寄存器
            for (MachineOperand &Op: MI.operands()) {
                if (Op.isReg() && Op.isUse() && !Op.isImplicit()) {
                    Register Reg = Op.getReg();
                    if (Reg.isVirtual() && CopyMap.count(Reg)) {
                        Register Root = getRootReg(Reg, CopyMap);
                        if (Root != Reg) {
                            Op.setReg(Root);
                            Changed = true;
                        }
                    }
                }
            }
            //2.处理Copy指令
            if (MI.isCopy()) {
                Register Dst = MI.getOperand(0).getReg();
                Register Src = MI.getOperand(1).getReg();
                if (Dst.isVirtual() && Src.isVirtual()) {
                    Register RootSrc = getRootReg(Src, CopyMap);
                    // 冗余 COPY: %a = COPY %a → 删除
                    if (Dst == RootSrc) {
                        MI.eraseFromParent();
                        Changed = true;
                        continue;
                    }
                    CopyMap[Dst] = RootSrc;
                }
                continue;
            }
            //3.当前指令定义了寄存器--失效相关copy映射
            for (const MachineOperand &Op: MI.operands()) {
                if (Op.isReg() && Op.isDef() && !Op.isImplicit()) {
                    Register DefReg = Op.getReg();
                    if (DefReg.isVirtual()) {
                        // 存在对DefReg的定义，会使COPY映射失效
                        CopyMap.erase(DefReg);
                        // 可以遍历COPYMap删除value==DefReg的项
                    }
                }
            }
        }


        //删除无用的COPY
        MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
        for (auto I = MBB.begin(); I != MBB.end();) {
            MachineInstr &MI = *I;
            if (MI.isCopy()) {
                Register DefReg = MI.getOperand(0).getReg();
                if (DefReg.isVirtual() && MRI.use_nodbg_empty(DefReg)) {
                    I = MBB.erase(I);
                    Changed = true;
                    continue;
                }
            }
            ++I;
        }
    }


    bool RISCVRedundantCopyElim::runOnMachineFunction(MachineFunction &MF) {
        llvm::errs() << "Running RISCVRedundantCopyElim on function: " << MF.getName() << "\n";
        if (skipFunction(MF.getFunction())) return false;
        //获取寄存器信息
        MachineRegisterInfo &MRI=MF.getRegInfo();
        bool Changed = false;
        //外层用for 内层用while
        for (auto &MBB : MF) {
            // TODO：删除所有未被使用的虚拟寄存器定义的 COPY
            auto I=MBB.begin();
            while(I!=MBB.end()){
                MachineInstr &MI=*I;//&MI引用了*I，使用MI直接等于会调用拷贝函数
                if(MI.isCopy()){
                    //获取定义的寄存器
                    Register DefReg=MI.getOperand(0).getReg();
                    if(DefReg.isVirtual() && MRI.use_nodbg_empty(DefReg)){
                        llvm::dbgs()<<"Erasing unused COPY: "<<MI;
                        I=MBB.erase(I);
                        Changed=true;
                        continue;
                    }
                }
                ++I;
            }

            // TODO：处理 COPY 链（如 %3 = COPY %2; %2 = COPY %0）
            // 对每个copy指令递归找到%2源头，直到物理寄存器或非copy定义，
            // 所有%3的所有uses都可以被替换成源头 ，重写uses


            // 处理 spill-reload 链


            //向前传播
            forwardCopyPropagate(MBB,Changed);
            //向后传播


        }
        return Changed;
    }
}




extern "C" ::llvm::Pass *my_copy_elim_create_pass() {
    return new RISCVRedundantCopyElim();
}

static llvm::RegisterPass <RISCVRedundantCopyElim> X("riscv-redundant-copy-elim", "My Copy Elim Pass");
