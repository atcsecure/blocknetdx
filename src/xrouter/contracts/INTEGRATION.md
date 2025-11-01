# XRouter Payment Channel Integration Guide

This guide explains how to integrate trustless payment channels into the existing XRouter codebase.

## Integration Overview

The payment channel system integrates with XRouter at several points:

1. **Client Side**: Modify request creation to use channel payments
2. **Server Side**: Modify request processing to verify channel payments
3. **Configuration**: Add payment channel settings
4. **RPC Interface**: Expose payment channel commands

## Step-by-Step Integration

### 1. Modify XRouter Request Flow

#### Current Flow (Transaction-based)

```
Client                                    Service Node
  |                                            |
  |--- Create blockchain transaction -------->|
  |                                            |
  |--- Send XRouter request with tx hash ---->|
  |                                            |
  |                                       Verify tx
  |                                       Broadcast tx
  |                                       Process request
  |                                            |
  |<---------- Send response ------------------|
```

#### New Flow (Channel-based)

```
Client                                    Service Node
  |                                            |
  |--- Create signed state update ----------->|
  |                                            |
  |                                       Verify signature
  |                                       Update channel state
  |                                       Process request
  |                                            |
  |<---------- Send response ------------------|
```

### 2. Code Modifications

#### A. Modify `xrouterapp.cpp`

Add payment channel support to request generation:

```cpp
// In xrouterapp.cpp

bool App::generatePaymentForRequest(
    const NodeAddr & nodeAddr,
    const std::string & command,
    const CAmount & fee,
    std::string & payment,
    bool useChannels = true)  // New parameter
{
    if (fee <= 0) {
        payment.clear();
        return true;
    }

    // Check if payment channels are enabled
    if (useChannels && gArgs.GetBoolArg("-xrouterpaymentchannels", false)) {
        // Try to use payment channel first
        auto openChannels = g_paymentChannelManager.GetChannelsByServiceNode(nodeAddr);

        for (const auto& channelId : openChannels) {
            auto channel = g_paymentChannelManager.GetChannel(channelId);
            if (!channel || !channel->IsOpen())
                continue;

            // Check if channel has sufficient balance
            if (channel->clientBalance >= fee) {
                ChannelStateUpdate stateUpdate;
                if (g_paymentChannelManager.CreatePayment(channelId, fee, stateUpdate)) {
                    // Serialize state update as payment proof
                    payment = HexStr(stateUpdate.Serialize());
                    return true;
                }
            }
        }

        // No suitable channel found, fall back to transaction
        LogPrint(BCLog::XROUTER, "No suitable payment channel found, using transaction\n");
    }

    // Fall back to traditional transaction-based payment
    return generatePayment(nodeAddr, paymentAddress, fee, payment);
}
```

#### B. Modify `xrouterserver.cpp`

Add payment channel verification to request processing:

```cpp
// In xrouterserver.cpp

bool XRouterServer::verifyPayment(
    const NodeAddr & nodeAddr,
    const std::string & paymentData,
    const CAmount & requiredFee,
    PaymentType & paymentType)
{
    if (requiredFee <= 0) {
        paymentType = PaymentType::NONE;
        return true;
    }

    // Check if payment channels are enabled
    if (gArgs.GetBoolArg("-xrouterpaymentchannels", false)) {
        // Try to parse as channel state update
        try {
            std::vector<unsigned char> data = ParseHex(paymentData);
            ChannelStateUpdate stateUpdate;

            if (stateUpdate.Deserialize(data)) {
                // Verify and process channel payment
                if (g_paymentChannelManager.VerifyPayment(stateUpdate)) {
                    paymentType = PaymentType::CHANNEL;
                    return true;
                }
            }
        } catch (...) {
            // Not a channel payment, try transaction
        }
    }

    // Fall back to transaction verification
    if (checkFeePayment(nodeAddr, paymentAddress, paymentData, requiredFee)) {
        paymentType = PaymentType::TRANSACTION;
        return true;
    }

    return false;
}
```

#### C. Modify `xrouterpacket.h`

Add payment type to packet structure:

```cpp
// In xrouterpacket.h

enum class PaymentType {
    NONE = 0,
    TRANSACTION = 1,
    CHANNEL = 2
};

class XRouterPacket {
    // ... existing fields ...

    PaymentType paymentType{PaymentType::NONE};
    std::string paymentData;

    // ... existing methods ...

    void setPaymentType(PaymentType type) { paymentType = type; }
    PaymentType getPaymentType() const { return paymentType; }
};
```

### 3. Configuration Setup

#### blocknet.conf

Add payment channel configuration options:

```ini
# Enable payment channels
xrouterpaymentchannels=1

# Ethereum node RPC endpoint
xrouter.ethereum.rpc=http://localhost:8545

# Payment channel contract address (deployed contract)
xrouter.paymentchannel.contract=0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb

# Default challenge period (seconds)
xrouter.paymentchannel.challengeperiod=3600

# Minimum channel capacity
xrouter.paymentchannel.mincapacity=1.0

# Auto-open channels with frequently used service nodes
xrouter.paymentchannel.autoopen=1

# Service node configuration
# Accept payment channels
xrouter.acceptchannels=1

# Ethereum address for receiving channel payments
xrouter.ethereum.address=0x1234567890abcdef1234567890abcdef12345678

# Ethereum private key (for signing)
xrouter.ethereum.privatekey=0xabcdef...
```

