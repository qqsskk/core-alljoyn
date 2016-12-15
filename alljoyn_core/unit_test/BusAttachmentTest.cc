/******************************************************************************
 *  * Copyright (c) Open Connectivity Foundation (OCF) and AllJoyn Open
 *    Source Project (AJOSP) Contributors and others.
 *
 *    SPDX-License-Identifier: Apache-2.0
 *
 *    All rights reserved. This program and the accompanying materials are
 *    made available under the terms of the Apache License, Version 2.0
 *    which accompanies this distribution, and is available at
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Copyright (c) Open Connectivity Foundation and Contributors to AllSeen
 *    Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for
 *    any purpose with or without fee is hereby granted, provided that the
 *    above copyright notice and this permission notice appear in all
 *    copies.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *     WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *     WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *     AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *     DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *     PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *     TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *     PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/
#include <qcc/platform.h>

#include <signal.h>
#include <stdio.h>
#include <vector>

#include <qcc/String.h>
#include <qcc/Thread.h>

#include <alljoyn/ApplicationStateListener.h>
#include <alljoyn/BusAttachment.h>
#include <alljoyn/DBusStd.h>
#include <alljoyn/AllJoynStd.h>
#include <alljoyn/BusObject.h>
#include <alljoyn/MsgArg.h>
#include <alljoyn/version.h>

#include <alljoyn/Status.h>

#include "BusInternal.h"

/* Header files included for Google Test Framework */
#include <gtest/gtest.h>
#include "ajTestCommon.h"
//#include <qcc/time.h>

using namespace std;
using namespace qcc;
using namespace ajn;

class TestApplicationStateListener : public ApplicationStateListener {
  public:
    virtual void State(const char* busName, const qcc::KeyInfoNISTP256& publicKeyInfo, PermissionConfigurator::ApplicationState state)
    {
        QCC_UNUSED(busName);
        QCC_UNUSED(publicKeyInfo);
        QCC_UNUSED(state);
    }
};

class TestECDHEAuthListener : public DefaultECDHEAuthListener {
  public:
    volatile int authCount;
    TestECDHEAuthListener() : authCount(0) { }
    virtual void AuthenticationComplete(const char*, const char*, bool) { ++authCount; }
};

class BusAttachmentTest : public testing::Test {
  public:
    BusAttachment bus;
    TestECDHEAuthListener* authListener;
    TestApplicationStateListener testListener;
    TestApplicationStateListener* nullListener;

    BusAttachmentTest() :
        bus("BusAttachmentTest", false),
        authListener(nullptr),
        nullListener(nullptr)
    {
        EXPECT_EQ(ER_OK, BusAttachment::DeleteDefaultKeyStore("BusAttachmentTest"));
    };

    virtual void SetUp() {
        ASSERT_EQ(ER_OK, bus.Start());
        ASSERT_FALSE(bus.IsConnected());
        ASSERT_EQ(ER_OK, bus.Connect(getConnectArg().c_str()));
        ASSERT_TRUE(bus.IsConnected());
    }

    virtual void TearDown() {
        bus.Stop();
        bus.Join();
        if (authListener != nullptr) {
            delete authListener;
            authListener = nullptr;
        }
    }

};

TEST_F(BusAttachmentTest, shouldReturnErrorWhenRegisteringNullApplicationStateListener)
{
    EXPECT_EQ(ER_INVALID_ADDRESS, bus.RegisterApplicationStateListener(*nullListener));
}

TEST_F(BusAttachmentTest, shouldReturnErrorWhenUnregisteringNullApplicationStateListener)
{
    EXPECT_EQ(ER_INVALID_ADDRESS, bus.UnregisterApplicationStateListener(*nullListener));
}

TEST_F(BusAttachmentTest, shouldNotHaveMatchRuleWithoutRegisteredListener)
{
    EXPECT_EQ(ER_BUS_MATCH_RULE_NOT_FOUND, bus.RemoveMatch(BusAttachment::Internal::STATE_MATCH_RULE));
}

TEST_F(BusAttachmentTest, shouldNotAddMatchRuleWhenFailedToRegisterListener)
{
    ASSERT_EQ(ER_INVALID_ADDRESS, bus.RegisterApplicationStateListener(*nullListener));

    EXPECT_EQ(ER_BUS_MATCH_RULE_NOT_FOUND, bus.RemoveMatch(BusAttachment::Internal::STATE_MATCH_RULE));
}

