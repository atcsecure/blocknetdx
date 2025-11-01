# XRouter Trustless Payment Channels - Technical Specification

## Executive Summary

This document specifies the implementation of trustless payment channels for the Blocknet XRouter protocol. Payment channels enable off-chain micropayments between clients and service nodes, providing scalability, reduced costs, and instant settlement while maintaining trustless operation through smart contracts.

## Table of Contents

1. [Introduction](#introduction)
2. [Current System](#current-system)
3. [Proposed Solution](#proposed-solution)
4. [Architecture](#architecture)
5. [Smart Contract Specification](#smart-contract-specification)
6. [C++ Implementation](#c-implementation)
7. [Security Analysis](#security-analysis)
8. [Performance Analysis](#performance-analysis)
9. [Migration Strategy](#migration-strategy)
10. [Future Work](#future-work)

## 1. Introduction

### 1.1 Background

XRouter is Blocknet's decentralized oracle and microservice protocol, enabling communication between blockchains and external services. Currently, XRouter uses a transaction-based payment model where each service request requires an on-chain payment transaction.

### 1.2 Problem Statement

The current payment model has several limitations:

1. **Scalability**: Each request requires blockchain confirmation (10-60 seconds)
2. **Cost**: Transaction fees for every micropayment are inefficient
3. **UX**: Delays and fees create friction for users
4. **Blockchain Load**: Thousands of small transactions congest the network

### 1.3 Solution Overview

Implement Layer 2 payment channels that:
- Allow unlimited off-chain payments
- Settle on-chain only when opening/closing
- Maintain trustless security through cryptographic signatures
- Provide instant payment finality

## 2. Current System

### 2.1 Current Payment Flow

```
1. Client creates fee transaction (on-chain)
2. Client includes transaction in XRouter request
3. Service node validates transaction
4. Service node broadcasts transaction
5. Service node processes request after confirmation
6. Service node sends response
```

**Metrics:**
- Average payment time: 30-60 seconds
- Cost per payment: 0.0001 BLOCK (~$0.001) + network fee
- Throughput: Limited by blockchain TPS

### 2.2 Current Implementation

Located in:
- `src/xrouter/utils-payments.cpp` - Payment creation and validation
- `src/xrouter/xrouterapp.cpp` - Client-side payment generation
- `src/xrouter/xrouterserver.cpp` - Server-side payment verification

## 3. Proposed Solution

### 3.1 Payment Channel Concept

A payment channel is a bilateral smart contract that:

1. Locks funds in escrow (smart contract)
2. Allows off-chain state updates (signed messages)
3. Enforces final settlement on-chain

### 3.2 Key Features

#### Trustless Operation
- Funds secured by smart contract
- Cryptographic signatures prevent fraud
- Blockchain enforces final state

#### Scalability
- Unlimited off-chain transactions
- Only 2 on-chain transactions (open, close)
- Instant payment finality

#### Bidirectional
- Payments flow both ways
- Service nodes can refund
- Balance updates efficient

#### Dispute Resolution
- Challenge period for unilateral closes
- Either party can dispute with newer state
- Blockchain arbitrates disputes

### 3.3 Use Cases

1. **High-Frequency Trading Bots**: Make thousands of XRouter calls per hour
2. **Data Aggregators**: Continuous oracle queries
3. **Cross-chain Applications**: Frequent blockchain queries
4. **Service Node Operators**: Accept payments from multiple clients

## 4. Architecture

### 4.1 System Components

```
┌─────────────────────────────────────────────────────────┐
│                    Blocknet Core                         │
│  ┌──────────────┐            ┌─────────────────────┐   │
│  │   XRouter    │            │  Payment Channel    │   │
│  │   Client     │◄──────────►│     Manager         │   │
│  └──────────────┘            └─────────────────────┘   │
│         │                              │                │
│         │                              │                │
│         ▼                              ▼                │
│  ┌──────────────┐            ┌─────────────────────┐   │
│  │   XRouter    │            │   Ethereum RPC      │   │
│  │   Server     │            │     Client          │   │
│  └──────────────┘            └─────────────────────┘   │
└─────────────┬───────────────────────┬──────────────────┘
              │                       │
              │                       │
              ▼                       ▼
      ┌──────────────┐      ┌─────────────────────┐
      │   Service    │      │  Ethereum Network   │
      │   Node       │      │  ┌───────────────┐  │
      └──────────────┘      │  │ PaymentChannel│  │
                            │  │ Smart Contract│  │
                            │  └───────────────┘  │
                            └─────────────────────┘
```

### 4.2 Data Flow

#### Opening Channel

```
Client                 XRouter               Smart Contract
  │                      │                          │
  ├──OpenChannel()──────►│                          │
  │                      ├──createTx()─────────────►│
  │                      │                          │
  │                      │◄─────ChannelOpened──────┤
  │◄─────channelId──────┤                          │
  │                      │                          │
```

#### Making Payment

```
Client                 Service Node
  │                          │
  ├──CreateStateUpdate()────►│
  │   (signed)               │
  │                          ├──VerifySignature()
  │                          │
  │                          ├──UpdateLocalState()
  │                          │
  │◄─────Service Response────┤
  │                          │
```

#### Closing Channel

```
Client                 Smart Contract         Service Node
  │                          │                      │
  ├──CooperativeClose()─────►│◄──Signature────────┤
  │   (final state)          │                      │
  │                          ├──TransferFunds()
  │◄─────ClientBalance───────┤                      │
  │                          ├──────NodeBalance────►│
  │                          │                      │
```

## 5. Smart Contract Specification

### 5.1 Contract Interface

```solidity
interface IXRouterPaymentChannel {
    // Channel management
    function openChannel(address serviceNode, uint256 challengePeriod)
        external payable returns (bytes32 channelId);

    function depositClient(bytes32 channelId) external payable;

    function depositServiceNode(bytes32 channelId) external payable;

    // Closing
    function cooperativeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256 serviceNodeBalance,
        bytes calldata clientSignature,
        bytes calldata serviceNodeSignature
    ) external;

    function challengeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256 serviceNodeBalance,
        bytes calldata signature
    ) external;

    function disputeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256 serviceNodeBalance,
        bytes calldata signature
    ) external;

    function finalizeClose(bytes32 channelId) external;

    // View functions
    function getChannel(bytes32 channelId)
        external view returns (Channel memory);

    function isChannelOpen(bytes32 channelId)
        external view returns (bool);
}
```

### 5.2 State Machine

```
         ┌─────────┐
         │ INVALID │
         └────┬────┘
              │ openChannel()
              ▼
         ┌─────────┐
    ┌───►│  OPEN   │◄───┐
    │    └────┬────┘    │
    │         │         │ disputeClose()
    │         │         │
    │         │ challengeClose()
    │         ▼         │
    │    ┌──────────┐   │
    │    │CHALLENGED├───┘
    │    └────┬─────┘
    │         │ finalizeClose()
    │         │ cooperativeClose()
    │         ▼
    └────┤ CLOSED │
         └────────┘
```

### 5.3 Security Properties

1. **Conservation of Funds**: `clientBalance + serviceNodeBalance = totalDeposits`
2. **Monotonic Nonces**: State updates must have strictly increasing nonces
3. **Valid Signatures**: All state updates require valid signatures
4. **Challenge Protection**: Unilateral closes have dispute period

## 6. C++ Implementation

### 6.1 Key Classes

#### XRouterPaymentChannelManager
- Manages all payment channels
- Creates and verifies payments
- Handles blockchain interactions

#### PaymentChannel
- Represents a single channel
- Tracks balances and state
- Stores latest signed state

#### ChannelStateUpdate
- Represents state transition
- Contains signatures
- Serializable for transmission

### 6.2 Integration Points

#### Client Side (xrouterapp.cpp)

```cpp
// Before making XRouter request
if (usePaymentChannels) {
    ChannelStateUpdate payment;
    if (createChannelPayment(serviceNode, fee, payment)) {
        // Use channel payment
        request.setPayment(payment.Serialize());
    } else {
        // Fallback to transaction
        createTransactionPayment(serviceNode, fee);
    }
}
```

#### Server Side (xrouterserver.cpp)

```cpp
// On receiving XRouter request
bool valid = false;

if (isChannelPayment(request)) {
    ChannelStateUpdate payment;
    payment.Deserialize(request.getPayment());
    valid = verifyChannelPayment(payment);
} else {
    valid = verifyTransactionPayment(request);
}

if (valid) {
    processRequest(request);
}
```

### 6.3 RPC Interface

```bash
# Client commands
xrOpenPaymentChannel "serviceNodeAddr" amount [challengePeriod]
xrCreateChannelPayment "channelId" amount
xrClosePaymentChannel "channelId" [cooperative]

# Query commands
xrGetPaymentChannel "channelId"
xrListPaymentChannels ["serviceNodeAddr"]
xrGetChannelBalance

# Management commands
xrFinalizeChannelClose "channelId"
```

## 7. Security Analysis

### 7.1 Threat Model

#### Malicious Client
**Attack**: Submit old favorable state when closing
**Mitigation**: Monotonic nonces + challenge period

#### Malicious Service Node
**Attack**: Refuse to sign cooperative close
**Mitigation**: Client can unilaterally close

#### Network Attacks
**Attack**: Prevent dispute transaction from being mined
**Mitigation**: Long enough challenge period (1+ hours)

#### Smart Contract Bugs
**Attack**: Exploit contract vulnerability
**Mitigation**: Formal verification, audits, testing

### 7.2 Security Assumptions

1. **Blockchain Security**: Ethereum network is secure
2. **Signature Security**: ECDSA is cryptographically secure
3. **Rational Actors**: Parties act in economic self-interest
4. **Availability**: Parties can submit transactions within challenge period

### 7.3 Attack Scenarios

| Attack | Probability | Impact | Mitigation |
|--------|-------------|--------|------------|
| Old state close | Medium | Medium | Nonce verification |
| Signature forgery | Very Low | Critical | ECDSA security |
| Contract exploit | Low | Critical | Audits, testing |
| Griefing (refuse close) | Medium | Low | Unilateral close |
| Front-running | Low | Low | Challenge period |

## 8. Performance Analysis

### 8.1 Comparison with Current System

| Metric | Current | With Channels | Improvement |
|--------|---------|---------------|-------------|
| Payment time | 30-60s | <1s | 30-60x faster |
| Cost per payment | ~$0.001 | ~$0.000001 | 1000x cheaper |
| Throughput | ~100 TPS | Unlimited | ∞ |
| Blockchain load | 1 tx/request | 1 tx/1000+ requests | 1000x reduction |

### 8.2 Gas Costs

| Operation | Gas Used | Cost (50 gwei) |
|-----------|----------|----------------|
| Open channel | ~150,000 | ~$6 |
| Cooperative close | ~80,000 | ~$3 |
| Challenge close | ~120,000 | ~$4 |
| Dispute | ~100,000 | ~$3.50 |
| Finalize close | ~60,000 | ~$2 |

**Break-even Analysis**:
- Channel profitable after ~100 payments vs. transactions
- Typical channel handles 1,000-10,000 payments

### 8.3 Scalability

```
Without Channels:
1000 requests = 1000 blockchain transactions

With Channels:
1000 requests = 2 blockchain transactions (open + close)
Scalability improvement: 500x
```

## 9. Migration Strategy

### 9.1 Phase 1: Deployment (Month 1-2)
- Deploy smart contract to Ethereum testnet
- Integrate payment channel code into Blocknet Core
- Internal testing and security review
- Deploy to Ethereum mainnet

### 9.2 Phase 2: Beta Testing (Month 3-4)
- Release as experimental feature (opt-in)
- Test with volunteer service nodes
- Monitor performance and gather metrics
- Fix bugs and optimize

### 9.3 Phase 3: Gradual Rollout (Month 5-6)
- Enable by default for new installations
- Provide migration tools
- Support both payment methods
- Educational materials and documentation

### 9.4 Phase 4: Full Adoption (Month 7+)
- Payment channels become primary method
- Transaction-based payments deprecated
- Optimize based on production data
- Consider additional features

### 9.5 Backwards Compatibility

The system maintains full backwards compatibility:
- Both payment methods supported simultaneously
- Automatic fallback to transactions
- No breaking changes to existing APIs
- Service nodes can enable at their own pace

## 10. Future Work

### 10.1 Planned Enhancements

#### Multi-hop Channels
Enable payments through intermediary nodes:
```
Client ──► Intermediary ──► Service Node
```

#### Virtual Channels
Create channels without on-chain transactions using existing channel network.

#### Watchtowers
Automated services that monitor and dispute on behalf of offline users.

#### Cross-chain Channels
Use atomic swaps to enable channels between different blockchains.

### 10.2 Research Areas

1. **Channel Rebalancing**: Algorithms for optimal fund allocation
2. **Routing**: Pathfinding for multi-hop payments
3. **Privacy**: Zero-knowledge proofs for payment amounts
4. **Interoperability**: Standards for cross-implementation compatibility

## Conclusion

XRouter trustless payment channels provide a scalable, cost-effective solution for micropayments while maintaining security through smart contracts. The system offers 1000x cost reduction and unlimited throughput for XRouter services, positioning Blocknet for mass adoption.

## References

1. **Lightning Network**: Bitcoin's payment channel network
2. **Raiden Network**: Ethereum payment channel implementation
3. **State Channels**: Ethereum.org documentation
4. **Sprites**: Academic paper on payment channel improvements
5. **Counterfactual**: Virtual channel instantiation

## Appendices

### Appendix A: File Structure

```
src/xrouter/
├── contracts/
│   ├── XRouterPaymentChannel.sol      # Smart contract
│   ├── README.md                       # User documentation
│   ├── INTEGRATION.md                  # Integration guide
│   └── DEPLOYMENT.md                   # Deployment guide
├── xrouterpaymentchannel.h             # C++ header
├── xrouterpaymentchannel.cpp           # C++ implementation
└── rpcpaymentchannel.cpp               # RPC commands
```

### Appendix B: Configuration Options

```ini
# Enable payment channels
xrouterpaymentchannels=1

# Ethereum node RPC
xrouter.ethereum.rpc=https://mainnet.infura.io/v3/API_KEY

# Contract address
xrouter.paymentchannel.contract=0x...

# Challenge period (seconds)
xrouter.paymentchannel.challengeperiod=3600

# Minimum channel capacity (BLOCK)
xrouter.paymentchannel.mincapacity=1.0

# Auto-open channels
xrouter.paymentchannel.autoopen=1

# Service node settings
xrouter.acceptchannels=1
xrouter.ethereum.address=0x...
```

### Appendix C: Test Cases

See `test/xrouter/paymentchannel_tests.cpp` for comprehensive test suite covering:
- Channel opening and closing
- Payment creation and verification
- Signature validation
- Dispute resolution
- Edge cases and error handling

---

**Version**: 1.0
**Date**: 2025-11-01
**Authors**: Blocknet Development Team
**Status**: Implementation Complete
