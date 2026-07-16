#include <Arduino.h>
#include <unity.h>

#include "modules/monstermesh/BattlePacket.h"
#include "modules/monstermesh/Gen1BattleEngine.h"
#include "modules/monstermesh/MonsterMeshBattleValidation.h"
#include "modules/monstermesh/MonsterMeshTextBattle.h"
#include "modules/monstermesh/WirePartyCodec.h"

#include <string.h>

namespace {

constexpr uint32_t MY_NODE    = 0x11223344u;
constexpr uint32_t PEER_NODE  = 0x55667788u;
constexpr uint32_t THIRD_NODE = 0xAABBCCDDu;
constexpr uint16_t SESSION    = 0x4217u;

using WireParty = Gen1BattleEngine::WireParty;
using WireMon   = Gen1BattleEngine::WireMon;

bool localPartyNotReady(void *) { return false; }

// ── Gen-1 party used only for the (unchanged) engine start / Psywave test ──────
Gen1Party makeParty(uint8_t level = 25, uint8_t move = 33)
{
    Gen1Party party = {};
    party.count = 1;
    party.species[0] = 0x99; // Bulbasaur's Red/Blue internal species id.
    party.species[1] = 0xFF;
    Gen1Pokemon &mon = party.mons[0];
    mon.species = 0x99;
    mon.level = level;
    mon.boxLevel = level;
    mon.dvs[0] = 0x88;
    mon.dvs[1] = 0x88;
    mon.moves[0] = move;
    const Gen1MoveData *moveData = gen1Move(move);
    mon.pp[0] = moveData ? moveData->pp : 0;
    return party;
}

// ── Neutral cross-gen V2 WireParty (national dex + final stats) ────────────────
// species 1 = Bulbasaur (national dex), move 33 = Tackle (valid Gen1/Gen3).
WireParty makeWireParty(uint8_t level = 25, uint16_t move = 33,
                        uint16_t species = 1)
{
    WireParty p;
    p.count = 1;
    WireMon &m = p.mons[0];
    m.species = species;
    m.level   = level;
    m.maxHp = 100; m.atk = 55; m.def = 50; m.spe = 45; m.spa = 65; m.spd = 60;
    m.moves[0] = move;
    return p;
}

// CHALLENGE_V2 wire layout:
//   header(4) | [0..3]=targetId(BE) [4]=gen [5]=nameLen [6..]=name | WireParty(139)
size_t buildChallengeV2(uint8_t out[BATTLELINK_MAX_PKT], uint32_t target,
                        const WireParty &party, uint8_t gen = 3,
                        uint16_t session = SESSION)
{
    memset(out, 0, BATTLELINK_MAX_PKT);
    BattlePacket *pkt = reinterpret_cast<BattlePacket *>(out);
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_CHALLENGE_V2;
    pkt->setSessionId(session);
    pkt->seq = 0;
    pkt->payload[0] = (target >> 24) & 0xFF;
    pkt->payload[1] = (target >> 16) & 0xFF;
    pkt->payload[2] = (target >> 8) & 0xFF;
    pkt->payload[3] = target & 0xFF;
    pkt->payload[4] = gen;
    pkt->payload[5] = 4;                 // nameLen
    memcpy(pkt->payload + 6, "peer", 4);
    packWireParty(party, pkt->payload + 6 + 4);
    return BATTLELINK_HDR_SIZE + 6 + 4 + TB_WIRE_PARTY_BYTES;
}

// ACCEPT_V2 wire layout:
//   header(4) | [0]=accepted [1]=nameLen [2..]=name | WireParty(139)
size_t buildAcceptV2(uint8_t out[BATTLELINK_MAX_PKT], uint16_t session,
                     bool accepted, const WireParty &party)
{
    memset(out, 0, BATTLELINK_MAX_PKT);
    BattlePacket *pkt = reinterpret_cast<BattlePacket *>(out);
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_ACCEPT_V2;
    pkt->setSessionId(session);
    pkt->seq = 0;
    pkt->payload[0] = accepted ? 1 : 0;
    pkt->payload[1] = 4;                 // nameLen
    memcpy(pkt->payload + 2, "peer", 4);
    packWireParty(party, pkt->payload + 2 + 4);
    return BATTLELINK_HDR_SIZE + 2 + 4 + TB_WIRE_PARTY_BYTES;
}

size_t buildUpdateWithBadHash(uint8_t out[BATTLELINK_MAX_PKT],
                              uint16_t session = SESSION)
{
    memset(out, 0, BATTLELINK_MAX_PKT);
    BattlePacket *pkt = reinterpret_cast<BattlePacket *>(out);
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_UPDATE;
    pkt->setSessionId(session);
    pkt->seq = 1;
    pkt->payload[0] = 0;
    pkt->payload[1] = (TB_UPD_RESULT >> 8) & 0xFF;
    pkt->payload[2] = TB_UPD_RESULT & 0xFF;
    pkt->payload[3] = 0xFF;
    pkt->payload[4] = 0xFF;
    pkt->payload[5] = 0xFF;
    pkt->payload[6] = TB_RESULT_DRAW;
    return BATTLELINK_HDR_SIZE + 7;
}

size_t buildFullState(uint8_t out[BATTLELINK_MAX_PKT],
                      uint16_t session = SESSION)
{
    memset(out, 0, BATTLELINK_MAX_PKT);
    BattlePacket *pkt = reinterpret_cast<BattlePacket *>(out);
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_FULL_STATE;
    pkt->setSessionId(session);
    pkt->seq = 2;
    size_t w = 0;
    pkt->payload[w++] = 0;
    pkt->payload[w++] = 0;
    pkt->payload[w++] = TB_RESULT_DRAW;
    for (uint8_t side = 0; side < 2; ++side) {
        pkt->payload[w++] = 0;
        pkt->payload[w++] = 1;
        pkt->payload[w++] = 0;
        pkt->payload[w++] = 10;
        pkt->payload[w++] = 0;
    }
    pkt->payload[w++] = 35;
    pkt->payload[w++] = 0;
    pkt->payload[w++] = 0;
    pkt->payload[w++] = 0;
    return BATTLELINK_HDR_SIZE + w;
}

// ── UPDATE builder claiming TB_UPD_HP|TB_UPD_BENCH ─────────────────────────────
// Audit finding 5f: the client walks the whole flag-ordered UPDATE payload and
// rejects a truncated packet BEFORE mutating engine state. `truncated=true`
// emits only the HP section (payload len 10) while still advertising the BENCH
// flag, so the up-front walk must drop the whole packet. `truncated=false`
// appends a well-formed empty-bench section (count=0, boosts, ecount=0) so the
// same flags are actually applied.
size_t buildUpdateHpBench(uint8_t out[BATTLELINK_MAX_PKT], uint16_t myHp,
                          uint16_t enemyHp, bool truncated, uint8_t seq,
                          uint16_t session = SESSION)
{
    memset(out, 0, BATTLELINK_MAX_PKT);
    BattlePacket *pkt = reinterpret_cast<BattlePacket *>(out);
    pkt->type = (uint8_t)PktType::TEXT_BATTLE_UPDATE;
    pkt->setSessionId(session);
    pkt->seq = seq;
    const uint16_t flags = TB_UPD_HP | TB_UPD_BENCH;   // 0x0081
    size_t w = 0;
    pkt->payload[w++] = 0;                       // turn
    pkt->payload[w++] = (flags >> 8) & 0xFF;     // flags BE hi
    pkt->payload[w++] = flags & 0xFF;            // flags BE lo
    pkt->payload[w++] = 0xFF;                    // hash[0] (deliberately bad)
    pkt->payload[w++] = 0xFF;                    // hash[1]
    pkt->payload[w++] = 0xFF;                    // hash[2]
    // TB_UPD_HP section (4 bytes): myHp BE, enemyHp BE.
    pkt->payload[w++] = (myHp >> 8) & 0xFF;
    pkt->payload[w++] = myHp & 0xFF;
    pkt->payload[w++] = (enemyHp >> 8) & 0xFF;
    pkt->payload[w++] = enemyHp & 0xFF;
    if (!truncated) {
        // TB_UPD_BENCH section (8 bytes for empty benches):
        //   count(1)=0 | active-mon boosts(6) | ecount(1)=0
        pkt->payload[w++] = 0;                   // self bench count
        for (int i = 0; i < 6; ++i) pkt->payload[w++] = 0;  // boost stages
        pkt->payload[w++] = 0;                   // enemy bench count
    }
    return BATTLELINK_HDR_SIZE + w;
}

// Drive a client into WAIT_REMOTE with a live engine (HP fully healed) so an
// incoming UPDATE has concrete engine state to (not) mutate.
void bringClientToWaitRemote(MonsterMeshTextBattle &battle,
                             MeshtasticTransport &transport)
{
    battle.setMyNodeNum(MY_NODE);
    battle.setMyTbParty(makeParty(), "me");
    uint8_t packet[BATTLELINK_MAX_PKT];
    const size_t len = buildChallengeV2(packet, MY_NODE, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    battle.handleKey('Y');   // ACCEPT → engine_.start() → Phase::WAIT_REMOTE
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::WAIT_REMOTE,
                            (uint8_t)battle.phase());
    // Drain the ACCEPT_V2 the client just queued so later sends are isolated.
    size_t sentLen = 0;
    transport.dequeueSend(packet, sentLen, sizeof(packet));
}

void test_truncated_update_hp_bench_is_dropped_without_engine_mutation()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    bringClientToWaitRemote(battle, transport);