TEST_F(BusAttachmentTest, shouldReturnErrorWhenUnregisteringUnknownApplicationStateListener)
{
    EXPECT_EQ(ER_APPLICATION_STATE_LISTENER_NO_SUCH_LISTENER, bus.UnregisterApplicationStateListener(testListener));
}

TEST_F(BusAttachmentTest, shouldRegisterApplicationStateListener)
{
    EXPECT_EQ(ER_OK, bus.RegisterApplicationStateListener(testListener));
}

TEST_F(BusAttachmentTest, shouldAddMatchRuleWhenListenerWasRegistered)
{
    ASSERT_EQ(ER_OK, bus.RegisterApplicationStateListener(testListener));

    EXPECT_EQ(ER_OK, bus.RemoveMatch(BusAttachment::Internal::STATE_MATCH_RULE));
}

TEST_F(BusAttachmentTest, shouldReturnErrorWhenRegisteringSameApplicationStateListenerTwice)
{
    ASSERT_EQ(ER_OK, bus.RegisterApplicationStateListener(testListener));

    EXPECT_EQ(ER_APPLICATION_STATE_LISTENER_ALREADY_EXISTS, bus.RegisterApplicationStateListener(testListener));
}

TEST_F(BusAttachmentTest, shouldUnregisterApplicationStateListener)
{
    ASSERT_EQ(ER_OK, bus.RegisterApplicationStateListener(testListener));

    EXPECT_EQ(ER_OK, bus.UnregisterApplicationStateListener(testListener));
}

TEST_F(BusAttachmentTest, shouldRemoveMatchRuleAfterUnregisteringListener)
{
    ASSERT_EQ(ER_OK, bus.RegisterApplicationStateListener(testListener));
    ASSERT_EQ(ER_OK, bus.UnregisterApplicationStateListener(testListener));

    EXPECT_EQ(ER_BUS_MATCH_RULE_NOT_FOUND, bus.RemoveMatch(BusAttachment::Internal::STATE_MATCH_RULE));
}

TEST_F(BusAttachmentTest, shouldNotRemoveMatchRuleWhenFailedToUnregisterListener)
{
    ASSERT_EQ(ER_OK, bus.RegisterApplicationStateListener(testListener));
    ASSERT_EQ(ER_INVALID_ADDRESS, bus.UnregisterApplicationStateListener(*nullListener));

    EXPECT_EQ(ER_OK, bus.RemoveMatch(BusAttachment::Internal::STATE_MATCH_RULE));
}

TEST_F(BusAttachmentTest, IsConnected)
{
    EXPECT_TRUE(bus.IsConnected());
    QStatus disconnectStatus = bus.Disconnect();
    EXPECT_EQ(ER_OK, disconnectStatus);
    if (ER_OK == disconnectStatus) {
        EXPECT_FALSE(bus.IsConnected());
    }
}

/*
 * Call Disconnect without any parameters.
 * Rest of test is identical to the IsConnected test
 */
TEST_F(BusAttachmentTest, Disconnect)
{
    EXPECT_TRUE(bus.IsConnected());
    QStatus disconnectStatus = bus.Disconnect();
    EXPECT_EQ(ER_OK, disconnectStatus);
    if (ER_OK == disconnectStatus) {
        EXPECT_FALSE(bus.IsConnected());
    }
}

TEST_F(BusAttachmentTest, FindName_Same_Name)
{
    QStatus status = ER_OK;

    const char* requestedName = "org.alljoyn.bus.BusAttachmentTest.advertise";

    /* flag indicates that Fail if name cannot be immediatly obtained */
    status = bus.FindAdvertisedName(requestedName);
    EXPECT_EQ(ER_OK, status);

    status = bus.FindAdvertisedName(requestedName);
    EXPECT_EQ(ER_ALLJOYN_FINDADVERTISEDNAME_REPLY_ALREADY_DISCOVERING, status);


    status = bus.CancelFindAdvertisedName(requestedName);
    EXPECT_EQ(ER_OK, status);
}


TEST_F(BusAttachmentTest, FindName_Null_Name)
{
    QStatus status = ER_OK;

    const char* requestedName = NULL;

    /* flag indicates that Fail if name cannot be immediatly obtained */
    status = bus.FindAdvertisedName(requestedName);
    EXPECT_EQ(ER_BAD_ARG_1, status);
}

