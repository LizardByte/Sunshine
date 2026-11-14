/**
 * @file tests/unit/test_upnp_mapping.cpp
 * @brief Verify UPnP ownership boundaries and permanent-rule crash recovery.
 */

// test includes
#include "../tests_common.h"

// local includes
#include <src/upnp_mapping.h>

namespace {
  /**
   * @brief Construct a representative rule owned by Sunshine.
   * @return Rule on the inner gateway.
   */
  upnp::owned_mapping_t sample_mapping() {
    return {"http://192.168.1.1/control", "urn:schemas-upnp-org:service:WANIPConnection:1", "192.168.1.20", "47989", "47989", "TCP", "Sunshine - Client HTTP"};
  }

  TEST(UpnpMappingOwnershipTest, DistinguishesOwnedManualAndConflictingRules) {
    const auto mapping = sample_mapping();
    EXPECT_EQ(upnp::classify_mapping(0, mapping.target, mapping.internal_port, mapping.description, mapping), upnp::mapping_state_e::ours);
    EXPECT_EQ(upnp::classify_mapping(0, mapping.target, mapping.internal_port, "Manual forwarding", mapping), upnp::mapping_state_e::existing);
    EXPECT_EQ(upnp::classify_mapping(0, "192.168.1.99", mapping.internal_port, mapping.description, mapping), upnp::mapping_state_e::conflict);
    EXPECT_EQ(upnp::classify_mapping(0, mapping.target, "48000", mapping.description, mapping), upnp::mapping_state_e::conflict);
  }

  TEST(UpnpMappingOwnershipTest, LookupErrorsAreNeverProofOfOwnershipOrAbsence) {
    const auto mapping = sample_mapping();
    EXPECT_EQ(upnp::classify_mapping(714, "", "", "", mapping), upnp::mapping_state_e::missing);
    for (const int error : {-1, 501, 606, 718, 729}) {
      EXPECT_EQ(upnp::classify_mapping(error, mapping.target, mapping.internal_port, mapping.description, mapping), upnp::mapping_state_e::unknown);
    }
  }

  TEST(UpnpMappingJournalTest, RecoversCreationIntentWithoutNeedingTheLastDiscoveryResult) {
    const auto mapping = sample_mapping();
    const auto recovered = upnp::read_mapping_journal(upnp::mapping_journal_event(mapping, true));
    ASSERT_EQ(recovered.size(), 1);
    EXPECT_EQ(recovered.front(), mapping);
    EXPECT_TRUE(recovered.front().permanent);
  }

  TEST(UpnpMappingJournalTest, KeepsBothGatewaysAndOldDestinationsUntilRemoved) {
    auto inner = sample_mapping();
    auto outer = inner;
    outer.control_url = "http://10.0.0.1/control";
    outer.target = "10.0.0.42";
    auto changed = outer;
    changed.target = "10.0.0.43";
    auto events = upnp::mapping_journal_event(inner, true) + upnp::mapping_journal_event(outer, true) + upnp::mapping_journal_event(changed, true);
    ASSERT_EQ(upnp::read_mapping_journal(events).size(), 3);
    events += upnp::mapping_journal_event(outer, false);
    const auto recovered = upnp::read_mapping_journal(events);
    ASSERT_EQ(recovered.size(), 2);
    EXPECT_EQ(recovered[0], inner);
    EXPECT_EQ(recovered[1], changed);
  }

  TEST(UpnpMappingJournalTest, RefreshesDoNotDuplicateAndCompletedRemovalClearsIntent) {
    auto mapping = sample_mapping();
    auto events = upnp::mapping_journal_event(mapping, true);
    mapping.generation = 123;
    mapping.permanent = true;
    events += upnp::mapping_journal_event(mapping, true);
    EXPECT_EQ(upnp::read_mapping_journal(events).size(), 1);
    events += upnp::mapping_journal_event(mapping, false);
    events += upnp::mapping_journal_event(mapping, false);
    EXPECT_TRUE(upnp::read_mapping_journal(events).empty());
  }

  TEST(UpnpMappingJournalTest, IgnoresInterruptedWritesWithoutLosingEarlierRules) {
    const auto mapping = sample_mapping();
    const auto complete = upnp::mapping_journal_event(mapping, true);
    const auto recovered = upnp::read_mapping_journal(complete + "+ \"http://incomplete");
    ASSERT_EQ(recovered.size(), 1);
    EXPECT_EQ(recovered.front(), mapping);
    EXPECT_TRUE(upnp::read_mapping_journal(complete + "+ \"http://incomplete" + upnp::mapping_journal_event(mapping, false)).empty());
  }

  TEST(UpnpMappingJournalTest, IgnoresInvalidEventsAndIncompleteIdentities) {
    auto mapping = sample_mapping();
    auto event = upnp::mapping_journal_event(mapping, true);
    event[1] = '?';
    EXPECT_TRUE(upnp::read_mapping_journal(event).empty());
    mapping.protocol = "SCTP";
    EXPECT_TRUE(upnp::read_mapping_journal(upnp::mapping_journal_event(mapping, true)).empty());
    mapping = sample_mapping();
    mapping.control_url.clear();
    EXPECT_TRUE(upnp::read_mapping_journal(upnp::mapping_journal_event(mapping, true)).empty());
    EXPECT_TRUE(upnp::read_mapping_journal("\nmalformed\n").empty());
  }

  TEST(UpnpMappingJournalTest, PreservesQuotedDescriptionsAndEndpointCharacters) {
    auto mapping = sample_mapping();
    mapping.description = "Sunshine - \"Client HTTP\"";
    mapping.control_url = "http://192.168.1.1/control?value=\\quoted";
    const auto recovered = upnp::read_mapping_journal(upnp::mapping_journal_event(mapping, true));
    ASSERT_EQ(recovered.size(), 1);
    EXPECT_EQ(recovered.front(), mapping);
  }

  TEST(UpnpMappingJournalTest, RefusesLineBreaksThatWouldSplitARecoveryRecord) {
    auto mapping = sample_mapping();
    mapping.control_url += "\nextra";
    EXPECT_TRUE(upnp::mapping_journal_event(mapping, true).empty());
    mapping = sample_mapping();
    mapping.description += '\r';
    EXPECT_TRUE(upnp::mapping_journal_event(mapping, true).empty());
  }
}  // namespace
