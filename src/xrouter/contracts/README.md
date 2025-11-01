# XRouter Trustless Payment Channels

This directory contains the smart contract implementation for XRouter's trustless payment channel system, enabling Layer 2 off-chain payments for XRouter services.

## Overview

The XRouter Payment Channel system provides a trustless, scalable solution for micropayments between clients and service nodes. Instead of requiring an on-chain transaction for every XRouter query, clients can open a payment channel and make unlimited off-chain payments with minimal overhead.

## Architecture

### Components

1. **XRouterPaymentChannel.sol** - Ethereum smart contract for payment channel management
2. **xrouterpaymentchannel.h/cpp** - C++ implementation for channel management in Blocknet Core
3. **rpcpaymentchannel.cpp** - RPC commands for interacting with payment channels

## How It Works

### 1. Channel Lifecycle

#### Opening a Channel

```
Client                          Smart Contract              Service Node
  |                                    |                          |
  |------ openChannel(deposit) ------->|                          |
  |                                    |                          |
  |<----- ChannelOpened event ---------|                          |
  |                                    |                          |
  |------ notify channel open ---------------------------------->|
```

The client deposits funds into the smart contract, creating a new payment channel with a specific service node.

#### Making Payments (Off-chain)

```
Client                                                    Service Node
  |                                                             |
  |-- Create signed state update (nonce++, balance-=fee) ----->|
  |                                                             |
  |                                                       Verify signature
  |                                                       Store new state
  |                                                             |
  |<-------------- Provide XRouter service --------------------|
```

For each XRouter request:
1. Client creates a new state update with incremented nonce
2. Client decreases their balance and increases service node balance
3. Client signs the state update
4. Service node verifies signature and new state
5. Service node stores the latest state and provides the service

No on-chain transactions are required!

#### Closing a Channel

**Cooperative Close** (Instant):
```
Client                          Smart Contract              Service Node
  |                                    |                          |
  |-- Sign final state --------------->|<-- Sign final state -----|
  |                                    |                          |
  |------ cooperativeClose() --------->|                          |
  |    (both signatures)               |                          |
  |                                    |                          |
  |<----- Transfer funds --------------|-------- Transfer ------->|
  |                                    |                          |
  |<----- ChannelClosed event ---------|                          |
```

Both parties sign the final state and submit it together for instant settlement.

**Unilateral Close** (With Challenge Period):
```
Client                          Smart Contract              Service Node
  |                                    |                          |
  |------ challengeClose() ----------->|                          |
  |    (latest signed state)           |                          |
  |                                    |                          |
  |<--- ChannelChallenged event -------|-------- Notify --------->|
  |                                    |                          |
  |                              [Challenge Period]               |
  |                              (e.g., 1 hour)                   |
  |                                    |                          |
  |                                    |<----- disputeClose() ----|
  |                                    |   (if has newer state)   |
  |                                    |                          |
  |                         [Challenge Period Expires]            |
  |                                    |                          |
  |------ finalizeClose() ------------>|                          |
  |                                    |                          |
  |<----- Transfer funds --------------|-------- Transfer ------->|
```

If one party becomes unresponsive, the other can initiate a unilateral close with a challenge period, allowing disputes with newer states.

### 2. Security Features

#### State Nonce

Each state update includes a monotonically increasing nonce. This prevents:
- **Replay attacks**: Old states cannot be resubmitted
- **State rollback**: Only newer states are accepted

#### Cryptographic Signatures

All state updates must be signed by both parties:
- **Client signature**: Proves client authorized the payment
- **Service node signature**: Proves service node accepted the state

#### Challenge Period

When closing unilaterally:
- A time window (e.g., 1 hour) is provided for disputes
- Either party can submit a newer signed state
- Prevents one party from closing with an old, favorable state

#### Balance Verification

The contract verifies:
- `clientBalance + serviceNodeBalance == totalDeposits`
- Ensures conservation of funds
- Prevents creating or destroying value

## Smart Contract API