String nameA;
String nameB;
bool foundNameA = false;
bool foundNameB = false;

class FindMulipleNamesBusListener : public BusListener {
    void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix) {
        QCC_UNUSED(transport);

        printf("FoundAdvertisedName name=%s  prefix=%s\n", name, namePrefix);
        if (strcmp(name, nameA.c_str()) == 0) {
            foundNameA = true;
        }
        if (strcmp(name, nameB.c_str()) == 0) {
            foundNameB = true;
        }
    }
};

TEST_F(BusAttachmentTest, find_multiple_names)
{
    QStatus status = ER_FAIL;
    FindMulipleNamesBusListener testBusListener;
    bus.RegisterBusListener(testBusListener);

    nameA = genUniqueName(bus);
    nameB = genUniqueName(bus);
    foundNameA = false;
    foundNameB = false;

    status = bus.FindAdvertisedName(nameA.c_str());
    EXPECT_EQ(ER_OK, status);
    status = bus.FindAdvertisedName(nameB.c_str());
    EXPECT_EQ(ER_OK, status);

    BusAttachment otherBus("BusAttachmentTestOther", true);
    status = otherBus.Start();
    EXPECT_EQ(ER_OK, status);
    status = otherBus.Connect(getConnectArg().c_str());
    EXPECT_EQ(ER_OK, status);

    status = otherBus.AdvertiseName(nameA.c_str(), TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);
    status = otherBus.AdvertiseName(nameB.c_str(), TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);

    // Wait for the both found name signals to complete.
    for (int i = 0; i < 800; ++i) {
        qcc::Sleep(WAIT_TIME_10);
        if (foundNameA && foundNameB) {
            break;
        }
    }


    EXPECT_TRUE(foundNameA);
    EXPECT_TRUE(foundNameB);

    status = otherBus.CancelAdvertiseName(nameA.c_str(), TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);
    status = otherBus.CancelAdvertiseName(nameB.c_str(), TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);

    status = bus.CancelFindAdvertisedName(nameB.c_str());
    EXPECT_EQ(ER_OK, status);

    foundNameA = false;
    foundNameB = false;

    status = otherBus.AdvertiseName(nameA.c_str(), TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);
    status = otherBus.AdvertiseName(nameB.c_str(), TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);

    // Wait for the found name signal to complete.
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(WAIT_TIME_10);
        if (foundNameA) {
            break;
        }
    }

    EXPECT_TRUE(foundNameA);
    EXPECT_FALSE(foundNameB);

    status = otherBus.CancelAdvertiseName(nameA.c_str(), TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);
    status = otherBus.CancelAdvertiseName(nameB.c_str(), TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);

    status = bus.CancelFindAdvertisedName(nameA.c_str());
    EXPECT_EQ(ER_OK, status);

    // Must Unregister bus listener or the test will segfault
    bus.UnregisterBusListener(testBusListener);

    otherBus.Stop();
    otherBus.Join();
}

bool foundName1;
bool foundName2;
bool foundName3;
TransportMask transport1;
TransportMask transport2;
TransportMask transport3;
class FindNamesByTransportListener : public BusListener {
    void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix) {
        printf("FoundAdvertisedName name=%s  prefix=%s\n", name, namePrefix);
        if (strcmp(name, "name.x") == 0) {
            transport1 |= transport;
            foundName1 = true;
        }
        if (strcmp(name, "name.y") == 0) {
            transport2 |= transport;
            foundName2 = true;
        }
        if (strcmp(name, "name.z") == 0) {
            transport3 |= transport;
            foundName3 = true;
        }

    }
};

