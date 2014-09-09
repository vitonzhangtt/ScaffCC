// Scaffold
// This pass stops at Rz gates in the call graphs and decomposes them into
// sequences of clifford+T gates
//

#include <cstdlib>
#include <cstdio>

#include "llvm/ADT/ArrayRef.h"

#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Intrinsics.h"
#include "llvm/LLVMContext.h"
#include "llvm/Pass.h"

#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstVisitor.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace {
	// We need to use a ModulePass in order to create new Functions
	struct Rotations : public ModulePass {
		static char ID;
		Rotations() : ModulePass(ID) {}

		struct RotationVisitor : public InstVisitor<RotationVisitor> {
			// All decompositions will be created as Functions in M's FunctionList
			Module *M;
			// The constructor is called once per module (in runOnModule)
			RotationVisitor(Module *module) : M(module) {}

			// private:
			std::string exec(const char* cmd) {
				FILE* pipe = popen(cmd, "r");
				if (!pipe) return "ERROR";
				char buffer[128];
				std::string result = "";
				while(!feof(pipe)) {
					if (fgets(buffer, 128, pipe) != NULL)
						result += buffer;
				}
				pclose(pipe);
				return result;
			} // exec()
			// public: 
			void visitCallInst(CallInst &I) {
				// Determine whether this is an Rz gate
				Function *CF = I.getCalledFunction();
				// Is this an intrinsic?
				if (!CF->isIntrinsic()) return;
				std::string rot_exec = std::string("/rotZ ");
				// If it is a rotation, what is the axis?
				std::string axis;
				switch (CF->getIntrinsicID()) {
					case Intrinsic::Rz:
						axis = std::string(" Z ");
						break;
					case Intrinsic::Rx:
						axis = std::string(" X ");
						break;
					case Intrinsic::Ry:
						axis = std::string(" Y ");
						break;
					default:
						return;
				}
				// Detemine whether we know the rotation angle
				if (!isa<ConstantFP>(I.getArgOperand(1))) {
				  errs() << "Unknown rotation angle: " << I.getArgOperand(1) << "\n";
				  errs() << "Instruction: " << I << "\n";
				  errs() << "Function: " << I.getParent()->getParent()->getName() << "\n";
					return;
				}
			      
				// Extract the target qubit from the CallInst
				// Using CallSite
				// CallSite CS(&I);
				// Value *Target = CS.getArgument(0);
				// errs() << *Target << "\n";
				// END CallSite
				Value *Target = I.getArgOperand(0);
				// Extract the rotation angle from the CallInst
				double Angle = cast<ConstantFP>(I.getArgOperand(1))
					->getValueAPF()
					.convertToDouble();
				// Create a unique function name (for lookup later)
				std::string buf; raw_string_ostream ss(buf);
				ss << "DecomposeRotation_" << Angle;
				std::string FuncName = ss.str();
				// Sanitize strings
				for (std::string::iterator iter = FuncName.begin(); iter < FuncName.end(); iter++) {
					switch (*iter) {
						case '-': FuncName.replace(iter,iter+1,"n"); break;
						case '+': FuncName.erase(iter); iter--; break;
						case '.': FuncName.replace(iter,iter+1,"_"); break;
						case '"': FuncName.erase(iter); iter--; break;
					}
				}
				// Create a FunctionType object with 'void' return type and one 'qbit'
				// parameter
				FunctionType *FuncType = FunctionType::get(
					Type::getVoidTy(getGlobalContext()),
					ArrayRef<Type*>(Type::getInt16Ty(getGlobalContext())),
					false);
				// Lookup the Function in the module
				Function *DR = M->getFunction(FuncName);
				// If it does not exist perform a decomposition
				if (!DR) {
					// Create the new function
					DR = Function::Create(FuncType, GlobalVariable::ExternalLinkage,
						FuncName, M);

					Function::arg_iterator args = DR->arg_begin(); //set name of variable
					Value* qArg = args;
					qArg->setName("q");

					// Create a BasicBlock and insert it at the end of the Function
					BasicBlock *BB = BasicBlock::Create(getGlobalContext(), "", DR, 0);
					// Populate the BasicBlock
					// Build SQCT command
					std::string buf; raw_string_ostream ss2(buf);
					char *path = getenv("SQCTPATH");
					if (!path) {
						errs() << "SQCT not found!\n";
						return;
					}
					ss2 << path << rot_exec << Angle << axis;
					//errs() << "Calling '" << ss2.str() << "'\n";
					// Capture result
					std::string circuit = exec(ss2.str().c_str());
					// Dummy circuit until SQCT is integrated
					// std::string circuit = "HTTtHTTtHTTtHTTt";
					// For each gate in decomposition:
					for (int i=0, e=circuit.length(); i<e; i++) {
						Function *gate = NULL;
						switch(circuit[i]) {
							case 'T':
								gate = Intrinsic::getDeclaration(M, Intrinsic::T);
								break;
							case 't':
								gate = Intrinsic::getDeclaration(M, Intrinsic::Tdag);
								break;
							case 'P':
								// TODO: P NOT YET SUPPORTED
								gate = Intrinsic::getDeclaration(M, Intrinsic::T);
								break;
							case 'p':
								// TODO: P NOT YET SUPPORTED
								gate = Intrinsic::getDeclaration(M, Intrinsic::Tdag);
								break;
							case 'H':
								gate = Intrinsic::getDeclaration(M, Intrinsic::H);
								break;
							case 'X':
								gate = Intrinsic::getDeclaration(M, Intrinsic::X);
								break;
							case 'Y':
								gate = Intrinsic::getDeclaration(M, Intrinsic::Y);
								break;
							case 'Z':
								gate = Intrinsic::getDeclaration(M, Intrinsic::Z);
								break;
							default:
								continue;
						}
						CallInst *newCallInst;
						newCallInst = CallInst::Create(gate, ArrayRef<Value*>(DR->arg_begin()),
								// Insert at front
								// "", BB->front());
								// Insert at end
								"", BB);
						//newCallInst->setTailCall();
					}
					ReturnInst::Create(getGlobalContext(), 0, BB);
				} // endif 'decomposition not found'
				// Replace the old Rz call with the new call to Decomposed_Rotation
				BasicBlock::iterator ii(&I);
				ReplaceInstWithInst(I.getParent()->getInstList(), ii,
					CallInst::Create(DR, ArrayRef<Value*>(Target)));
				// } // endif 'found Rz'
			} // visitCallInst()

		}; // struct RotationVisitor

		virtual bool runOnModule(Module &M) {
			RotationVisitor RV(&M);
			RV.visit(M);

			return true;
		} // runOnModule()
		
	}; // struct Rotations
} // namespace

char Rotations::ID = 0;
static RegisterPass<Rotations> X("Rotations", "Rotation Decomposition", false, false);