### 4. Automatic Channel Management

Implement automatic channel opening for frequently used service nodes:

```cpp
// In xrouterapp.cpp

void App::ensurePaymentChannel(const NodeAddr & nodeAddr, CAmount estimatedUsage)
{
    if (!gArgs.GetBoolArg("-xrouter.paymentchannel.autoopen", false))
        return;

    // Check if we have an open channel
    auto channels = g_paymentChannelManager.GetChannelsByServiceNode(nodeAddr);
    bool hasOpenChannel = false;

    for (const auto& channelId : channels) {
        auto channel = g_paymentChannelManager.GetChannel(channelId);
        if (channel && channel->IsOpen() && channel->clientBalance >= estimatedUsage) {
            hasOpenChannel = true;
            break;
        }
    }

    if (!hasOpenChannel) {
        // Open new channel with minimum capacity
        CAmount minCapacity = AmountFromValue(
            gArgs.GetArg("-xrouter.paymentchannel.mincapacity", "1.0")
        );

        CAmount depositAmount = std::max(estimatedUsage * 10, minCapacity);

        // Get service node's Ethereum address and pubkey
        std::string serviceNodeAddress = getServiceNodeEthAddress(nodeAddr);
        CPubKey serviceNodePubKey = getServiceNodePubKey(nodeAddr);

        uint64_t challengePeriod = gArgs.GetArg(
            "-xrouter.paymentchannel.challengeperiod",
            3600
        );

        uint256 channelId = g_paymentChannelManager.OpenChannel(
            serviceNodeAddress,
            serviceNodePubKey,
            depositAmount,
            challengePeriod
        );

        if (!channelId.IsNull()) {
            LogPrint(BCLog::XROUTER,
                "Automatically opened payment channel %s with %s\n",
                channelId.GetHex(),
                nodeAddr
            );
        }
    }
}
```

### 5. Channel Monitoring and Management

Implement background tasks for channel maintenance:

```cpp
// In xrouterapp.cpp

void App::monitorPaymentChannels()
{
    // Run periodically (e.g., every 5 minutes)
    static std::chrono::steady_clock::time_point lastRun;
    auto now = std::chrono::steady_clock::now();

    if (now - lastRun < std::chrono::minutes(5))
        return;

    lastRun = now;

    // 1. Sync all channels with blockchain
    auto allChannels = g_paymentChannelManager.GetOpenChannels();

    for (const auto& channelId : allChannels) {
        g_paymentChannelManager.SyncChannelFromBlockchain(channelId);

        auto channel = g_paymentChannelManager.GetChannel(channelId);
        if (!channel)
            continue;

        // 2. Check for challenged channels that need dispute
        if (channel->state == ChannelState::CHALLENGED) {
            // Check if we have a newer state to dispute with
            if (channel->latestState.nonce > channel->nonce) {
                LogPrint(BCLog::XROUTER,
                    "Disputing close of channel %s with newer state\n",
                    channelId.GetHex()
                );

                g_paymentChannelManager.DisputeClose(
                    channelId,
                    channel->latestState
                );
            }
        }

        // 3. Finalize channels past challenge period
        if (channel->state == ChannelState::CHALLENGED) {
            uint64_t currentTime = GetTime();
            if (currentTime >= channel->closingTime + channel->challengePeriod) {
                LogPrint(BCLog::XROUTER,
                    "Finalizing close of channel %s\n",
                    channelId.GetHex()
                );

                g_paymentChannelManager.FinalizeClose(channelId);
            }
        }

        // 4. Top up low-balance channels
        if (channel->IsOpen() && channel->clientBalance < channel->clientDeposit / 10) {
            CAmount topUpAmount = channel->clientDeposit / 2;

            LogPrint(BCLog::XROUTER,
                "Topping up channel %s with %d\n",
                channelId.GetHex(),
                topUpAmount
            );

            g_paymentChannelManager.AddDeposit(channelId, topUpAmount, true);
        }
    }
}
```

### 6. Service Node Integration

Service nodes need to accept and track channel payments:

