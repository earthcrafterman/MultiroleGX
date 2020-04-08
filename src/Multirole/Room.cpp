#include "Room.hpp"

#include <fmt/format.h>

#include "CardDatabase.hpp"
#include "Client.hpp"
#include "IRoomManager.hpp"
#include "YGOPro/Banlist.hpp"
#include "YGOPro/Scope.hpp"
#include "YGOPro/Type.hpp"

namespace Ignis::Multirole
{

// public

Room::Room(IRoomManager& owner, asio::io_context& ioCtx, Options&& options) :
	STOCMsgFactory(options.info.t1Count),
	owner(owner),
	strand(ioCtx),
	host(nullptr),
	options(std::move(options)),
	state(STATE_WAITING)
{}

void Room::RegisterToOwner()
{
	options.id = owner.Add(shared_from_this());
}

Room::StateEnum Room::State() const
{
	return state;
}

bool Room::CheckPassword(std::string_view str) const
{
	return options.pass.empty() || options.pass == str;
}

Room::Properties Room::GetProperties()
{
	Properties prop;
	prop.info = options.info;
	prop.notes = options.notes;
	prop.passworded = !options.pass.empty();
	prop.id = options.id;
	prop.state = state;
	{
		std::lock_guard<std::mutex> lock(mDuelists);
		for(auto& kv : duelists)
		{
			auto pos = EncodePosition(kv.first);
			prop.duelists.emplace(pos, kv.second->Name());
		}
	}
	return prop;
}

asio::io_context::strand& Room::Strand()
{
	return strand;
}

void Room::TryClose()
{
	if(state != STATE_WAITING)
		return;
	// TODO: stop timers too
	std::lock_guard<std::mutex> lock(mClients);
	std::lock_guard<std::mutex> lock2(mDuelists);
	duelists.clear();
	for(auto& c : clients)
		c->Disconnect();
}

// private

void Room::OnJoin(Client& client)
{
	switch(state)
	{
	case STATE_WAITING:
	{
		// Join to waiting room
		JoinToWaiting(client);
		return;
	}
	default:
	{
		// Join to started duel
		JoinToDuel(client);
		return;
	}
	}
}

void Room::OnConnectionLost(Client& client)
{
	switch(state)
	{
	case STATE_WAITING:
	{
		if(host == &client)
		{
			{
				std::lock_guard<std::mutex> lock(mDuelists);
				duelists.clear();
			}
			std::lock_guard<std::mutex> lock(mClients);
			for(auto& c : clients)
				c->Disconnect();
			return;
		}
		client.Disconnect();
		const auto posKey = client.Position();
		if(posKey != Client::POSITION_SPECTATOR)
		{
			{
				std::lock_guard<std::mutex> lock(mDuelists);
				duelists.erase(posKey);
			}
			SendToAll(MakePlayerChange(client, PCHANGE_TYPE_LEAVE));
		}
		else
		{
			SendToAll(MakeWatchChange(clients.size() - duelists.size() - 1U));
		}
		return;
	}
	default: // Room is currently dueling
	{
		// TODO
		return;
	}
	}
}

void Room::OnChat(Client& client, std::string_view str)
{
	const auto posKey = client.Position();
	if(posKey != Client::POSITION_SPECTATOR)
	{
		SendToAll(MakeChat(client, str));
	}
	else
	{
		const auto formatted =
			fmt::format(FMT_STRING("{:s}: {:s}"), client.Name(), str);
		SendToAll(MakeChat(CHAT_MSG_TYPE_SPECTATOR, formatted));
	}
}

void Room::OnToDuelist(Client& client)
{
	if(state != STATE_WAITING)
		return;
	const auto posKey = client.Position();
	std::lock_guard<std::mutex> lock(mDuelists);
	if(posKey == Client::POSITION_SPECTATOR)
	{
		// NOTE: ifs intentionally not short-circuited
		if(TryEmplaceDuelist(client))
		{
			SendToAll(MakePlayerEnter(client));
			SendToAll(MakePlayerChange(client));
			SendToAll(MakeWatchChange(clients.size() - duelists.size()));
			client.Send(MakeTypeChange(client, host == &client));
		}
	}
	else
	{
		duelists.erase(posKey);
		auto nextPos = posKey;
		nextPos.second++;
		if(TryEmplaceDuelist(client, nextPos) && client.Position() != posKey)
		{
			client.SetReady(false);
			SendToAll(MakePlayerChange(posKey, client.Position()));
			SendToAll(MakePlayerChange(client));
			client.Send(MakeTypeChange(client, host == &client));
		}
	}
}

void Room::OnToObserver(Client& client)
{
	if(state != STATE_WAITING)
		return;
	const auto posKey = client.Position();
	if(posKey == Client::POSITION_SPECTATOR)
		return;
	{
		std::lock_guard<std::mutex> lock(mDuelists);
		duelists.erase(posKey);
	}
	SendToAll(MakePlayerChange(client, PCHANGE_TYPE_SPECTATE));
	client.SetPosition(Client::POSITION_SPECTATOR);
	client.Send(MakeTypeChange(client, host == &client));
}

void Room::OnUpdateDeck(Client& client, const std::vector<uint32_t>& main,
                        const std::vector<uint32_t>& side)
{
	if(state != STATE_WAITING)
		return;
	if(client.Position() == Client::POSITION_SPECTATOR)
		return;
	client.SetOriginalDeck(LoadDeck(main, side));
	// TODO: Handle side decking
}

void Room::OnReady(Client& client, bool value)
{
	if(state != STATE_WAITING)
		return;
	if(client.Position() == Client::POSITION_SPECTATOR ||
	   client.Ready() == value)
		return;
	if(client.OriginalDeck() == nullptr)
		value = false;
	if(value && options.info.dontCheckDeck == 0)
	{
		if(auto error = CheckDeck(*client.OriginalDeck()); error)
		{
			client.Send(*error);
			value = false;
		}
	}
	client.SetReady(value);
	SendToAll(MakePlayerChange(client));
}

void Room::OnTryKick(Client& client, uint8_t pos)
{
	if(state != STATE_WAITING || &client != host)
		return;
	Client::PosType p;
	p.first = static_cast<unsigned char>(pos >= options.info.t1Count);
	p.second = p.first != 0U ? pos - options.info.t1Count : pos;
	if(duelists.count(p) == 0 || duelists[p] == host)
		return;
	Client* kicked = duelists[p];
	kicked->Disconnect();
	{
		std::lock_guard<std::mutex> lock(mDuelists);
		duelists.erase(p);
	}
	SendToAll(MakePlayerChange(*kicked, PCHANGE_TYPE_LEAVE));
}

void Room::OnTryStart(Client& client)
{
	if(state != STATE_WAITING || &client != host)
		return;
	if(int32_t(duelists.size()) != options.info.t1Count + options.info.t2Count)
		return;
	for(const auto& kv : duelists)
		if(!kv.second->Ready())
			return;
	SendToAll(MakeStartDuel());
	SendRPS();
}

void Room::OnRPSChoice(Client& client, uint8_t value)
{
	if(state != STATE_RPS)
		return;
	const auto& pos = client.Position();
	if(pos.second != 0u || value > 3)
		return;
	states.rps.c[pos.first] = value;
	if(!states.rps.c[0] || !states.rps.c[1])
		return;
	SendToTeam(0u, MakeRPSResult(states.rps.c[0], states.rps.c[1]));
	SendToTeam(1u, MakeRPSResult(states.rps.c[1], states.rps.c[0]));
	if(states.rps.c[0] == states.rps.c[1])
	{
		SendRPS();
		return;
	}
	enum : uint8_t
	{
		ROCK    = 2,
		PAPER   = 3,
		SCISSOR = 1,
	};
	states.goingFirstSelector =
		duelists[{
		static_cast<uint8_t>(
			(states.rps.c[1] == ROCK    && states.rps.c[0] == SCISSOR) ||
			(states.rps.c[1] == PAPER   && states.rps.c[0] == ROCK)    ||
			(states.rps.c[1] == SCISSOR && states.rps.c[0] == PAPER)
		),0u}];
	states.goingFirstSelector->Send(MakeAskIfGoingFirst());
}

void Room::OnTurnChoice(Client& client, bool goingFirst)
{
	if(state != STATE_RPS && state != STATE_DUELING)
		return;
	if(&client != states.goingFirstSelector)
		return;
	StartDuel(
		(client.Position().first == 0u && goingFirst) ||
		(client.Position().first == 1u && !goingFirst));
}

void Room::Add(std::shared_ptr<Client> client)
{
	std::lock_guard<std::mutex> lock(mClients);
	clients.insert(client);
}

void Room::Remove(std::shared_ptr<Client> client)
{
	std::lock_guard<std::mutex> lock(mClients);
	clients.erase(client);
	if(clients.empty())
	{
		asio::post(strand,
		[this, self = shared_from_this()]()
		{
			owner.Remove(options.id);
			// NOTE: Destructor of this Room is called here
		});
	}
}

void Room::SendToAll(const YGOPro::STOCMsg& msg)
{
	for(auto& c : clients)
		c->Send(msg);
}

void Room::SendToTeam(uint8_t team, const YGOPro::STOCMsg& msg)
{
	assert(team < 2);
	for(const auto& kv : duelists)
	{
		if(kv.first.first != team)
			continue;
		kv.second->Send(msg);
	}
}

bool Room::TryEmplaceDuelist(Client& client, Client::PosType hint)
{
	auto EmplaceLoop = [&](Client::PosType p, uint8_t max) -> bool
	{
		for(; p.second < max; p.second++)
		{
			if(duelists.count(p) == 0 && duelists.emplace(p, &client).second)
			{
				client.SetPosition(p);
				return true;
			}
		}
		return false;
	};
	if(hint.first == 0U)
		if(EmplaceLoop(hint, options.info.t1Count))
			return true;
	auto p = hint;
	if(hint.first != 1U)
		p.second = 0U;
	p.first = 1U;
	if(EmplaceLoop(p, options.info.t2Count))
		return true;
	if(hint != Client::PosType{})
		return TryEmplaceDuelist(client);
	return false;
}

void Room::JoinToWaiting(Client& client)
{
	if(host == nullptr)
		host = &client;
	using YGOPro::STOCMsg;
	client.Send(STOCMsg::JoinGame{options.info}); // TODO: create on ctor?
	std::lock_guard<std::mutex> lock(mDuelists);
	if(TryEmplaceDuelist(client))
	{
		SendToAll(MakePlayerEnter(client));
		SendToAll(MakePlayerChange(client));
		client.Send(MakeTypeChange(client, host == &client));
		client.Send(MakeWatchChange(clients.size() - duelists.size()));
	}
	else
	{
		SendToAll(MakeWatchChange(clients.size() - duelists.size()));
	}
	for(auto& kv : duelists)
	{
		if(kv.second == &client)
			continue; // Skip the new duelist itself
		client.Send(MakePlayerEnter(*kv.second));
		client.Send(MakePlayerChange(*kv.second));
	}
}

void Room::JoinToDuel(Client& client)
{
	// TODO
}

std::unique_ptr<YGOPro::Deck> Room::LoadDeck(
	const std::vector<uint32_t>& main,
	const std::vector<uint32_t>& side) const
{
	auto IsExtraDeckCardType = [](uint32_t type) constexpr -> bool
	{
		if(type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ))
			return true;
		// NOTE: Link Spells exist.
		if((type & TYPE_LINK) && (type & TYPE_MONSTER))
			return true;
		return false;
	};
	auto& db = *options.cpkg.db;
	YGOPro::CodeVector m, e, s;
	uint32_t err = 0;
	for(const auto code : main)
	{
		auto& data = db.DataFromCode(code);
		if(data.code == 0)
		{
			err = code;
			continue;
		}
		if(data.type & TYPE_TOKEN)
			continue;
		if(IsExtraDeckCardType(data.type))
			e.push_back(code);
		else
			m.push_back(code);
	}
	for(const auto code : side)
	{
		auto& data = db.DataFromCode(code);
		if(data.code == 0)
		{
			err = code;
			continue;
		}
		if(data.type & TYPE_TOKEN)
			continue;
		s.push_back(code);
	}
	return std::make_unique<YGOPro::Deck>(
		std::move(m),
		std::move(e),
		std::move(s),
		err);
}

