#include "Test.h"

#include "../Hook/SysCallHook.h"
#include "../Hook/PageHook.h"
#include "../Hook/InlineHook.h"
#include "../Util/GetUnExportFunAddress.h"
#include "../Debug/DebugAPI.h"
#include "../IDT/idt.h"

//根据SSDT表函数的地址获取索引
#define SSDTIndex(ptr)              *(PULONG)((ULONG_PTR)ptr + 0x15)

ULONG64 ntdebugactiveprocess = 0;

ULONG64 TestFn(ULONG64 in1, ULONG64 in2);
#pragma alloc_text(".text0", TestFn)
ULONG64 TestFn(ULONG64 in1, ULONG64 in2)
{
	ULONG64 data1 = 0x500;
	data1 += in1;
	in2 -= 0x10;
	return in1 + in2 * 3 - in1 / in2 + data1;
}

ULONG64 hkTestFn(ULONG64 in1, ULONG64 in2);
#pragma alloc_text(".text1", hkTestFn)
ULONG64 hkTestFn(ULONG64 in1, ULONG64 in2)
{
	return 0xDEADBEEF;
}

VOID TestSSDTHook() 
{
	ntdebugactiveprocess = GetSSDTFunAddrress(Nt_DebugActiveProcess);
	//win7/win10 OK
	InitSysCallHook();
	InitDebugSystem();
	
	AddSSDTHook(Nt_CreateDebugObject, (PVOID)proxyNtCreateDebugObject, 4);
	//AddSSDTHook(Nt_DebugActiveProcess, (PVOID)proxyNtDebugActiveProcess, 2);
	AddSSDTHook(Nt_WaitForDebugEvent, (PVOID)proxyNtWaitForDebugEvent, 4);
	AddSSDTHook(Nt_DebugContinue, (PVOID)proxyNtDebugContinue, 3);
	AddSSDTHook(Nt_RemoveProcessDebug, (PVOID)proxyNtRemoveProcessDebug, 2);
}

VOID UnloadTest()
{
	UnHookSysCall();
}

PVOID obHandle = 0;

OB_PREOP_CALLBACK_STATUS PreOperation(
	_In_ PVOID RegistrationContext,
	_Inout_ POB_PRE_OPERATION_INFORMATION OperationInformation)
{
	UNREFERENCED_PARAMETER(RegistrationContext);
	if (OperationInformation->Operation == OB_OPERATION_HANDLE_CREATE) {

		//x32dbg对dnf进程进行句柄打开操作
		if (strcmp((const char*)PsGetProcessImageFileName(PsGetCurrentProcess()), "x32dbg.exe") == 0)
		{
			DbgPrint("x32dbg正在创建%s进程句柄1\n", PsGetProcessImageFileName((PEPROCESS)OperationInformation->Object));
			ULONG real = OperationInformation->Parameters->CreateHandleInformation.DesiredAccess;
			ULONG old = OperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess;
			DbgPrint("要求权限:%x,实际给的权限:%x1\n", old, real);

		}
	}

	return OB_PREOP_SUCCESS;
}

VOID TestCallBack()
{
	OB_OPERATION_REGISTRATION opReg = { 0 };
	OB_CALLBACK_REGISTRATION obReg = { 0 };

	UNICODE_STRING usAltitude;
	RtlInitUnicodeString(&usAltitude, L"2000");
	obReg.Version = ObGetFilterVersion();
	obReg.RegistrationContext = NULL;
	obReg.OperationRegistrationCount = 1;
	obReg.OperationRegistration = &opReg;
	obReg.Altitude = usAltitude;

	opReg.ObjectType = PsProcessType;
	opReg.Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
	opReg.PostOperation = NULL;
	opReg.PreOperation = PreOperation;

	NTSTATUS st = ObRegisterCallbacks(&obReg, &obHandle);
	if (NT_SUCCESS(st))
	{
		DbgPrint("创建回调成功1\n");
	}
}

