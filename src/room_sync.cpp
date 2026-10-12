// Stage 6 replaces this stub (src/room_sync.h): until then nothing is published and the pending list does the work.
#include "room_sync.h"

namespace crew::roomsync {
void TickRoomSync() noexcept {}
void LeaveRoomSync() noexcept {}
SyncState RoomSyncState() noexcept { return SyncState::unavailable; }
int RoomSyncLines(SyncLine*,int) noexcept { return 0; }
}  // namespace crew::roomsync
