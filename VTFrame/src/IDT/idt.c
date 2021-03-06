#include "idt.h"

#include "../Include/Native.h"
#include "../Debug/DebugAPI.h"

VOID PrintIdt() 
{
	for (int i = 0; i < KeNumberProcessors; i++)
	{
		KeSetSystemAffinityThread((KAFFINITY)(1 << i));
		//读取IDTR寄存器
		IDT_INFO idtr = { 0 };
		__sidt(&idtr);
		//表中每一项都是一个KIDTENTRY64结构的地址,里面有对应的中断函数地址
		PKIDTENTRY64 pEntry = (PKIDTENTRY64)idtr.Base;
		TRAPADDR trap = { 0 };
		trap.field.low = pEntry[1].OffsetLow;
		trap.field.mid = pEntry[1].OffsetMiddle;
		trap.field.hig = pEntry[1].OffsetHigh;
		//替换0x0f为1号中断处理函数，然后在VT处理中将一号中断转发给0x0f中断
		KIRQL irql = WPOFFx64();
		pEntry[0x0f].Alignment = pEntry[1].Alignment;
		WPONx64(irql);
		KeRevertToUserAffinityThread();
	}
}

ULONG64 GetTrap03Address() 
{
	PKIDTENTRY64 pEntry = NULL;
	TRAPADDR trap = { 0 };
	PVOID pKiExceptionDispatch = 0,pKiDispatchException = 0,pDbgkForwardException = 0;
	//读取IDTR寄存器
	IDT_INFO idtr = { 0 };
	__sidt(&idtr);

	//表中每一项都是一个KIDTENTRY64结构的地址,里面有对应的中断函数地址
	pEntry = (PKIDTENTRY64)idtr.Base;
	trap.field.low = pEntry[3].OffsetLow;
	trap.field.mid = pEntry[3].OffsetMiddle;
	trap.field.hig = pEntry[3].OffsetHigh;

	//硬编码d5,ldasm读取trap03某些指令出错了
	pKiExceptionDispatch = (PVOID)GetSubFunInFunction2((PVOID)(trap.All+0xd5), 0);
	pKiDispatchException = (PVOID)GetSubFunInFunction2(pKiExceptionDispatch, 1);
	pDbgkForwardException = (PVOID)GetSubFunInFunction2(pKiDispatchException, DbgkForwardException_i);
	return (ULONG64)pDbgkForwardException;
}

ULONG64 GetKiDispatchExceptionAddress()
{
	PKIDTENTRY64 pEntry = NULL;
	TRAPADDR trap = { 0 };
	PVOID pKiExceptionDispatch = 0, pKiDispatchException = 0, pDbgkForwardException = 0;
	//读取IDTR寄存器
	IDT_INFO idtr = { 0 };

	__sidt(&idtr);

	//表中每一项都是一个KIDTENTRY64结构的地址,里面有对应的中断函数地址
	pEntry = (PKIDTENTRY64)idtr.Base;
	trap.field.low = pEntry[3].OffsetLow;
	trap.field.mid = pEntry[3].OffsetMiddle;
	trap.field.hig = pEntry[3].OffsetHigh;

	//硬编码d5,ldasm读取trap03某些指令出错了
	pKiExceptionDispatch = (PVOID)GetSubFunInFunction2((PVOID)(trap.All + 0xd5), 0);
	pKiDispatchException = (PVOID)GetSubFunInFunction2(pKiExceptionDispatch, 1);
	
	return (ULONG64)pKiDispatchException;
}
