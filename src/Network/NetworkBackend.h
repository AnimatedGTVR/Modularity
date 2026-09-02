#pragma once

// The seam every transport plugs into.
//
// Photon implements this; so could Steam, ENet, a dedicated server or a local
// loopback. Nothing above this interface names a Photon type, so gameplay code
// and the editor keep working if the backend is swapped or absent.
//
// Threading: implementations must not invoke callbacks from a transport thread.
// Backends queue events and deliver them from tick(), which the engine calls on
// the main thread. That keeps scene mutation single-threaded, matching the rest
// of the engine.

#include "NetworkTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Net {

// Events a backend reports upward. Delivered from tick() only.
struct BackendCallbacks {
    std::function<void(ConnectionState)> onStateChanged;
    std::function<void(const NetworkError&)> onError;

    std::function<void(const std::vector<RoomInfo>&)> onRoomListUpdated;
    std::function<void(const std::string& roomName)> onJoinedRoom;
    std::function<void()> onLeftRoom;

    std::function<void(const NetworkPlayer&)> onPlayerJoined;
    std::function<void(PlayerId)> onPlayerLeft;
    std::function<void(PlayerId newHost)> onHostChanged;

    std::function<void(const std::string& key, const std::string& value)> onRoomPropertyChanged;
    std::function<void(PlayerId, const std::string& key, const std::string& value)> onPlayerPropertyChanged;

    // Raw payload delivery. `code` distinguishes engine channels (spawn, despawn,
    // rpc, state). The payload is the NetworkSerializer byte buffer.
    std::function<void(PlayerId sender, uint8_t code, const std::vector<uint8_t>&)> onEvent;
};

// Engine-reserved event codes. Backends pass these through untouched.
namespace EventCode {
constexpr uint8_t kSpawn = 1;
constexpr uint8_t kDespawn = 2;
constexpr uint8_t kRpc = 3;
constexpr uint8_t kStateSync = 4;
constexpr uint8_t kOwnershipTransfer = 5;
constexpr uint8_t kUserBase = 64;   // gameplay codes start here
}  // namespace EventCode

class NetworkBackend {
public:
    virtual ~NetworkBackend() = default;

    // Human-readable backend name for logs and the editor ("Photon", "Offline").
    virtual const char* name() const = 0;

    virtual bool initialize(const SessionConfig& config, const BackendCallbacks& callbacks) = 0;
    virtual void shutdown() = 0;

    // Pumps the transport and drains queued callbacks. Called once per frame on
    // the main thread; must never block.
    virtual void tick(float deltaSeconds) = 0;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool reconnect() = 0;

    virtual ConnectionState state() const = 0;
    virtual int roundTripTimeMs() const = 0;
    virtual const std::string& region() const = 0;

    virtual bool joinLobby() = 0;
    virtual bool leaveLobby() = 0;
    virtual bool refreshRoomList() = 0;
    virtual const std::vector<RoomInfo>& roomList() const = 0;

    virtual bool createRoom(const std::string& name, const RoomOptions& options) = 0;
    virtual bool joinRoom(const std::string& name) = 0;
    virtual bool joinOrCreateRoom(const std::string& name, const RoomOptions& options) = 0;
    virtual bool joinRandomRoom() = 0;
    virtual bool leaveRoom() = 0;

    virtual const std::vector<NetworkPlayer>& players() const = 0;
    virtual PlayerId localPlayerId() const = 0;
    virtual PlayerId hostPlayerId() const = 0;
    virtual bool isHost() const = 0;

    virtual bool setRoomProperty(const std::string& key, const std::string& value) = 0;
    virtual bool setPlayerProperty(const std::string& key, const std::string& value) = 0;
    virtual bool setNickname(const std::string& nickname) = 0;

    // Sends a serialized payload. `target` selects recipients; `targetPlayer` is
    // used only when target == RpcTarget::Target.
    virtual bool sendEvent(uint8_t code,
                           const std::vector<uint8_t>& payload,
                           RpcTarget target,
                           SendMode mode,
                           PlayerId targetPlayer = kInvalidPlayerId) = 0;

    // Last error, cleared on the next successful operation.
    virtual const NetworkError& lastError() const = 0;
};

// Offline / local session backend.
//
// Satisfies "run the engine without Photon": it presents a single local player
// who is always the host, accepts every room operation, and loops Local/All
// sends straight back so gameplay code paths are identical to a live session.
// Also the backend the headless tests drive.
std::unique_ptr<NetworkBackend> CreateLoopbackBackend();

// Photon Realtime backend. Returns nullptr when the engine was built without
// Photon support (MODULARITY_ENABLE_PHOTON off, or the package absent), so the
// caller can fall back to loopback rather than hard-failing.
std::unique_ptr<NetworkBackend> CreatePhotonBackend();

// True when a Photon backend is actually available in this build.
bool IsPhotonBackendAvailable();

}  // namespace Net