NTSTATUS
MyDbgkpQueueMessage(
	IN PEPROCESS Process,
	IN PETHREAD Thread,
	IN OUT PDBGKM_MSG ApiMsg,
	IN ULONG Flags,
	IN PDEBUG_OBJECT TargetDebugObject
)
{
	DbgPrint("%s进程正在调用DbgkpQueueMessage\n",PsGetProcessImageFileName(PsGetCurrentProcess()));
	return OriDbgkpQueueMessage(Process,Thread,ApiMsg,Flags,TargetDebugObject);
}

VOID TestInlineHook() 
{
	PVOID pOri = NULL, pDbgkForwardException;
	pDbgkForwardException = (PVOID)GetTrap03Address();
	pOri = (PVOID)GetSubFunInFunction((PVOID)GetSubFunInFunction((PVOID)GetSubFunInFunction((PVOID)GetSSDTFunAddrress(Nt_DebugActiveProcess), DbgkpPostFakeProcessCreateMessages_i), DbgkpPostFakeThreadMessages_i), DbgkpQueueMessage_i);
	OriDbgkpQueueMessage = (OriDbgkpQueueMessagex)SetLineHook(pOri, DbgkpQueueMessage_2);

	//内核API Inline Hook
	SetLineHook(pDbgkForwardException, proxyDbgkForwardException);
	SetLineHook(ntdebugactiveprocess, proxyNtDebugActiveProcess);

	//DbgkCreateThread + 0x86 +3 = 2004 0803
	//NtCreateUserProcess->21 PspAllocateThread ->+47c  PspUserThreadStartup ->DbgkCreateThread
	PVOID NtCreateUserProcess = GetSSDTFunAddrress(Nt_CreateUserProcess);
	ULONG64 PspAllocateThread = GetSubFunInFunction(NtCreateUserProcess, PspAllocateThread_i);
	ULONG64 Traget = PspAllocateThread + 0x47c;
	ULONG temp = *(ULONG*)(Traget + 3);
	ULONG64 PspUserThreadStartup = Traget + temp + 7;
	ULONG64 DbgkCreateThread = GetSubFunInFunction(PspUserThreadStartup, DbgkCreateThread_i);
	//KiDispatchException + 2f8 + 3 = 2004 0803
	ULONG64 KiDispatchException = GetKiDispatchExceptionAddress();

	KIRQL irql = WPOFFx64();
	*(USHORT*)(DbgkCreateThread + 0x86 + 3) = 0x0308;
	*(USHORT*)(KiDispatchException + 0x2f8 + 3) = 0x0308;
	WPONx64(irql);
}

VOID TestPageHook() 
{
	NTSTATUS st = STATUS_UNSUCCESSFUL;
	PVOID pOriDbgkpQueueMessage = NULL, pDbgkForwardException = NULL;

	pDbgkForwardException = (PVOID)GetTrap03Address();
	pOriDbgkpQueueMessage = (PVOID)GetSubFunInFunction((PVOID)GetSubFunInFunction((PVOID)GetSubFunInFunction((PVOID)GetSSDTFunAddrress(Nt_DebugActiveProcess), DbgkpPostFakeProcessCreateMessages_i), DbgkpPostFakeThreadMessages_i), DbgkpQueueMessage_i);

	st = PHHook(pOriDbgkpQueueMessage, DbgkpQueueMessage_2);
	if (NT_SUCCESS(st))
	{
		DbgPrint("PHHook DbgkpQueueMessage success\n");
	}

	oriDbgkForwardException = (ULONG64)pDbgkForwardException;
	st = PHHook(pDbgkForwardException, proxyDbgkForwardException);
	if (NT_SUCCESS(st))
	{
		DbgPrint("PHHook DbgkForwardException success\n");
	}
	
	st = PHHook((PVOID)ntdebugactiveprocess, proxyNtDebugActiveProcess);
	if (NT_SUCCESS(st))
	{
		DbgPrint("PHHook DbgkForwardException success\n");
	}
}