TEST_F(BusAttachmentTest, find_names_by_transport)
{
    QStatus status = ER_FAIL;
    FindNamesByTransportListener testBusListener;
    bus.RegisterBusListener(testBusListener);

    foundName1 = false;
    transport1 = 0;
    foundName2 = false;
    transport2 = 0;
    foundName3 = false;
    transport3 = 0;

    status = bus.FindAdvertisedNameByTransport("name.x", TRANSPORT_TCP);
    EXPECT_EQ(ER_OK, status);
    status = bus.FindAdvertisedNameByTransport("name.y", TRANSPORT_LOCAL);
    EXPECT_EQ(ER_OK, status);
    status = bus.FindAdvertisedNameByTransport("name.z", TRANSPORT_LOCAL);
    EXPECT_EQ(ER_OK, status);
    status = bus.CancelFindAdvertisedNameByTransport("name.z", TRANSPORT_LOCAL);
    EXPECT_EQ(ER_OK, status);

    BusAttachment otherBus("BusAttachmentTestOther", true);
    status = otherBus.Start();
    EXPECT_EQ(ER_OK, status);
    status = otherBus.Connect(getConnectArg().c_str());
    EXPECT_EQ(ER_OK, status);

    status = otherBus.AdvertiseName("name.x", TRANSPORT_LOCAL);
    EXPECT_EQ(ER_OK, status);
    status = otherBus.AdvertiseName("name.y", TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);
    status = otherBus.AdvertiseName("name.z", TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);

    // Wait for the found name signal to complete.
    for (int i = 0; i < 200; ++i) {
        qcc::Sleep(WAIT_TIME_10);
        if (foundName2) {
            break;
        }
    }

    EXPECT_FALSE(foundName1);
    EXPECT_TRUE(foundName2);
    EXPECT_EQ(transport2, TRANSPORT_LOCAL);
    EXPECT_FALSE(foundName3);
    // Must Unregister bus listener or the test will segfault
    bus.UnregisterBusListener(testBusListener);

    otherBus.Stop();
    otherBus.Join();

}

bool foundQuietAdvertisedName = false;
class QuietAdvertiseNameListener : public BusListener {
    void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix) {
        QCC_UNUSED(transport);
        printf("FoundAdvertisedName name=%s  prefix=%s\n", name, namePrefix);
        if (strcmp(name, "org.alljoyn.BusNode.test") == 0) {
            foundQuietAdvertisedName = true;
        }
    }

    void LostAdvertisedName(const char* name, TransportMask transport, const char* namePrefix) {
        QCC_UNUSED(transport);
        printf("LostAdvertisedName name=%s  prefix=%s\n", name, namePrefix);
        if (strcmp(name, "org.alljoyn.BusNode.test") == 0) {
            foundQuietAdvertisedName = false;
        }
    }
};

TEST_F(BusAttachmentTest, quiet_advertise_name)
{
    QStatus status = ER_FAIL;
    foundQuietAdvertisedName = false;
    status = bus.AdvertiseName("quiet@org.alljoyn.BusNode.test", TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);

    BusAttachment otherBus("BusAttachmentTestOther", true);
    status = otherBus.Start();
    EXPECT_EQ(ER_OK, status);
    status = otherBus.Connect(getConnectArg().c_str());
    EXPECT_EQ(ER_OK, status);
    QuietAdvertiseNameListener testBusListener;
    otherBus.RegisterBusListener(testBusListener);
    status = otherBus.FindAdvertisedName("org.alljoyn.BusNode.test");
    EXPECT_EQ(ER_OK, status);

    // Wait for the found name signal to complete.
    for (int i = 0; i < 200; ++i) {
        if (foundQuietAdvertisedName) {
            break;
        }
        qcc::Sleep(WAIT_TIME_10);
    }
    EXPECT_TRUE(foundQuietAdvertisedName);

    bus.CancelAdvertiseName("quiet@org.alljoyn.BusNode.test", TRANSPORT_ANY);
    /*
     * CancelAdvertiseName causes the "LostAdvertisedName" BusListener to be
     * called.  The LostAdvertisedName sets the FountQuietAdvertisedName flag
     * to false.
     */
    // Wait for the found name signal to complete.
    for (int i = 0; i < 200; ++i) {
        if (!foundQuietAdvertisedName) {
            break;
        }
        qcc::Sleep(WAIT_TIME_10);
    }
    EXPECT_FALSE(foundQuietAdvertisedName);
    otherBus.UnregisterBusListener(testBusListener);
    otherBus.Stop();
    otherBus.Join();
}

/*
 * listeners and variables used by the JoinSession Test.
 * This test is a mirror of the JUnit test that goes by the same name
 *
 */
bool found;
bool lost;

class FindNewNameBusListener : public BusListener, public BusAttachmentTest {
    virtual void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix) {
        QCC_UNUSED(name);
        QCC_UNUSED(transport);
        QCC_UNUSED(namePrefix);

        found = true;
        bus.EnableConcurrentCallbacks();
    }
};