std::unique_ptr<YGOPro::STOCMsg> Room::CheckDeck(const YGOPro::Deck& deck) const
{
	using namespace Error;
	using namespace YGOPro;
	// Handy shortcut.
	auto MakeErrorPtr = [](DeckOrCard type, uint32_t value)
	{
		return std::make_unique<STOCMsg>(MakeError(type, value));
	};
	// Check if the deck had any error while loading.
	if(deck.Error())
		return MakeErrorPtr(CARD_UNKNOWN, deck.Error());
	// Amalgamate all card codes into a single map for easier iteration.
	std::map<uint32_t, std::size_t> all;
	auto AddToMap = [&all](const CodeVector& from)
	{
		for(const auto& code : from)
			all[code]++;
	};
	AddToMap(deck.Main());
	AddToMap(deck.Extra());
	AddToMap(deck.Side());
	// Merge aliased cards to their original code and delete them
	auto& db = *options.cpkg.db;
	for(auto it = all.begin(), last = all.end(); it != last;)
	{
		if(uint32_t alias = db.DataFromCode(it->first).alias; alias != 0)
		{
			all[alias] = all[alias] + it->second;
			it = all.erase(it);
		}
		else
		{
			++it;
		}
	}
	// Check if the deck obeys the limits.
	auto OutOfBound = [](const auto& lim, const CodeVector& vector) -> auto
	{
		std::pair<std::size_t, bool> p{vector.size(), false};
		return (p.second = p.first < lim.min || p.first > lim.max), p;
	};
	if(const auto p = OutOfBound(options.limits.main, deck.Main()); p.second)
		return MakeErrorPtr(DECK_BAD_MAIN_COUNT, p.first);
	if(const auto p = OutOfBound(options.limits.extra, deck.Extra()); p.second)
		return MakeErrorPtr(DECK_BAD_EXTRA_COUNT, p.first);
	if(const auto p = OutOfBound(options.limits.side, deck.Side()); p.second)
		return MakeErrorPtr(DECK_BAD_SIDE_COUNT, p.first);
	// Custom predicates...
	//	true if card scope is unnofficial using currently allowed mode.
	auto CheckUnofficial = [](uint32_t scope, uint8_t allowed) constexpr -> bool
	{
		switch(allowed)
		{
		case ALLOWED_CARDS_OCG_ONLY:
		case ALLOWED_CARDS_TCG_ONLY:
		case ALLOWED_CARDS_OCG_TCG:
			return scope > SCOPE_OCG_TCG;
		}
		return false;
	};
	//	true if card scope is prerelease and they aren't allowed
	auto CheckPrelease = [](uint32_t scope, uint8_t allowed) constexpr -> bool
	{
		return allowed == ALLOWED_CARDS_WITH_PRERELEASE &&
		       !(scope & SCOPE_OFFICIAL);
	};
	//	true if only ocg are allowed and scope is not ocg (its tcg).
	auto CheckOCG = [](uint32_t scope, uint8_t allowed) constexpr -> bool
	{
		return allowed == ALLOWED_CARDS_OCG_ONLY && !(scope & SCOPE_OCG);
	};
	//	true if only tcg are allowed and scope is not tcg (its ocg).
	auto CheckTCG = [](uint32_t scope, uint8_t allowed) constexpr -> bool
	{
		return allowed == ALLOWED_CARDS_TCG_ONLY && !(scope & SCOPE_TCG);
	};
	//	true if card code exists on the banlist and exceeds the listed amount.
	auto CheckBanlist = [](const auto& kv, const Banlist& bl) -> bool
	{
		if(bl.IsWhitelist() && bl.Whitelist().count(kv.first) == 0)
			return true;
		if(bl.Forbidden().count(kv.first) != 0U)
			return true;
		if(kv.second > 1 && (bl.Limited().count(kv.first) != 0U))
			return true;
		if(kv.second > 2 && (bl.Semilimited().count(kv.first) != 0U))
			return true;
		return false;
	};
	for(const auto& kv : all)
	{
		if(kv.second > 3)
			return MakeErrorPtr(CARD_MORE_THAN_3, kv.first);
		if(db.DataFromCode(kv.first).type & options.info.forb)
			return MakeErrorPtr(CARD_FORBIDDEN_TYPE, kv.first);
		const auto& ced = db.ExtraFromCode(kv.first);
		if(CheckUnofficial(ced.scope, options.info.allowed))
			return MakeErrorPtr(CARD_UNOFFICIAL, kv.first);
		if(CheckPrelease(ced.scope, options.info.allowed))
			return MakeErrorPtr(CARD_UNOFFICIAL, kv.first);
		if(CheckOCG(ced.scope, options.info.allowed))
			return MakeErrorPtr(CARD_TCG_ONLY, kv.first);
		if(CheckTCG(ced.scope, options.info.allowed))
			return MakeErrorPtr(CARD_OCG_ONLY, kv.first);
		if(options.banlist && CheckBanlist(kv, *options.banlist))
			return MakeErrorPtr(CARD_BANLISTED, kv.first);
	}
	return nullptr;
}

void Room::SendRPS()
{
	state = STATE_RPS;
	states.goingFirstSelector = nullptr;
	states.rps.c[0] = states.rps.c[1] = 0;
	auto msg = MakeAskRPS();
	duelists[{0u, 0u}]->Send(msg);
	duelists[{1u, 0u}]->Send(msg);
}

void Room::StartDuel(bool isTeam0GoingFirst)
{
	state = STATE_DUELING;
}

void Room::FinishDuel()
{
	assert(state == STATE_DUELING);
	// TODO
}

} // namespace Ignis::Multirole
