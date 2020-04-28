#include "DLWrapper.hpp"

#include <cstring>
#include <stdexcept> // std::runtime_error

#include "IDataSupplier.hpp"
#include "IScriptSupplier.hpp"
#include "ILogger.hpp"
#include "../../DLOpen.hpp"

namespace Ignis::Multirole::Core
{

// Core callbacks
static void DataReader(void* payload, int code, OCG_CardData* data)
{
	*data = static_cast<IDataSupplier*>(payload)->DataFromCode(code);
}

static int ScriptReader(void* payload, OCG_Duel duel, const char* name)
{
	auto& srd = *static_cast<DLWrapper::ScriptReaderData*>(payload);
	std::string script = srd.supplier->ScriptFromFilePath(name);
	if(script.empty())
		return 0;
	return srd.OCG_LoadScript(duel, script.data(), script.length(), name);
}

static void LogHandler(void* payload, const char* str, int t)
{
	static_cast<ILogger*>(payload)->Log(static_cast<ILogger::LogType>(t), str);
}

static void DataReaderDone(void* payload, OCG_CardData* data)
{
	static_cast<IDataSupplier*>(payload)->DataUsageDone(*data);
}

// public

DLWrapper::DLWrapper(std::string_view absFilePath)
{
	handle = DLOpen::LoadObject(absFilePath.data());
	if(handle == nullptr)
		throw std::runtime_error("Could not load core.");
	// Load every function from the shared object into the functions
#define OCGFUNC(ret, name, args) \
	do{ \
	void* funcPtr = DLOpen::LoadFunction(handle, #name); \
	(name) = reinterpret_cast<decltype(name)>(funcPtr); \
	if((name) == nullptr) \
	{ \
		DLOpen::UnloadObject(handle); \
		throw std::runtime_error("Could not load API function."); \
	} \
	}while(0);
#include "../../ocgapi_funcs.inl"
#undef OCGFUNC
	scriptReaderData.OCG_LoadScript = OCG_LoadScript;
}

DLWrapper::~DLWrapper()
{
	DLOpen::UnloadObject(handle);
}

void DLWrapper::SetDataSupplier(IDataSupplier* ds)
{
	dataSupplier = ds;
}

// IDataSupplier* DLWrapper::GetDataSupplier()
// {
// 	return dataSupplier;
// }

void DLWrapper::SetScriptSupplier(IScriptSupplier* ss)
{
	scriptReaderData.supplier = ss;
}

IScriptSupplier* DLWrapper::GetScriptSupplier()
{
	return scriptReaderData.supplier;
}

void DLWrapper::SetLogger(ILogger* l)
{
	logger = l;
}

// ILogger* DLWrapper::GetLogger()
// {
// 	return logger;
// }

IWrapper::Duel DLWrapper::CreateDuel(const DuelOptions& opts)
{
	OCG_DuelOptions options =
	{
		opts.seed,
		opts.flags,
		opts.team1,
		opts.team2,
		&DataReader,
		dataSupplier,
		&ScriptReader,
		&scriptReaderData,
		&LogHandler,
		logger,
		&DataReaderDone,
		dataSupplier
	};
	OCG_Duel duel = nullptr;
	if(OCG_CreateDuel(&duel, options) != OCG_DUEL_CREATION_SUCCESS)
		throw std::runtime_error("Could not create duel");
	return duel;
}

void DLWrapper::DestroyDuel(Duel duel)
{
	OCG_DestroyDuel(duel);
}

void DLWrapper::AddCard(Duel duel, const OCG_NewCardInfo& info)
{
	OCG_DuelNewCard(duel, info);
}

void DLWrapper::Start(Duel duel)
{
	OCG_StartDuel(duel);
}

IWrapper::DuelStatus DLWrapper::Process(Duel duel)
{
	return static_cast<DuelStatus>(OCG_DuelProcess(duel));
}

IWrapper::Buffer DLWrapper::GetMessages(Duel duel)
{
	uint32_t length;
	auto pointer = OCG_DuelGetMessage(duel, &length);
	Buffer buffer(static_cast<Buffer::size_type>(length));
	std::memcpy(buffer.data(), pointer, static_cast<std::size_t>(length));
	return buffer;
}

void DLWrapper::SetResponse(Duel duel, const Buffer& buffer)
{
	OCG_DuelSetResponse(duel, buffer.data(), buffer.size());
}

int DLWrapper::LoadScript(Duel duel, std::string_view name, std::string_view str)
{
	return OCG_LoadScript(duel, str.data(), str.size(), name.data());
}

std::size_t DLWrapper::QueryCount(Duel duel, uint8_t team, uint32_t loc)
{
	return static_cast<std::size_t>(OCG_DuelQueryCount(duel, team, loc));
}

IWrapper::Buffer DLWrapper::Query(Duel duel, const QueryInfo& info)
{
	uint32_t length;
	auto pointer = OCG_DuelQuery(duel, &length, info);
	Buffer buffer(static_cast<Buffer::size_type>(length));
	std::memcpy(buffer.data(), pointer, static_cast<std::size_t>(length));
	return buffer;
}

IWrapper::Buffer DLWrapper::QueryLocation(Duel duel, const QueryInfo& info)
{
	uint32_t length;
	auto pointer = OCG_DuelQueryLocation(duel, &length, info);
	Buffer buffer(static_cast<Buffer::size_type>(length));
	std::memcpy(buffer.data(), pointer, static_cast<std::size_t>(length));
	return buffer;
}

IWrapper::Buffer DLWrapper::QueryField(Duel duel)
{
	uint32_t length;
	auto pointer = OCG_DuelQueryField(duel, &length);
	Buffer buffer(static_cast<Buffer::size_type>(length));
	std::memcpy(buffer.data(), pointer, static_cast<std::size_t>(length));
	return buffer;
}

} // namespace Ignis::Multirole::Core