bool sessionAccepted = false;
bool sessionJoined = false;
bool onJoined = false;
QStatus joinSessionStatus = ER_FAIL;
int busSessionId = 0;
int otherBusSessionId = 0;
bool sessionLost = false;
SessionListener::SessionLostReason sessionLostReason = SessionListener::ALLJOYN_SESSIONLOST_INVALID;

class JoinSession_SessionPortListener : public SessionPortListener, SessionListener {
  public:
    JoinSession_SessionPortListener(BusAttachment* bus) : bus(bus) { };

    bool AcceptSessionJoiner(SessionPort sessionPort, const char* joiner, const SessionOpts& opts) {
        QCC_UNUSED(joiner);
        QCC_UNUSED(opts);

        if (sessionPort == 42) {
            sessionAccepted = true;
            bus->EnableConcurrentCallbacks();
            return true;
        } else {
            sessionAccepted = false;
            return false;
        }
    }

    void SessionLost(SessionId id, SessionListener::SessionLostReason reason) {
        QCC_UNUSED(id);
        sessionLostReason = reason;
        sessionLost = true;
    }
    void SessionJoined(SessionPort sessionPort, SessionId id, const char* joiner) {
        QCC_UNUSED(joiner);

        if (sessionPort == 42) {
            busSessionId = id;
            sessionJoined = true;
        } else {
            sessionJoined = false;
        }
        bus->SetSessionListener(id, this);
    }
    BusAttachment* bus;
};

class JoinSession_BusListener : public BusListener {
  public:
    JoinSession_BusListener(BusAttachment* bus) : bus(bus) { }

    void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix) {
        QCC_UNUSED(transport);
        QCC_UNUSED(namePrefix);

        found = true;
        SessionOpts sessionOpts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);

        SessionId sessionId = 0;
        // Since we are using blocking form of joinSession, we need to enable concurrency
        bus->EnableConcurrentCallbacks();
        // Join session once the AdvertisedName has been found
        joinSessionStatus = bus->JoinSession(name, 42, &sessionListener, sessionId, sessionOpts);
        otherBusSessionId = sessionId;

    }
    BusAttachment* bus;
    SessionListener sessionListener;
};

TEST_F(BusAttachmentTest, JoinLeaveSession) {
    QStatus status = ER_FAIL;

    // Initialize test specific globals
    sessionAccepted = false;
    sessionJoined = false;
    onJoined = false;
    joinSessionStatus = ER_FAIL;
    busSessionId = 0;
    otherBusSessionId = 0;
    sessionLost = false;
    sessionLostReason = SessionListener::ALLJOYN_SESSIONLOST_INVALID;

    // Set up SessionOpts
    SessionOpts sessionOpts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);

    // User defined sessionPort Number
    SessionPort sessionPort = 42;

    // BindSessionPort new SessionPortListener
    JoinSession_SessionPortListener sessionPortListener(&bus);
    status = bus.BindSessionPort(sessionPort, sessionOpts, sessionPortListener);
    EXPECT_EQ(ER_OK, status);

    // Request name from bus
    int flag = DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE;
    status = bus.RequestName("org.alljoyn.bus.BusAttachmentTest.advertise", flag);
    EXPECT_EQ(ER_OK, status);

    // Advertise same bus name
    status = bus.AdvertiseName("org.alljoyn.bus.BusAttachmentTest.advertise", TRANSPORT_ANY);
    EXPECT_EQ(ER_OK, status);

    // Create Second BusAttachment
    BusAttachment otherBus("BusAttachemntTest.JoinSession", true);
    status = otherBus.Start();
    EXPECT_EQ(ER_OK, status);
    status = otherBus.Connect(getConnectArg().c_str());
    EXPECT_EQ(ER_OK, status);

    // Register BusListener for the foundAdvertisedName Listener
    JoinSession_BusListener busListener(&otherBus);
    otherBus.RegisterBusListener(busListener);

    // Find the AdvertisedName
    status = otherBus.FindAdvertisedName("org.alljoyn.bus.BusAttachmentTest.advertise");
    EXPECT_EQ(ER_OK, status);

    for (size_t i = 0; i < 1000; ++i) {
        if (found) {
            break;
        }
        qcc::Sleep(WAIT_TIME_5);
    }

    EXPECT_TRUE(found);

    for (size_t i = 0; i < 1000; ++i) {
        if (sessionAccepted && sessionJoined && otherBusSessionId) {
            break;
        }
        qcc::Sleep(WAIT_TIME_5);
    }

    EXPECT_EQ(ER_OK, joinSessionStatus);
    EXPECT_TRUE(sessionAccepted);
    EXPECT_TRUE(sessionJoined);
    EXPECT_EQ(busSessionId, otherBusSessionId);

    sessionLost = false;

    status = otherBus.LeaveSession(otherBusSessionId);
    for (size_t i = 0; i < 200; ++i) {
        if (sessionLost) {
            break;
        }
        qcc::Sleep(WAIT_TIME_5);
    }
    EXPECT_TRUE(sessionLost);
    EXPECT_EQ(SessionListener::ALLJOYN_SESSIONLOST_REMOTE_END_LEFT_SESSION, sessionLostReason);

    otherBus.Stop();
    otherBus.Join();

    // Remove the session port listener allocated on the stack, before it gets destroyed
    EXPECT_EQ(ER_OK, bus.UnbindSessionPort(sessionPort));
}