### Functions

#### `openChannel(address serviceNode, uint256 challengePeriod)`
Opens a new payment channel with a service node.
- **Parameters:**
  - `serviceNode`: Address of the service node
  - `challengePeriod`: Duration of challenge period in seconds (1 hour to 7 days)
- **Requires:** ETH deposit (msg.value)
- **Returns:** `channelId`

#### `depositClient(bytes32 channelId)`
Add more funds to an existing channel.
- **Requires:** ETH deposit (msg.value)

#### `cooperativeClose(bytes32 channelId, uint256 nonce, uint256 clientBalance, uint256 serviceNodeBalance, bytes clientSig, bytes serviceNodeSig)`
Instantly close channel with mutual agreement.
- **Requires:** Valid signatures from both parties

#### `challengeClose(bytes32 channelId, uint256 nonce, uint256 clientBalance, uint256 serviceNodeBalance, bytes signature)`
Initiate unilateral close with challenge period.
- **Requires:** Valid signature from the other party

#### `disputeClose(bytes32 channelId, uint256 nonce, uint256 clientBalance, uint256 serviceNodeBalance, bytes signature)`
Challenge a closing state with a newer state.
- **Requires:**
  - Channel in challenged state
  - Within challenge period
  - Nonce higher than current closing state

#### `finalizeClose(bytes32 channelId)`
Finalize channel close after challenge period expires.
- **Requires:** Challenge period has elapsed

### Events

```solidity
event ChannelOpened(bytes32 indexed channelId, address indexed client,
                   address indexed serviceNode, uint256 clientDeposit,
                   uint256 serviceNodeDeposit, uint256 challengePeriod);

event ChannelChallenged(bytes32 indexed channelId, uint256 nonce,
                       uint256 clientBalance, uint256 serviceNodeBalance,
                       uint256 closingTime);

event ChannelClosed(bytes32 indexed channelId, uint256 clientFinalBalance,
                   uint256 serviceNodeFinalBalance);

event ChannelDisputed(bytes32 indexed channelId, uint256 newNonce,
                     uint256 newClientBalance, uint256 newServiceNodeBalance);
```

## C++ Integration

### XRouterPaymentChannelManager

The C++ manager handles:
- Opening and managing channels
- Creating signed payment states
- Verifying received payments
- Closing channels

### Key Classes

#### `ChannelStateUpdate`
Represents a signed state update:
```cpp
struct ChannelStateUpdate {
    uint256 channelId;
    uint64_t nonce;
    CAmount clientBalance;
    CAmount serviceNodeBalance;
    std::vector<unsigned char> clientSignature;
    std::vector<unsigned char> serviceNodeSignature;
    uint64_t timestamp;
};
```

#### `PaymentChannel`
Represents a payment channel:
```cpp
struct PaymentChannel {
    uint256 channelId;
    std::string contractAddress;
    std::string clientAddress;
    std::string serviceNodeAddress;
    CAmount clientBalance;
    CAmount serviceNodeBalance;
    uint64_t nonce;
    ChannelState state;
    // ... more fields
};
```

## RPC Commands

### Client Commands

#### `xrOpenPaymentChannel "serviceNodeAddress" amount ( challengePeriod )`
Open a new payment channel.

**Example:**
```bash
xrouter-cli xrOpenPaymentChannel "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb" 10.0
```

#### `xrCreateChannelPayment "channelId" amount`
Create an off-chain payment within a channel.

**Example:**
```bash
xrouter-cli xrCreateChannelPayment "abc123..." 0.01
```

#### `xrClosePaymentChannel "channelId" ( cooperative )`
Close a payment channel.

**Example:**
```bash
xrouter-cli xrClosePaymentChannel "abc123..." true
```

### Query Commands

#### `xrGetPaymentChannel "channelId"`
Get detailed channel information.

#### `xrListPaymentChannels ( "serviceNodeAddress" )`
List all channels, optionally filtered by service node.

#### `xrGetChannelBalance`
Get total available balance across all channels.

