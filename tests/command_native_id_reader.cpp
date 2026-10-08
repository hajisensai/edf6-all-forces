// Compile the production native ID reader separately from npc_core_check's
// anonymous fixture globals. Its held-soldier fixture owns those two symbols;
// only their unused support-spawn counterparts are renamed in this test TU.
#define HoldSupportSoldier IdentityFixtureHoldSupportSoldier
#define SupportSoldierHeld IdentityFixtureSupportSoldierHeld
#include "../src/support_soldier.cpp"