    const uint16_t baseMyHp    = battle.engine().party(0).mons[0].hp;
    const uint16_t baseEnemyHp = battle.engine().party(1).mons[0].hp;
    // The injected HP must differ from the baseline or the test can't tell a
    // silent partial-apply from a no-op.
    TEST_ASSERT_TRUE(baseMyHp != 37);
    TEST_ASSERT_TRUE(baseEnemyHp != 42);

    uint8_t packet[BATTLELINK_MAX_PKT];
    // Flags claim HP|BENCH but the payload stops after the HP section. The
    // pre-walk hits BENCH's count byte past payloadLen and rejects the packet
    // before the HP section is applied.
    const size_t len = buildUpdateHpBench(packet, 37, 42, /*truncated*/ true,
                                          /*seq*/ 5);
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));

    // No section applied: engine HP is exactly the pre-packet baseline, and the
    // bad-hash resync path (which only runs after sections apply) never fired.
    TEST_ASSERT_EQUAL_UINT32(baseMyHp,    battle.engine().party(0).mons[0].hp);
    TEST_ASSERT_EQUAL_UINT32(baseEnemyHp, battle.engine().party(1).mons[0].hp);
    TEST_ASSERT_FALSE(battle.clientNeedsFullState());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::WAIT_REMOTE,
                            (uint8_t)battle.phase());
}

