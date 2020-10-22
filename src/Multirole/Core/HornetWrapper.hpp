#ifndef HORNETWRAPPER_HPP
#define HORNETWRAPPER_HPP
#include "IWrapper.hpp"

#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace Ignis::Hornet
{

enum class Action : uint8_t;
struct SharedSegment;

} // namespace Ignis::Hornet

namespace Ignis::Multirole::Core
{

class HornetWrapper final : public IWrapper
{
public:
	HornetWrapper(std::string_view absFilePath);
	~HornetWrapper();

	std::pair<int, int> Version() override;

	Duel CreateDuel(const DuelOptions& opts) override;
	void DestroyDuel(Duel duel) override;
	void AddCard(Duel duel, const NewCardInfo& info) override;
	void Start(Duel duel) override;

	DuelStatus Process(Duel duel) override;
	Buffer GetMessages(Duel duel) override;
	void SetResponse(Duel duel, const Buffer& buffer) override;
	int LoadScript(Duel duel, std::string_view name, std::string_view str) override;

	std::size_t QueryCount(Duel duel, uint8_t team, uint32_t loc) override;
	Buffer Query(Duel duel, const QueryInfo& info) override;
	Buffer QueryLocation(Duel duel, const QueryInfo& info) override;
	Buffer QueryField(Duel duel) override;
private:
	using LockType = boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex>;

	const std::string shmName;
	boost::interprocess::shared_memory_object shm;
	boost::interprocess::mapped_region region;
	Hornet::SharedSegment* hss;

	[[nodiscard]] LockType NotifyAndWaitCompletion(Hornet::Action act);

	[[nodiscard]] LockType CallbackMechanism(Hornet::Action act);
};

} // namespace Ignis::Multirole::Core

#endif // HORNETWRAPPER_HPP
