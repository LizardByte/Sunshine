/**
 * @file tests/unit/test_upnp.cpp
 * @brief Test src/upnp.*
 */

// test includes
#include "../tests_common.h"

// local includes
#include <src/upnp.h>

TEST(UpnpIsValidIgdTest, RejectsNoIgd) {
  ASSERT_FALSE(upnp::is_valid_igd(UPNP_NO_IGD));
}

TEST(UpnpIsValidIgdTest, AcceptsConnectedIgd) {
  ASSERT_TRUE(upnp::is_valid_igd(UPNP_CONNECTED_IGD));
}

#if (MINIUPNPC_API_VERSION >= 18)
TEST(UpnpIsValidIgdTest, AcceptsPrivateIpIgd) {
  // MiniUPnPc 2.2.8+ reports a valid connected IGD with a private IP address as UPNP_PRIVATEIP_IGD.
  ASSERT_TRUE(upnp::is_valid_igd(UPNP_PRIVATEIP_IGD));
}
#endif

TEST(UpnpIsValidIgdTest, AcceptsDisconnectedIgd) {
  // Regression test for #5462: MiniUPnPc 2.2.8+ reports a valid but disconnected IGD as
  // UPNP_DISCONNECTED_IGD. The old check (status == 1 || status == 2) rejected it, so UPnP port
  // mapping silently failed for routers advertising a valid IGD with NewConnectionStatus=Disconnected.
  ASSERT_TRUE(upnp::is_valid_igd(UPNP_DISCONNECTED_IGD));
}

TEST(UpnpIsValidIgdTest, RejectsUnknownDevice) {
  ASSERT_FALSE(upnp::is_valid_igd(UPNP_UNKNOWN_DEVICE));
}

TEST(UpnpIsValidIgdTest, RejectsOutOfRange) {
  ASSERT_FALSE(upnp::is_valid_igd(-1));
  ASSERT_FALSE(upnp::is_valid_igd(5));
  ASSERT_FALSE(upnp::is_valid_igd(100));
}