TEST_F(BusAttachmentTest, GetDBusProxyObj) {
    ProxyBusObject dBusProxyObj(bus.GetDBusProxyObj());

    MsgArg msgArg[2];
    msgArg[0].Set("s", "org.alljoyn.test.BusAttachment");
    msgArg[1].Set("u", DBUS_NAME_FLAG_ALLOW_REPLACEMENT | DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE);
    Message replyMsg(bus);

    QStatus status = dBusProxyObj.MethodCall(ajn::org::freedesktop::DBus::WellKnownName, "RequestName", msgArg, 2, replyMsg);
    EXPECT_EQ(ER_OK, status);

    unsigned int requestNameResponce;
    replyMsg->GetArg(0)->Get("u", &requestNameResponce);
    EXPECT_EQ(DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER, requestNameResponce);
}

TEST_F(BusAttachmentTest, Ping_self) {
    ASSERT_EQ(ER_OK, bus.Ping(bus.GetUniqueName().c_str(), 1000));
}

TEST_F(BusAttachmentTest, Ping_bad_wellknownName) {
    QStatus status = bus.Ping(":1badNaME.2", 500);
    ASSERT_EQ(ER_ALLJOYN_PING_REPLY_UNKNOWN_NAME, status);
}

TEST_F(BusAttachmentTest, Ping_null_ptr) {
    ASSERT_EQ(ER_BUS_BAD_BUS_NAME, bus.Ping(NULL, 500));
}

TEST_F(BusAttachmentTest, Ping_other_on_same_bus) {
    BusAttachment otherBus("BusAttachment OtherBus", false);

    QStatus status = ER_OK;
    status = otherBus.Start();
    ASSERT_EQ(ER_OK, status);
    status = otherBus.Connect(getConnectArg().c_str());
    ASSERT_EQ(ER_OK, status);

    ASSERT_EQ(ER_OK, bus.Ping(otherBus.GetUniqueName().c_str(), 1000));

    otherBus.Stop();
    otherBus.Join();
}


static bool pingAsyncFlag = false;

class TestPingAsyncCB : public BusAttachment::PingAsyncCB {
  public:
    TestPingAsyncCB() : m_status(ER_FAIL), m_context(NULL) { }

    void PingCB(QStatus status, void* context) {
        m_status = status;
        m_context = context;
        pingAsyncFlag = true;
    }
    QStatus m_status;
    void* m_context;
};

TEST_F(BusAttachmentTest, Ping_self_async) {
    pingAsyncFlag = false;
    TestPingAsyncCB pingCB;
    const char* contextStr = "PingContextTestString";
    ASSERT_EQ(ER_OK, bus.PingAsync(bus.GetUniqueName().c_str(), 1000, &pingCB, (void*)contextStr));

    for (uint32_t msecs = 0; msecs < LOOP_END_1100; msecs += WAIT_TIME_5) {
        if (pingAsyncFlag) {
            break;
        }
        qcc::Sleep(WAIT_TIME_5);
    }

    EXPECT_EQ(ER_OK, pingCB.m_status);
    EXPECT_STREQ(contextStr, (char*)pingCB.m_context);
}