void test_wellformed_update_hp_bench_is_applied()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    bringClientToWaitRemote(battle, transport);

    const uint16_t baseMyHp    = battle.engine().party(0).mons[0].hp;
    const uint16_t baseEnemyHp = battle.engine().party(1).mons[0].hp;
    TEST_ASSERT_TRUE(baseMyHp != 37);
    TEST_ASSERT_TRUE(baseEnemyHp != 42);

    uint8_t packet[BATTLELINK_MAX_PKT];
    // Same HP|BENCH flags, now with the well-formed empty-bench section. The
    // full payload passes the pre-walk and the HP section mutates the engine.
    const size_t len = buildUpdateHpBench(packet, 37, 42, /*truncated*/ false,
                                          /*seq*/ 6);
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));

    TEST_ASSERT_EQUAL_UINT32(37, battle.engine().party(0).mons[0].hp);
    TEST_ASSERT_EQUAL_UINT32(42, battle.engine().party(1).mons[0].hp);
}

// ── V2 WireParty validation (invalid count / species / level / move) ───────────

void test_wire_party_validation_accepts_valid_party_transactionally()
{
    uint8_t blob[TB_WIRE_PARTY_BYTES];
    packWireParty(makeWireParty(), blob);
    WireParty decoded = {};

    TEST_ASSERT_TRUE(tbUnpackAndValidateWireParty(blob, sizeof(blob), decoded));
    TEST_ASSERT_EQUAL_UINT8(1, decoded.count);
    TEST_ASSERT_EQUAL_UINT32(1, decoded.mons[0].species);
    TEST_ASSERT_EQUAL_UINT8(25, decoded.mons[0].level);
    TEST_ASSERT_EQUAL_UINT32(33, decoded.mons[0].moves[0]);
    TEST_ASSERT_EQUAL_UINT32(100, decoded.mons[0].maxHp);
}

