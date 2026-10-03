#include "GDBBreakpoints.h"
#include "Debugger.h"
#include "Cafe/HW/Espresso/Recompiler/PPCRecompiler.h"

GDBServer::ExecutionBreakpoint::ExecutionBreakpoint(MPTR address, BreakpointType type, bool visible, std::string reason)
	: m_address(address), m_removedAfterInterrupt(false), m_reason(std::move(reason))
{
	if (type == BreakpointType::BP_SINGLE)
	{
		this->m_pauseThreads = true;
		this->m_restoreAfterInterrupt = false;
		this->m_deleteAfterAnyInterrupt = false;
		this->m_pauseOnNextInterrupt = false;
		this->m_visible = visible;
	}
	else if (type == BreakpointType::BP_PERSISTENT)
	{
		this->m_pauseThreads = true;
		this->m_restoreAfterInterrupt = true;
		this->m_deleteAfterAnyInterrupt = false;
		this->m_pauseOnNextInterrupt = false;
		this->m_visible = visible;
	}
	else if (type == BreakpointType::BP_RESTORE_POINT)
	{
		this->m_pauseThreads = false;
		this->m_restoreAfterInterrupt = false;
		this->m_deleteAfterAnyInterrupt = false;
		this->m_pauseOnNextInterrupt = false;
		this->m_visible = false;
	}
	else if (type == BreakpointType::BP_STEP_POINT)
	{
		this->m_pauseThreads = false;
		this->m_restoreAfterInterrupt = false;
		this->m_deleteAfterAnyInterrupt = true;
		this->m_pauseOnNextInterrupt = true;
		this->m_visible = false;
	}

	this->m_origOpCode = memory_readU32(address);
	memory_writeU32(address, DEBUGGER_BP_T_GDBSTUB_TW);
	PPCRecompiler_invalidateRange(address, address + 4);
}

GDBServer::ExecutionBreakpoint::~ExecutionBreakpoint()
{
	memory_writeU32(this->m_address, this->m_origOpCode);
	PPCRecompiler_invalidateRange(this->m_address, this->m_address + 4);
}

uint32 GDBServer::ExecutionBreakpoint::GetVisibleOpCode() const
{
	if (this->m_visible)
		return memory_readU32(this->m_address);
	else
		return this->m_origOpCode;
}

void GDBServer::ExecutionBreakpoint::RemoveTemporarily()
{
	memory_writeU32(this->m_address, this->m_origOpCode);
	PPCRecompiler_invalidateRange(this->m_address, this->m_address + 4);
	this->m_restoreAfterInterrupt = true;
}

void GDBServer::ExecutionBreakpoint::Restore()
{
	memory_writeU32(this->m_address, DEBUGGER_BP_T_GDBSTUB_TW);
	PPCRecompiler_invalidateRange(this->m_address, this->m_address + 4);
	this->m_restoreAfterInterrupt = false;
}

GDBServer::AccessBreakpoint::AccessBreakpoint(MPTR address, AccessPointType type)
	: m_address(address), m_type(type)
{
	cemuLog_log(LogType::Force, "Debugger read/write breakpoints are not supported on non-x86 CPUs yet.");
}

GDBServer::AccessBreakpoint::~AccessBreakpoint()
{
}
