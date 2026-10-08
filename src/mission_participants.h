#pragma once

namespace crew {
struct ObjRef;
// Game thread only. Enumerates actual Soldier actors in all seven native team registries,
// including dead players. Returned EOS ProductUserId handles are borrowed for this snapshot.
// count is ACTOR count (split-screen users may share a PUID); expectedPlayers is the native
// mission-start count, not EOS lobby membership. The caller decides when a snapshot is complete.
// Failure clears both counts: never use a truncated/partially loaded roster as a quorum.
bool ReadMissionParticipants(void** puids,unsigned capacity,unsigned* count,unsigned* expectedPlayers) noexcept;
bool MissionParticipantCreationsMatch(const ObjRef* createdByIndex,unsigned expectedPlayers) noexcept;
}
