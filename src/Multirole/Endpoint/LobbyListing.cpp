#include "LobbyListing.hpp"

#include <asio/write.hpp>
#include <fmt/format.h> // fmt::to_string
#include <nlohmann/json.hpp>

#include "../Lobby.hpp"

namespace Ignis::Multirole::Endpoint
{

// public

LobbyListing::LobbyListing(
	asio::io_context& ioCtx, unsigned short port, Lobby& lobby) :
	acceptor(ioCtx, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
	serializeTimer(ioCtx),
	lobby(lobby)
{
	DoAccept();
	DoSerialize();
}

void LobbyListing::Stop()
{
	acceptor.close();
	serializeTimer.cancel();
}

// private

void LobbyListing::DoSerialize()
{
	serializeTimer.expires_after(std::chrono::seconds(2));
	serializeTimer.async_wait([this](const std::error_code& ec)
	{
		if(ec)
			return;
		auto ComposeHeader = [](std::size_t length, std::string_view mime)
		{
			constexpr const char* HTTP_HEADER_FORMAT_STRING =
			"HTTP/1.0 200 OK\r\n"
			"Content-Length: {:d}\r\n"
			"Content-Type: {:s}\r\n\r\n";
			return fmt::format(HTTP_HEADER_FORMAT_STRING, length, mime);
		};
		nlohmann::json j{{"rooms", nlohmann::json::array()}};
		nlohmann::json& ar = j["rooms"];
		for(auto& rp : lobby.GetAllRoomsProperties())
		{
			ar.emplace_back();
			auto& room = ar.back();
			room["roomid"] = rp.id;
			room["roomname"] = ""; // NOTE: UNUSED but expected atm
			room["roomnotes"] = rp.notes;
			room["roommode"] = 0; // NOTE: UNUSED but expected atm
			room["needpass"] = rp.passworded;
			room["team1"] = rp.info.t1Count;
			room["team2"] = rp.info.t2Count;
			room["best_of"] = rp.info.bestOf;
			room["duel_flag"] = rp.info.duelFlags;
			room["forbidden_types"] = rp.info.forb;
			room["extra_rules"] = rp.info.extraRules;
			room["start_lp"] = rp.info.startingLP;
			room["start_hand"] = rp.info.startingDrawCount;
			room["draw_count"] = rp.info.drawCountPerTurn;
			room["time_limit"] = rp.info.timeLimitInSeconds;
			room["rule"] = rp.info.allowed;
			room["no_check"] = static_cast<bool>(rp.info.dontCheckDeck);
			room["no_shuffle"] = static_cast<bool>(rp.info.dontShuffleDeck);
			room["banlist_hash"] = rp.info.banlistHash;
			room["istart"] = rp.state == (Room::WAITING) ? "waiting" : "start";
			auto& ac = room["users"];
			for(auto& kv : rp.duelists)
			{
				ac.emplace_back();
				auto& client = ac.back();
// 				client["id"] = ???; // NOTE: UNUSED
				client["name"] = kv.second;
// 				client["ip"] = json::nlohmann::null; // NOTE: UNUSED
// 				client["status"] = json::nlohmann::null; // NOTE: UNUSED
				client["pos"] = kv.first;
			}
		}
		constexpr auto eHandler = nlohmann::json::error_handler_t::ignore;
		const std::string strJ = j.dump(-1, 0, false, eHandler); // DUMP EET
		{
			std::lock_guard<std::mutex> lock(mSerialized);
			serialized = ComposeHeader(strJ.size(), "application/json") + strJ;
		}
		DoSerialize();
	});
}

void LobbyListing::DoAccept()
{
	acceptor.async_accept(
	[this](const std::error_code& ec, asio::ip::tcp::socket soc)
	{
		if(!acceptor.is_open())
			return;
		if(!ec)
			DoSendRoomList(std::move(soc));
		DoAccept();
	});
}

void LobbyListing::DoSendRoomList(asio::ip::tcp::socket soc)
{
	std::lock_guard<std::mutex> lock(mSerialized);
	auto socPtr = std::make_shared<asio::ip::tcp::socket>(std::move(soc));
	auto msg = std::make_shared<std::string>(serialized);
	asio::async_write(*socPtr, asio::buffer(*msg),
	[socPtr, msg](const std::error_code& /*unused*/, std::size_t /*unused*/){});
}

} // namespace Ignis::Multirole::Endpoint