void test_wire_party_validation_rejects_bad_counts()
{
    uint8_t blob[TB_WIRE_PARTY_BYTES];
    packWireParty(makeWireParty(), blob);
    WireParty decoded = {};
    decoded.count = 3;

    blob[0] = 0;
    TEST_ASSERT_FALSE(tbUnpackAndValidateWireParty(blob, sizeof(blob), decoded));
    TEST_ASSERT_EQUAL_UINT8(3, decoded.count);   // untouched on failure
    blob[0] = 7;
    TEST_ASSERT_FALSE(tbUnpackAndValidateWireParty(blob, sizeof(blob), decoded));
    TEST_ASSERT_EQUAL_UINT8(3, decoded.count);
}

void test_wire_party_validation_rejects_bad_species_levels_and_moves()
{
    uint8_t blob[TB_WIRE_PARTY_BYTES];
    WireParty decoded = {};

    // species 0 (empty-slot sentinel) is never valid in an active slot.
    packWireParty(makeWireParty(), blob);
    blob[1] = 0; blob[2] = 0;
    TEST_ASSERT_FALSE(tbUnpackAndValidateWireParty(blob, sizeof(blob), decoded));

    // species > 386 (garbage / future dex).
    packWireParty(makeWireParty(), blob);
    blob[1] = 0x02; blob[2] = 0x00;   // 512
    TEST_ASSERT_FALSE(tbUnpackAndValidateWireParty(blob, sizeof(blob), decoded));

    // level 0 and level 101.
    packWireParty(makeWireParty(), blob);
    blob[3] = 0;
    TEST_ASSERT_FALSE(tbUnpackAndValidateWireParty(blob, sizeof(blob), decoded));
    blob[3] = 101;
    TEST_ASSERT_FALSE(tbUnpackAndValidateWireParty(blob, sizeof(blob), decoded));

    // move id 9999 resolves under no gen's move table.
    packWireParty(makeWireParty(), blob);
    blob[16] = 0x27; blob[17] = 0x0F; // 9999 at mon0 move slot 0
    TEST_ASSERT_FALSE(tbUnpackAndValidateWireParty(blob, sizeof(blob), decoded));
}

void test_raw_wire_party_level_zero_is_rejected()
{
    WireParty party = makeWireParty();
    party.mons[0].level = 0;
    TEST_ASSERT_FALSE(tbValidateWireParty(party));
}

// ── CHALLENGE_V2 handler: no bind on invalid / self / wrong-target ─────────────

void test_invalid_challenge_v2_does_not_bind_or_change_battle_state()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    battle.setMyNodeNum(MY_NODE);
    battle.setMyTbParty(makeParty(), "me");

    uint8_t packet[BATTLELINK_MAX_PKT];

    // Level-zero party in a CHALLENGE addressed to us must not bind a peer.
    size_t len = buildChallengeV2(packet, MY_NODE, makeWireParty(0));
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_FALSE(battle.isActive());
    TEST_ASSERT_EQUAL_UINT32(0, battle.remoteId());

    // A CHALLENGE echoed back from our own node id must be dropped.
    len = buildChallengeV2(packet, MY_NODE, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(MY_NODE, packet, len));
    TEST_ASSERT_FALSE(battle.isActive());
    TEST_ASSERT_EQUAL_UINT32(0, battle.remoteId());

    // A CHALLENGE addressed to a different node must be dropped.
    len = buildChallengeV2(packet, THIRD_NODE, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_FALSE(battle.isActive());
    TEST_ASSERT_EQUAL_UINT32(0, battle.remoteId());
}

void test_zero_sender_challenge_cannot_create_an_unbound_session()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    battle.setMyNodeNum(MY_NODE);
    battle.setMyTbParty(makeParty(), "me");

    uint8_t packet[BATTLELINK_MAX_PKT];
    const size_t len = buildChallengeV2(packet, MY_NODE, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(0, packet, len));
    TEST_ASSERT_FALSE(battle.isActive());
    TEST_ASSERT_EQUAL_UINT32(0, battle.remoteId());

    // A later UPDATE from a real third node must not inherit the rejected
    // challenge's session or mutate the battle into an active state.
    const size_t updateLen = buildUpdateWithBadHash(packet);
    TEST_ASSERT_FALSE(battle.handlePacket(THIRD_NODE, packet, updateLen));
    TEST_ASSERT_FALSE(battle.isActive());
}

