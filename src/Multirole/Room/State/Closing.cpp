#include "../Context.hpp"

namespace Ignis::Multirole::Room
{

void Context::operator()(State::Closing& /*unused*/)
{
	for(const auto& kv : duelists)
		kv.second->Disconnect();
	for(const auto& c : spectators)
		c->Disconnect();
	spectators.clear();
	duelists.clear();
}

StateOpt Context::operator()(State::Closing& /*unused*/, Event::Join& e)
{
	e.client.Disconnect();
	return std::nullopt;
}

StateOpt Context::operator()(State::Closing& /*unused*/, Event::ConnectionLost& e)
{
	e.client.Disconnect();
	return std::nullopt;
}

} // namespace Ignis::Multirole::Room
