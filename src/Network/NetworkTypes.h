#pragma once

// Backend-agnostic networking vocabulary.
//
// Nothing in this header (or anywhere under src/Network except PhotonBackend.*)
// may include or name a Photon type. Photon is one implementation of
// NetworkBackend; the engine talks only to these types, so a later Steam / ENet /
// dedicated-server backend drops in without touching gameplay code.
//
// Following the engine's ID-based architecture: objects are addressed by id, and
// nothing here retains a SceneObject*.

#include <cstdint>
#include <string>
#include <vector>

namespace Net {

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

// Per-session network object id. Allocated by the authority, unique within a
// session. Distinct from a scene object id: the same networked object has
// different scene ids on different clients.
using NetworkId = uint32_t;
constexpr NetworkId kInvalidNetworkId = 0;

// Photon actor numbers are 1-based ints; every backend maps its own player
// handle onto this.
using PlayerId = int32_t;
constexpr PlayerId kInvalidPlayerId = 0;

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

enum class ConnectionState {
    Disconnected = 0,
    Connecting,
    ConnectedToMaster,
    JoiningLobby,
    InLobby,
    JoiningRoom,
    InRoom,
    LeavingRoom,
    Disconnecting,
    Failed,
};

const char* ToString(ConnectionState state);

// Backend-neutral failure categories, so gameplay/UI can react without knowing
// Photon's numeric error codes. The backend maps its own codes onto these and
// keeps the original text in NetworkError::message.
enum class ErrorCode {
    None = 0,
    InvalidAppId,
    InvalidRegion,
    AuthenticationFailed,
    ConnectionTimeout,
    ConnectionLost,
    RoomNotFound,
    RoomFull,
    RoomAlreadyExists,
    NotInRoom,
    NotAuthorized,
    BackendUnavailable,   // package missing / backend not compiled in
    OperationFailed,
    Unknown,
};

const char* ToString(ErrorCode code);

struct NetworkError {
    ErrorCode code = ErrorCode::None;
    int nativeCode = 0;      // backend's own code, for logs only
    std::string message;