TEST_F(BusAttachmentTest, PingAsync_other_on_same_bus) {
    BusAttachment otherBus("BusAttachment OtherBus", false);

    QStatus status = ER_OK;
    status = otherBus.Start();
    ASSERT_EQ(ER_OK, status);
    status = otherBus.Connect(getConnectArg().c_str());
    ASSERT_EQ(ER_OK, status);

    pingAsyncFlag = false;
    TestPingAsyncCB pingCB;
    const char* contextStr = "PingOtherContextTestString";
    ASSERT_EQ(ER_OK, bus.PingAsync(otherBus.GetUniqueName().c_str(), 1000,  &pingCB, (void*)contextStr));

    for (uint32_t msecs = 0; msecs < LOOP_END_1100; msecs += WAIT_TIME_5) {
        if (pingAsyncFlag) {
            break;
        }
        qcc::Sleep(WAIT_TIME_5);
    }

    EXPECT_EQ(ER_OK, pingCB.m_status);
    EXPECT_STREQ(contextStr, (char*)pingCB.m_context);

    otherBus.Stop();
    otherBus.Join();
}

TEST_F(BusAttachmentTest, BasicSecureConnection)
{
    BusAttachment otherBus("BusAttachmentOtherBus", false);
    EXPECT_EQ(ER_OK, BusAttachment::DeleteDefaultKeyStore("BusAttachmentOtherBus"));
    ASSERT_EQ(ER_BUS_NOT_CONNECTED, otherBus.SecureConnection(bus.GetUniqueName().c_str()));
    otherBus.Start();
    // Use expect from now onward to make sure we reached the end of the function and do all clean-up
    EXPECT_EQ(ER_BUS_NOT_CONNECTED, otherBus.SecureConnection(bus.GetUniqueName().c_str()));
    otherBus.Connect();
    EXPECT_EQ(ER_BUS_SECURITY_NOT_ENABLED, otherBus.SecureConnection(bus.GetUniqueName().c_str()));

    authListener = new TestECDHEAuthListener();
    EXPECT_EQ(ER_OK, otherBus.EnablePeerSecurity("ALLJOYN_ECDHE_NULL", authListener, "myOtherTestKeyStore", true));
    EXPECT_EQ(ER_OK, bus.EnablePeerSecurity("ALLJOYN_ECDHE_NULL", authListener, "myTestKeyStore", true));

    EXPECT_EQ(ER_OK, otherBus.SecureConnection(bus.GetUniqueName().c_str()));
    EXPECT_EQ(authListener->authCount, 2);

    otherBus.Stop();
    otherBus.ClearKeyStore();
    otherBus.Join();
    bus.ClearKeyStore();
}

TEST_F(BusAttachmentTest, BasicSecureConnectionAsync)
{
    BusAttachment otherBus("BusAttachmentOtherBus", false);
    ASSERT_EQ(ER_BUS_NOT_CONNECTED, otherBus.SecureConnectionAsync(bus.GetUniqueName().c_str()));
    otherBus.Start();
    // Use expect from now onward to make sure we reached the end of the function and do all clean-up
    EXPECT_EQ(ER_BUS_NOT_CONNECTED, otherBus.SecureConnectionAsync(bus.GetUniqueName().c_str()));
    otherBus.Connect();
    EXPECT_EQ(ER_BUS_SECURITY_NOT_ENABLED, otherBus.SecureConnectionAsync(bus.GetUniqueName().c_str()));

    authListener = new TestECDHEAuthListener();

    EXPECT_EQ(ER_OK, otherBus.EnablePeerSecurity("ALLJOYN_ECDHE_NULL", authListener, "myOtherTestKeyStore", true));
    EXPECT_EQ(ER_OK, bus.EnablePeerSecurity("ALLJOYN_ECDHE_NULL", authListener, "myTestKeyStore", true));

    EXPECT_EQ(ER_OK, bus.SecureConnectionAsync(otherBus.GetUniqueName().c_str()));

    // Wait for the authentication to complete.
    for (int i = 0; i < 200; ++i) {
        if (authListener->authCount == 2) {
            break;
        }
        qcc::Sleep(WAIT_TIME_10);
    }
    EXPECT_EQ(authListener->authCount, 2);

    otherBus.Stop();
    otherBus.ClearKeyStore();
    otherBus.Join();
    bus.ClearKeyStore();
}