```cpp
// In xrouterserver.cpp

bool XRouterServer::processChannelPayment(const ChannelStateUpdate& stateUpdate)
{
    // 1. Verify the payment
    if (!g_paymentChannelManager.VerifyPayment(stateUpdate)) {
        LogPrint(BCLog::XROUTER, "Invalid channel payment\n");
        return false;
    }

    // 2. Store the state update (for potential disputes)
    storeChannelState(stateUpdate);

    // 3. Update our records
    auto channel = g_paymentChannelManager.GetChannel(stateUpdate.channelId);
    if (!channel) {
        LogPrint(BCLog::XROUTER, "Channel not found\n");
        return false;
    }

    // 4. Sign the state update to acknowledge
    ChannelStateUpdate acknowledgment = stateUpdate;
    if (!g_paymentChannelManager.SignState(acknowledgment, false)) {
        LogPrint(BCLog::XROUTER, "Failed to sign state acknowledgment\n");
        return false;
    }

    // 5. Send acknowledgment back to client (optional but good practice)
    sendChannelStateAcknowledgment(acknowledgment);

    LogPrint(BCLog::XROUTER,
        "Processed channel payment: %d, new balance: %d\n",
        channel->serviceNodeBalance,
        stateUpdate.serviceNodeBalance
    );

    return true;
}
```

### 7. Build System Integration

Update build files to include payment channel code:

#### Makefile.am

```makefile
# Add to BITCOIN_CORE_H
BITCOIN_CORE_H = \
  xrouter/xrouterpaymentchannel.h \
  # ... other headers ...

# Add to libbitcoin_server_a_SOURCES
libbitcoin_server_a_SOURCES = \
  xrouter/xrouterpaymentchannel.cpp \
  xrouter/rpcpaymentchannel.cpp \
  # ... other sources ...
```

### 8. Testing

Create integration tests:

```cpp
// test/xrouter/paymentchannel_tests.cpp

#include <boost/test/unit_test.hpp>
#include <xrouter/xrouterpaymentchannel.h>

BOOST_AUTO_TEST_SUITE(xrouter_paymentchannel_tests)

BOOST_AUTO_TEST_CASE(open_channel_test)
{
    // Test opening a payment channel
    XRouterPaymentChannelManager manager;

    std::string serviceNodeAddr = "0x1234...";
    CPubKey serviceNodePubKey; // Create test pubkey
    CAmount deposit = 5 * COIN;

    uint256 channelId = manager.OpenChannel(
        serviceNodeAddr,
        serviceNodePubKey,
        deposit,
        3600
    );

    BOOST_CHECK(!channelId.IsNull());

    auto channel = manager.GetChannel(channelId);
    BOOST_REQUIRE(channel != nullptr);
    BOOST_CHECK_EQUAL(channel->clientDeposit, deposit);
    BOOST_CHECK_EQUAL(channel->clientBalance, deposit);
    BOOST_CHECK(channel->IsOpen());
}

BOOST_AUTO_TEST_CASE(create_payment_test)
{
    // Test creating a payment
    XRouterPaymentChannelManager manager;

    // ... setup channel ...

    ChannelStateUpdate stateUpdate;
    bool success = manager.CreatePayment(channelId, 0.01 * COIN, stateUpdate);

    BOOST_CHECK(success);
    BOOST_CHECK_EQUAL(stateUpdate.nonce, 1);
    BOOST_CHECK_EQUAL(stateUpdate.clientBalance, 4.99 * COIN);
    BOOST_CHECK_EQUAL(stateUpdate.serviceNodeBalance, 0.01 * COIN);
}

BOOST_AUTO_TEST_SUITE_END()
```

## Migration Path

### Phase 1: Deployment
1. Deploy smart contract to Ethereum
2. Update Blocknet Core with payment channel code
3. Release as optional feature (disabled by default)

### Phase 2: Testing
1. Beta test with select service nodes
2. Monitor channel operations
3. Gather feedback and optimize

### Phase 3: Gradual Rollout
1. Enable by default for new installations
2. Provide migration tools for existing users
3. Support both transaction and channel payments

### Phase 4: Full Adoption
1. Deprecate transaction-based payments
2. Make channels the primary payment method
3. Optimize based on usage patterns

## Backwards Compatibility

The implementation maintains full backwards compatibility:

1. **Dual Payment Support**: Both transaction and channel payments work
2. **Automatic Fallback**: If channel payment fails, fall back to transaction
3. **Opt-in Feature**: Channels are optional and can be disabled
4. **Gradual Migration**: Users and service nodes can migrate at their own pace

## Performance Considerations

### Gas Optimization

The smart contract is optimized for gas efficiency:
- Minimal storage operations
- Efficient data structures
- Batched operations where possible

### Off-chain Efficiency

Payment channels provide significant improvements:
- **99% reduction** in blockchain transactions
- **Instant payments** (no blockchain confirmation needed)
- **Near-zero cost** for individual payments

### Scalability

With payment channels:
- Support **unlimited** XRouter queries per channel
- **No blockchain congestion** from micropayments
- **Linear scaling** with number of channels (not queries)

## Monitoring and Metrics

Track important metrics:

```cpp
// Add to stats collection
struct PaymentChannelStats {
    size_t totalChannels;
    size_t openChannels;
    CAmount totalDeposited;
    CAmount totalPaid;
    uint64_t totalPayments;
    uint64_t avgPaymentsPerChannel;
    CAmount gasSaved;  // vs. transaction-based
};
```

## Conclusion

This integration provides a trustless, scalable payment solution for XRouter while maintaining full backwards compatibility with the existing system. The phased rollout approach allows for safe deployment and testing before full adoption.