void test_valid_challenge_v2_binds_client_and_opens_overlay()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    battle.setMyNodeNum(MY_NODE);
    battle.setMyTbParty(makeParty(), "me");

    uint8_t packet[BATTLELINK_MAX_PKT];
    const size_t len = buildChallengeV2(packet, MY_NODE, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_TRUE(battle.isActive());
    TEST_ASSERT_EQUAL_UINT32(PEER_NODE, battle.remoteId());
    TEST_ASSERT_EQUAL_UINT8(
        (uint8_t)MonsterMeshTextBattle::Phase::WAIT_CHALLENGE_OVERLAY,
        (uint8_t)battle.phase());
}

// ── ACCEPT_V2 handler (server role): wrong node + invalid party rejection ──────

void test_accept_v2_from_wrong_node_cannot_accept_or_decline()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    battle.setMyNodeNum(MY_NODE);
    battle.startServerAuthAsInitiator(PEER_NODE, makeParty(), "me");

    uint8_t challenge[BATTLELINK_MAX_PKT];
    size_t challengeLen = 0;
    TEST_ASSERT_TRUE(transport.dequeueSend(challenge, challengeLen,
                                           sizeof(challenge)));
    const uint16_t session =
        reinterpret_cast<const BattlePacket *>(challenge)->sessionId();

    uint8_t packet[BATTLELINK_MAX_PKT];
    // Third node ACCEPT(1) must not start the battle.
    size_t len = buildAcceptV2(packet, session, true, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(THIRD_NODE, packet, len));
    TEST_ASSERT_TRUE(battle.awaitingAccept());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::WAIT_REMOTE,
                            (uint8_t)battle.phase());

    // Bound peer sends an invalid (level 0) party — rejected, still awaiting.
    len = buildAcceptV2(packet, session, true, makeWireParty(0));
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_TRUE(battle.awaitingAccept());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::WAIT_REMOTE,
                            (uint8_t)battle.phase());

    // Third node DECLINE must not end our pending challenge either.
    len = buildAcceptV2(packet, session, false, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(THIRD_NODE, packet, len));
    TEST_ASSERT_TRUE(battle.awaitingAccept());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::WAIT_REMOTE,
                            (uint8_t)battle.phase());

    // Only the bound peer's DECLINE finishes it.
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_FALSE(battle.awaitingAccept());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::FINISHED,
                            (uint8_t)battle.phase());
}

void test_expected_peer_valid_accept_v2_starts_server_battle()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    battle.setMyNodeNum(MY_NODE);
    battle.startServerAuthAsInitiator(PEER_NODE, makeParty(), "me");

    uint8_t challenge[BATTLELINK_MAX_PKT];
    size_t challengeLen = 0;
    TEST_ASSERT_TRUE(transport.dequeueSend(challenge, challengeLen,
                                           sizeof(challenge)));
    const uint16_t session =
        reinterpret_cast<const BattlePacket *>(challenge)->sessionId();
    TEST_ASSERT_TRUE(session != 0);

    uint8_t packet[BATTLELINK_MAX_PKT];
    const size_t len = buildAcceptV2(packet, session, true, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_FALSE(battle.awaitingAccept());
    TEST_ASSERT_TRUE(battle.isActive());
    TEST_ASSERT_EQUAL_UINT32(PEER_NODE, battle.remoteId());
}

// ── UPDATE / FULL_STATE: wrong-node rejection (client role) ────────────────────

void test_update_and_full_state_reject_wrong_node()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    battle.setMyNodeNum(MY_NODE);
    battle.setMyTbParty(makeParty(), "me");

    uint8_t packet[BATTLELINK_MAX_PKT];
    size_t len = buildChallengeV2(packet, MY_NODE, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    battle.handleKey('Y');
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::WAIT_REMOTE,
                            (uint8_t)battle.phase());

    // A third node's UPDATE must be dropped before it can request a resync.
    len = buildUpdateWithBadHash(packet);
    TEST_ASSERT_TRUE(battle.handlePacket(THIRD_NODE, packet, len));
    TEST_ASSERT_FALSE(battle.clientNeedsFullState());

    // The bound peer's bad-hash UPDATE is processed and requests a resync.
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_TRUE(battle.clientNeedsFullState());

    // A third node's FULL_STATE must not clear the resync flag.
    len = buildFullState(packet);
    TEST_ASSERT_TRUE(battle.handlePacket(THIRD_NODE, packet, len));
    TEST_ASSERT_TRUE(battle.clientNeedsFullState());

    // Only the bound peer's FULL_STATE resolves the resync.
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_FALSE(battle.clientNeedsFullState());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::FINISHED,
                            (uint8_t)battle.phase());
}