    bool ok() const { return code == ErrorCode::None; }
};

// ---------------------------------------------------------------------------
// Delivery
// ---------------------------------------------------------------------------

enum class SendMode {
    Unreliable = 0,
    Reliable,
};

// Who an RPC or event is delivered to.
enum class RpcTarget {
    All = 0,          // every player, including the sender
    Others,           // every player except the sender
    Host,             // the master client / session host
    Server,           // authority (same as Host for peer-hosted backends)
    Target,           // one specific player (see targetPlayer)
    Local,            // never leaves this process
    AllBuffered,      // All, and replayed to players who join later
    OthersBuffered,
};

const char* ToString(RpcTarget target);

inline bool IsBuffered(RpcTarget target) {
    return target == RpcTarget::AllBuffered || target == RpcTarget::OthersBuffered;
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

// Severity for the session's log sink. Mirrors the editor console's categories
// without this layer having to know the console exists.
enum class LogLevel {
    Info = 0,
    Warning,
    Error,
    Success,
};

const char* ToString(LogLevel level);

// ---------------------------------------------------------------------------
// Players and rooms
// ---------------------------------------------------------------------------

struct NetworkPlayer {
    PlayerId id = kInvalidPlayerId;
    std::string nickname;
    bool isLocal = false;
    bool isHost = false;          // master client
    // Custom player properties, backend-synchronized key/value strings.
    std::vector<std::pair<std::string, std::string>> properties;

    const std::string* findProperty(const std::string& key) const;
};

struct RoomInfo {
    std::string name;
    int playerCount = 0;
    int maxPlayers = 0;
    bool isOpen = true;
    bool isVisible = true;
    std::vector<std::pair<std::string, std::string>> properties;
};

struct RoomOptions {
    int maxPlayers = 0;             // 0 = backend default / unlimited
    bool isOpen = true;
    bool isVisible = true;
    // Properties published to the lobby so the room browser can filter on them.
    std::vector<std::string> lobbyVisibleProperties;
    std::vector<std::pair<std::string, std::string>> properties;
    bool cleanupCacheOnLeave = true;
};

// ---------------------------------------------------------------------------
// Session configuration
// ---------------------------------------------------------------------------

struct SessionConfig {
    // Credentials are supplied by project settings at runtime and must never be
    // baked into source or logged. See NetworkSession::describeConfig().
    std::string appId;
    std::string appVersion = "1.0";
    std::string nickname = "Player";
    std::string region;             // empty = best region / automatic
    bool offlineMode = false;
    int maxPlayers = 0;
    // How often synchronized state is sent, and the outbound budget. Correctness
    // first: these are conservative, not tuned.
    int sendRateHz = 20;
    int serializationRateHz = 10;
    int maxOutboundBytesPerSecond = 0;   // 0 = unlimited
};

// ---------------------------------------------------------------------------
// Synchronization
// ---------------------------------------------------------------------------

// How a remote object's transform is advanced between the snapshots that
// actually arrive over the network.
enum class SyncMode {
    // Snap straight to the newest snapshot. Cheapest, visibly steppy.
    Snapshot = 0,
    // Play back slightly in the past and blend between the two snapshots that
    // bracket the render time. Smooth, adds interpolationDelay of latency.
    Interpolate,
    // Interpolate, and when no new snapshot has arrived project forward using the
    // last known velocity. Hides brief packet loss at the cost of overshoot.
    Extrapolate,
};

const char* ToString(SyncMode mode);

// Per-object synchronization settings. Correctness first: the defaults are
// conservative rather than bandwidth-tuned.
struct SyncSettings {
    bool syncPosition = true;
    bool syncRotation = true;
    bool syncScale = false;      // scale rarely changes; off by default to save bandwidth
    bool syncVelocity = false;

    SyncMode mode = SyncMode::Interpolate;

    // Snapshots per second sent by the owner. 0 falls back to the session rate.
    int sendRateHz = 0;

    // How far behind the newest snapshot to render, in seconds. Must cover normal
    // inter-snapshot spacing or interpolation constantly runs dry.
    float interpolationDelay = 0.1f;

    // Cap on how far Extrapolate will project past the newest snapshot. Prevents
    // a stalled sender from flinging the object across the map.
    float maxExtrapolation = 0.25f;

    // Skip sending when nothing moved by more than this. 0 disables the check.
    float positionThreshold = 0.001f;
    float rotationThreshold = 0.01f;
    float scaleThreshold = 0.001f;
};

// One received sample of a remote object's transform.
struct TransformSnapshot {
    double timestamp = 0.0;   // sender-relative seconds
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float velocity[3] = {0.0f, 0.0f, 0.0f};
    bool hasPosition = false;
    bool hasRotation = false;
    bool hasScale = false;
    bool hasVelocity = false;
};

// Outbound traffic accounting, so a session can stay inside a byte budget and so
// the editor can show what networking actually costs.
struct BandwidthStats {
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint32_t packetsSent = 0;
    uint32_t packetsReceived = 0;
    uint32_t sendsSkippedByBudget = 0;
    uint32_t sendsSkippedByThreshold = 0;
};

// ---------------------------------------------------------------------------
// Spawn description
// ---------------------------------------------------------------------------

// What actually goes on the wire to replicate a ModuOBJ spawn.
//
// Deliberately tiny: the asset id plus a placement. Receivers instantiate through
// the existing ModuOBJ runtime API, so complete hierarchies are never transmitted
// and there is no second spawning system.
struct SpawnRequest {
    NetworkId networkId = kInvalidNetworkId;
    std::string assetId;        // ModuOBJ asset id
    std::string instanceId;     // ModuOBJ instance id, authored by the spawner
    PlayerId owner = kInvalidPlayerId;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};
    float scale[3] = {1.0f, 1.0f, 1.0f};
    // Network id of the parent object, or kInvalidNetworkId for a scene root.
    NetworkId parentNetworkId = kInvalidNetworkId;
    bool spawnDisabled = false;
};

}  // namespace Net