## Usage Examples

### Example 1: Opening a Channel

```bash
# Open a channel with 5 BLOCK deposit
xrouter-cli xrOpenPaymentChannel "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb" 5.0 7200

# Response:
{
  "channelId": "4f3d2a1b...",
  "serviceNodeAddress": "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb",
  "depositAmount": 5.00000000,
  "challengePeriod": 7200
}
```

### Example 2: Making Payments

```bash
# Create payment for XRouter query (0.001 BLOCK)
xrouter-cli xrCreateChannelPayment "4f3d2a1b..." 0.001

# Response:
{
  "channelId": "4f3d2a1b...",
  "nonce": 1,
  "clientBalance": 4.99900000,
  "serviceNodeBalance": 0.00100000,
  "clientSignature": "3045022100...",
  "timestamp": 1635789012
}
```

### Example 3: Checking Channel Status

```bash
xrouter-cli xrGetPaymentChannel "4f3d2a1b..."

# Response:
{
  "channelId": "4f3d2a1b...",
  "clientBalance": 4.99900000,
  "serviceNodeBalance": 0.00100000,
  "nonce": 1,
  "state": "open",
  "capacity": 5.00000000,
  "availableBalance": 4.99900000
}
```

## Benefits

### 1. Scalability
- **Unlimited transactions** off-chain between channel opens/closes
- **No blockchain congestion** from micropayments
- **Instant finality** for off-chain payments

### 2. Cost Efficiency
- **Minimal gas fees**: Only 2-3 transactions (open, close, optional dispute)
- **No per-query fees**: Thousands of queries for the cost of 2 transactions
- **Batched settlement**: All payments settled in one transaction

### 3. Trustless Operation
- **No custodians**: Funds locked in smart contract
- **Cryptographic security**: Signatures prevent fraud
- **Dispute resolution**: Challenge period protects both parties
- **Blockchain settlement**: Final state enforced on-chain

### 4. Privacy
- **Off-chain payments**: Individual queries not visible on blockchain
- **Only participants know**: Channel states known only to client and service node
- **Minimal on-chain data**: Only opening and closing states public

## Security Considerations

### Implementation Best Practices

1. **Always verify signatures** before accepting state updates
2. **Store all state updates** to handle disputes
3. **Monitor channel states** on blockchain for challenges
4. **Set appropriate challenge periods** (balance convenience vs. security)
5. **Implement automatic dispute handling** to respond to invalid closes

### Attack Vectors and Mitigations

#### Attempt to close with old state
**Mitigation:** Monotonic nonces prevent accepting older states

#### Refuse to sign cooperative close
**Mitigation:** Unilateral close with challenge period always available

#### Submit invalid balance split
**Mitigation:** Contract verifies balances sum to deposits

#### Replay signatures across channels
**Mitigation:** Channel ID included in signed message hash

## Future Enhancements

1. **Multi-hop channels**: Route payments through intermediaries
2. **Conditional payments**: HTLCs for atomic swaps
3. **Virtual channels**: Open channels without on-chain transactions
4. **Channel factories**: Create multiple channels from one transaction
5. **Watchtowers**: Automated dispute response services

## Deployment

### Smart Contract Deployment

1. Compile the Solidity contract:
```bash
solc --optimize --bin --abi XRouterPaymentChannel.sol
```

2. Deploy to Ethereum network:
```bash
# Use your preferred deployment tool (Truffle, Hardhat, etc.)
```

3. Configure Blocknet Core with contract address:
```ini
# blocknet.conf
xrouter.paymentchannel.contract=0x...
xrouter.paymentchannel.enabled=1
```

### Integration Steps

1. Deploy smart contract to Ethereum
2. Update xrouter configuration with contract address
3. Restart Blocknet Core with payment channel support
4. Service nodes configure payment addresses
5. Clients open channels and start making payments

## License

Copyright (c) 2018-2025 The Blocknet developers

Distributed under the MIT software license.