void test_invalidated_local_party_converts_accept_to_decline()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    battle.setMyNodeNum(MY_NODE);
    battle.setMyTbParty(makeParty(), "me");
    battle.setLocalPartyReadyFn(localPartyNotReady, nullptr);

    uint8_t packet[BATTLELINK_MAX_PKT];
    const size_t len = buildChallengeV2(packet, MY_NODE, makeWireParty());
    TEST_ASSERT_TRUE(battle.handlePacket(PEER_NODE, packet, len));
    TEST_ASSERT_EQUAL_UINT8(
        (uint8_t)MonsterMeshTextBattle::Phase::WAIT_CHALLENGE_OVERLAY,
        (uint8_t)battle.phase());

    battle.handleKey('Y');
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MonsterMeshTextBattle::Phase::FINISHED,
                            (uint8_t)battle.phase());

    size_t sentLen = 0;
    TEST_ASSERT_TRUE(transport.dequeueSend(packet, sentLen, sizeof(packet)));
    const BattlePacket *sent = reinterpret_cast<const BattlePacket *>(packet);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)PktType::TEXT_BATTLE_ACCEPT_V2, sent->type);
    TEST_ASSERT_EQUAL_UINT8(0, sent->payload[0]);   // declined
}

// ── Engine: Psywave at corrupted level zero keeps a nonzero divisor ────────────

void test_psywave_level_zero_has_nonzero_divisor()
{
    Gen1Party user = makeParty(0, 149);
    Gen1Party target = makeParty(25, 33);
    Gen1BattleEngine engine;
    engine.start(user, target, 0x12345678u);

    const uint16_t before = engine.party(1).mons[0].hp;
    engine.submitAction(0, 0, 0);
    engine.submitAction(1, 1, 0); // no-op switch: lets only Psywave execute.
    engine.executeTurn(nullptr, nullptr);
    TEST_ASSERT_LESS_THAN_UINT16(before, engine.party(1).mons[0].hp);
}

void test_pending_xp_restore_is_lossless_and_saturating()
{
    MeshtasticTransport transport;
    MonsterMeshTextBattle battle(transport);
    const uint32_t first[6] = {17, 0, 23, 0, 0, 0};
    battle.restorePendingXp(first);

    uint32_t drained[6] = {};
    TEST_ASSERT_TRUE(battle.consumePendingXp(drained));
    TEST_ASSERT_EQUAL_UINT32(17, drained[0]);
    TEST_ASSERT_EQUAL_UINT32(23, drained[2]);
    TEST_ASSERT_FALSE(battle.consumePendingXp(drained));

    const uint32_t nearMax[6] = {UINT32_MAX - 2, 0, 0, 0, 0, 0};
    const uint32_t overflow[6] = {10, 0, 0, 0, 0, 0};
    battle.restorePendingXp(nearMax);
    battle.restorePendingXp(overflow);
    TEST_ASSERT_TRUE(battle.consumePendingXp(drained));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, drained[0]);
}

} // namespace

void setUp() {}
void tearDown() {}

void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_wire_party_validation_accepts_valid_party_transactionally);
    RUN_TEST(test_wire_party_validation_rejects_bad_counts);
    RUN_TEST(test_wire_party_validation_rejects_bad_species_levels_and_moves);
    RUN_TEST(test_raw_wire_party_level_zero_is_rejected);
    RUN_TEST(test_invalid_challenge_v2_does_not_bind_or_change_battle_state);
    RUN_TEST(test_zero_sender_challenge_cannot_create_an_unbound_session);
    RUN_TEST(test_valid_challenge_v2_binds_client_and_opens_overlay);
    RUN_TEST(test_accept_v2_from_wrong_node_cannot_accept_or_decline);
    RUN_TEST(test_expected_peer_valid_accept_v2_starts_server_battle);
    RUN_TEST(test_update_and_full_state_reject_wrong_node);
    RUN_TEST(test_invalidated_local_party_converts_accept_to_decline);
    RUN_TEST(test_psywave_level_zero_has_nonzero_divisor);
    RUN_TEST(test_pending_xp_restore_is_lossless_and_saturating);
    RUN_TEST(test_truncated_update_hp_bench_is_dropped_without_engine_mutation);
    RUN_TEST(test_wellformed_update_hp_bench_is_applied);
    exit(UNITY_END());
}

void loop() {}
