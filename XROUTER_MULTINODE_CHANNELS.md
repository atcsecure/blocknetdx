// Multi-Node Payment Channels for XRouter - Technical Specification

## Executive Summary

This document specifies the implementation of **multi-node payment channels** for XRouter with built-in **consensus validation** and **conditional payment distribution**. Unlike traditional payment channels that connect one client to one service node, multi-node channels connect a client to **N service nodes simultaneously**, enabling:

1. **Consensus-based responses** - Multiple nodes queried in parallel
2. **Quality assurance** - Only nodes providing matching responses get paid
3. **Fault tolerance** - System continues even if some nodes are offline or malicious
4. **Economic incentives** - Dishonest nodes don't get paid, encouraging accuracy

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Consensus Mechanism](#consensus-mechanism)
4. [Payment Distribution](#payment-distribution)
5. [Smart Contract Specification](#smart-contract-specification)
6. [Usage Examples](#usage-examples)
7. [Security Analysis](#security-analysis)
8. [Economics](#economics)

## 1. Overview

### Problem Statement

In the single-node payment channel model:
- Client trusts a single service node for accurate data
- No way to verify response correctness
- Malicious or buggy nodes can provide incorrect data
- No economic penalty for dishonest behavior

### Solution

Multi-node channels query **multiple service nodes simultaneously** and compare responses:
- Client queries N nodes (e.g., 3-5 nodes)
- Nodes provide responses independently
- Responses are compared via hash matching
- **Consensus response** is determined (majority vote)
- **Only nodes with consensus response get paid**
- Dishonest nodes receive nothing

### Key Benefits

1. **Trustless Verification**: No need to trust any single node
2. **Fault Tolerance**: System works even if some nodes fail
3. **Economic Security**: Dishonest behavior is economically punished
4. **Improved Accuracy**: Multiple independent sources increase confidence
5. **Cost Efficient**: Still cheaper than N independent transactions

## 2. Architecture

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                    Client Application                        │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              Multi-Node Channel Manager                      │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐            │
│  │ Channel 1  │  │ Channel 2  │  │ Channel 3  │            │
│  │ (3 nodes)  │  │ (5 nodes)  │  │ (7 nodes)  │            │
│  └────────────┘  └────────────┘  └────────────┘            │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│            Ethereum Smart Contract                           │
│  ┌───────────────────────────────────────────────┐          │
│  │  MultiNodePaymentChannel.sol                  │          │
│  │  - Manages N-party channels                   │          │
│  │  - Validates consensus                        │          │
│  │  - Distributes payments conditionally         │          │
│  └───────────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│                  Service Node Network                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │  Node A  │  │  Node B  │  │  Node C  │  │  Node D  │   │
│  │          │  │          │  │          │  │          │   │
│  │ Ethereum │  │ Bitcoin  │  │ Litecoin │  │ Dogecoin │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Multi-Node Channel Structure

```solidity
struct MultiNodeChannel {
    address client;                    // Client address
    address[] serviceNodes;            // Array of N service nodes
    mapping(address => NodeInfo) nodes;// Per-node statistics
    uint256 clientBalance;             // Client's remaining funds
    uint256 minNodes;                  // Minimum nodes for consensus (e.g., 2)
    uint256 quorumPercentage;          // Required consensus % (e.g., 67%)
}

struct NodeInfo {
    uint256 balance;                   // Node's earned balance
    uint256 requestsServed;            // Total requests
    uint256 correctResponses;          // Responses matching consensus
    uint256 incorrectResponses;        // Responses NOT matching consensus
}
```

## 3. Consensus Mechanism

### How Consensus Works

#### Step 1: Request Submission

Client submits a request to the channel:
```javascript
submitRequest(channelId, requestId, queryHash, totalFee)
```

This:
- Deducts `totalFee` from client's balance (held in escrow)
- Notifies all N service nodes
- Starts response collection period

#### Step 2: Nodes Provide Responses

Each service node:
1. Executes the query independently
2. Generates a response
3. Calculates `responseHash = SHA256(response)`
4. Submits `responseHash` to smart contract

```solidity
submitResponse(channelId, requestId, responseHash)
```

#### Step 3: Vote Counting

The contract counts votes for each unique response hash:

```
Response Hash                  Votes
----------------------------------
0xabc123... (correct)          3 votes ✓
0xdef456... (incorrect)        1 vote
0x789abc... (incorrect)        1 vote
```

#### Step 4: Consensus Determination

Consensus is reached when:
```
votes(consensusHash) >= totalNodes * quorumPercentage / 100
```

Example with 5 nodes and 67% quorum:
- Required votes: 5 * 0.67 = 3.35 → 4 votes needed
- If 4+ nodes agree → Consensus reached ✓

### Quorum Configurations

| Nodes | 51% Quorum | 67% Quorum | 75% Quorum | 100% Quorum |
|-------|------------|------------|------------|-------------|
| 3     | 2          | 2          | 3          | 3           |
| 5     | 3          | 4          | 4          | 5           |
| 7     | 4          | 5          | 6          | 7           |
| 9     | 5          | 6          | 7          | 9           |

**Recommended**: 67% (2/3 majority) - good balance of security and availability

## 4. Payment Distribution

### Distribution Algorithm

After consensus is reached:

```solidity
function settleRequest(channelId, requestId) {
    // 1. Identify honest nodes (those with consensus response)
    address[] honestNodes;
    for each node in responses:
        if responseHash == consensusHash:
            honestNodes.push(node)

    // 2. Calculate payment per honest node
    feePerNode = totalFee / honestNodes.length

    // 3. Distribute to honest nodes
    for each node in honestNodes:
        nodes[node].balance += feePerNode
        nodes[node].correctResponses++

    // 4. Penalize dishonest nodes (no payment)
    for each node NOT in honestNodes:
        nodes[node].incorrectResponses++
}
```

### Payment Scenarios

#### Scenario 1: Full Consensus (All Agree)

```
Total Fee: 0.03 BLOCK
Nodes: A, B, C (3 nodes)
Responses:
  - Node A: 0xabc123... ✓
  - Node B: 0xabc123... ✓
  - Node C: 0xabc123... ✓

Result:
  - Consensus: 0xabc123... (3/3 votes)
  - Each node gets: 0.01 BLOCK
  - All nodes paid
```

#### Scenario 2: Partial Consensus (Majority)

```
Total Fee: 0.05 BLOCK
Nodes: A, B, C, D, E (5 nodes)
Quorum: 67% (need 4 votes)
Responses:
  - Node A: 0xabc123... ✓
  - Node B: 0xabc123... ✓
  - Node C: 0xabc123... ✓
  - Node D: 0xabc123... ✓
  - Node E: 0xdef456... ✗

Result:
  - Consensus: 0xabc123... (4/5 votes)
  - Honest nodes: A, B, C, D
  - Each honest node gets: 0.0125 BLOCK
  - Node E gets: 0 BLOCK (dishonest)
```

#### Scenario 3: No Consensus

```
Total Fee: 0.03 BLOCK
Nodes: A, B, C (3 nodes)
Quorum: 67% (need 2 votes)
Responses:
  - Node A: 0xabc123... (1 vote)
  - Node B: 0xdef456... (1 vote)
  - Node C: 0x789abc... (1 vote)

Result:
  - No consensus reached (max 1/3 votes < 2 required)
  - Settlement fails
  - Fee returned to client OR held for retry
```

### Node Statistics Tracking

The contract tracks per-node statistics:

```javascript
{
  "nodeAddress": "0x1234...",
  "requestsServed": 100,
  "correctResponses": 95,    // Paid 95 times
  "incorrectResponses": 5,   // Unpaid 5 times
  "accuracyPercentage": 95.0 // 95% accuracy
}
```

Clients can use this to:
- Blacklist consistently dishonest nodes
- Prefer high-accuracy nodes
- Adjust quorum requirements

## 5. Smart Contract Specification

### Core Functions

#### openMultiNodeChannel

```solidity
function openMultiNodeChannel(
    address[] memory serviceNodes,
    uint256 minNodes,
    uint256 quorumPercentage,
    uint256 challengePeriod
) external payable returns (bytes32 channelId)
```

Opens a new multi-node channel.

**Parameters:**
- `serviceNodes`: Array of N service node addresses
- `minNodes`: Minimum nodes required (2+)
- `quorumPercentage`: Consensus threshold (51-100)
- `challengePeriod`: Dispute resolution time

**Validation:**
- `serviceNodes.length >= minNodes`
- `minNodes >= 2`
- `quorumPercentage >= 51 && <= 100`
- `msg.value > 0` (client deposit)
- No duplicate node addresses

#### submitRequest

```solidity
function submitRequest(
    bytes32 channelId,
    uint256 requestId,
    bytes32 queryHash,
    uint256 totalFee
) external returns (bool)
```

Client submits a request to all nodes.

**Effects:**
- Deducts `totalFee` from client balance (escrow)
- Emits `RequestSubmitted` event
- Increments `totalRequests` counter

#### submitResponse

```solidity
function submitResponse(
    bytes32 channelId,
    uint256 requestId,
    bytes32 responseHash
) external
```

Service node submits response hash.

**Validation:**
- Caller must be a service node in the channel
- Node must be active
- Request must exist and not be settled
- Node hasn't already responded

**Effects:**
- Records response hash
- Increments vote count for that hash
- Updates consensus if this hash has most votes
- Emits `ResponseSubmitted` event
- Emits `ConsensusReached` if quorum met

#### settleRequest

```solidity
function settleRequest(
    bytes32 channelId,
    uint256 requestId
) external
```

Distributes payment after consensus.

**Validation:**
- Request must exist
- Must not already be settled
- Consensus must be reached (votes >= quorum)

**Effects:**
- Identifies honest nodes (consensus response)
- Calculates `feePerNode`
- Credits honest nodes' balances
- Updates `correctResponses` / `incorrectResponses`
- Marks request as settled
- Emits `PaymentDistributed` event

### Events

```solidity
event MultiNodeChannelOpened(
    bytes32 indexed channelId,
    address indexed client,
    address[] serviceNodes,
    uint256 clientDeposit,
    uint256 minNodes,
    uint256 quorumPercentage
);

event RequestSubmitted(
    bytes32 indexed channelId,
    uint256 indexed requestId,
    bytes32 queryHash,
    uint256 totalFee,
    uint256 nodeCount
);

event ResponseSubmitted(
    bytes32 indexed channelId,
    uint256 indexed requestId,
    address indexed nodeAddress,
    bytes32 responseHash
);

event ConsensusReached(
    bytes32 indexed channelId,
    uint256 indexed requestId,
    bytes32 consensusHash,
    uint256 consensusCount,
    uint256 totalNodes
);

event PaymentDistributed(
    bytes32 indexed channelId,
    uint256 indexed requestId,
    address[] honestNodes,
    uint256 feePerNode
);
```

## 6. Usage Examples

### Example 1: Opening a Multi-Node Channel

```bash
# Open channel with 3 nodes, 67% quorum, 10 BLOCK deposit
xrouter-cli xrOpenMultiNodeChannel \
  '["0xNode1Address","0xNode2Address","0xNode3Address"]' \
  2 \
  67 \
  10.0

# Response:
{
  "channelId": "0xabc123...",
  "serviceNodeCount": 3,
  "minNodes": 2,
  "quorumPercentage": 67,
  "depositAmount": 10.00000000
}
```

### Example 2: Submitting a Request

```bash
# Query Bitcoin block count from 3 nodes
# Fee: 0.03 BLOCK total (0.01 per honest node if all agree)
xrouter-cli xrSubmitMultiNodeRequest \
  "0xabc123..." \
  "xrGetBlockCount BTC" \
  0.03

# Response:
{
  "requestId": "0xdef456...",
  "channelId": "0xabc123...",
  "query": "xrGetBlockCount BTC",
  "totalFee": 0.03000000,
  "nodeCount": 3
}
```

### Example 3: Checking Consensus

```bash
# Check if consensus reached
xrouter-cli xrGetConsensusResponse \
  "0xabc123..." \
  "0xdef456..."

# Response:
{
  "requestId": "0xdef456...",
  "consensusReached": true,
  "consensusResponse": "750000",  # Block height
  "honestNodeCount": 3,
  "dishonestNodeCount": 0,
  "honestNodes": [
    "0xNode1Address",
    "0xNode2Address",
    "0xNode3Address"
  ],
  "dishonestNodes": [],
  "settled": false
}
```

### Example 4: Settling and Distributing Payment

```bash
# Settle request and pay honest nodes
xrouter-cli xrSettleMultiNodeRequest \
  "0xabc123..." \
  "0xdef456..."

# Response:
{
  "settled": true,
  "honestNodes": [
    "0xNode1Address",
    "0xNode2Address",
    "0xNode3Address"
  ],
  "feePerNode": 0.01000000,
  "totalFee": 0.03000000
}
```

### Example 5: Checking Node Stats

```bash
# Check performance of a specific node
xrouter-cli xrGetNodeStats \
  "0xabc123..." \
  "0xNode1Address"

# Response:
{
  "nodeAddress": "0xNode1Address",
  "balance": 0.95000000,        # Earned 0.95 BLOCK
  "requestsServed": 100,
  "correctResponses": 98,       # Honest 98 times
  "incorrectResponses": 2,      # Dishonest 2 times
  "accuracyPercentage": 98.00,
  "active": true
}
```

## 7. Security Analysis

### Attack Scenarios

#### Attack 1: Sybil Attack (One Entity Controls Multiple Nodes)

**Attack**: Attacker runs multiple nodes to control consensus

**Mitigation**:
- Node selection should be decentralized
- Clients can verify node diversity (different IPs, operators)
- Reputation system tracks node accuracy
- Staking requirements increase Sybil cost

**Residual Risk**: Medium if node selection is centralized

#### Attack 2: Collusion (Nodes Coordinate to Provide False Data)

**Attack**: Multiple nodes agree to provide incorrect but matching data

**Mitigation**:
- Economic disincentive (reputation damage, staking loss)
- Clients can cross-reference with external sources
- Use more nodes with higher quorum (e.g., 5 nodes, 80% quorum)
- Slashing mechanisms for provably false data

**Residual Risk**: Low if nodes are independent

#### Attack 3: Response Withholding

**Attack**: Node doesn't respond to manipulate consensus

**Mitigation**:
- Non-responsive nodes don't get paid (same as wrong response)
- Timeout periods enforced
- Reputation damage for frequent non-response

**Residual Risk**: Very Low

#### Attack 4: Eclipse Attack on Client

**Attack**: Attacker controls all nodes seen by client

**Mitigation**:
- Client uses trusted node directory
- Decentralized node discovery
- Verify node diversity

**Residual Risk**: Low with proper node selection

### Byzantine Fault Tolerance

The system provides Byzantine Fault Tolerance:

```
Fault Tolerance = floor((n - 1) / 3)
```

| Total Nodes | BFT (33%) | With 67% Quorum |
|-------------|-----------|-----------------|
| 3           | 0         | 2 needed        |
| 4           | 1         | 3 needed        |
| 5           | 1         | 4 needed        |
| 7           | 2         | 5 needed        |
| 10          | 3         | 7 needed        |

With **67% quorum**, the system tolerates up to **33% dishonest/faulty nodes**.

## 8. Economics

### Cost Analysis

#### Traditional Model (Single Node)

```
Cost per query = Transaction fee + Service fee
                = 0.0001 BLOCK + 0.001 BLOCK
                = 0.0011 BLOCK
```

#### Multi-Node Channel (3 Nodes, All Honest)

```
Cost per query = Total fee / Honest nodes
                = 0.03 BLOCK / 3 nodes
                = 0.01 BLOCK per node

Total cost = 0.03 BLOCK (paid to nodes)
           + 0 (no transaction fee for off-chain)
           = 0.03 BLOCK
```

**Note**: Higher cost per query BUT with consensus verification

#### Multi-Node Channel (5 Nodes, 1 Dishonest)

```
Total fee = 0.05 BLOCK
Honest nodes = 4 (out of 5)

Cost per honest node = 0.05 / 4 = 0.0125 BLOCK

Dishonest node receives = 0 BLOCK
```

### Pricing Strategies

#### Fixed Fee Per Node

Simple: `totalFee = feePerNode * expectedHonestNodes`

Example: $0.01 per node * 3 nodes = $0.03 total

#### Premium for Consensus

Charge extra for consensus guarantee:

```
Standard fee (1 node): $0.01
Consensus fee (3 nodes): $0.025 (2.5x)
Premium consensus (5 nodes): $0.04 (4x)
```

#### Dynamic Pricing Based on Accuracy

Nodes with higher accuracy can charge more:

```
Node with 99% accuracy: $0.012 per query
Node with 95% accuracy: $0.010 per query
Node with 90% accuracy: $0.008 per query
```

### Node Incentives

**Honest Behavior**:
- Earn fees consistently
- Build reputation
- Attract more clients

**Dishonest Behavior**:
- No payment for incorrect responses
- Reputation damage
- Potential channel exclusion
- Lost future revenue

**Economic Equilibrium**: Honesty is more profitable long-term

## 9. Comparison with Single-Node Channels

| Feature | Single-Node Channel | Multi-Node Channel |
|---------|-------------------|-------------------|
| **Trust Model** | Trust one node | Verify via consensus |
| **Cost per Query** | Lower (1x fee) | Higher (Nx fee) |
| **Reliability** | Single point of failure | N-1 fault tolerant |
| **Data Accuracy** | Unverified | Consensus-verified |
| **Dishonest Node Impact** | Complete failure | Partial (no payment) |
| **Setup Complexity** | Simple | Moderate |
| **Use Case** | Trusted nodes | Untrusted/diverse nodes |

## 10. Future Enhancements

### Weighted Voting

Nodes with higher reputation get more voting weight:

```
voteWeight = baseWeight * (accuracy / 100)
```

### Slashing Mechanisms

Dishonest nodes lose staked deposits:

```
if incorrectResponses > threshold:
    slash(node.deposit * slashingPercentage)
```

### Adaptive Quorum

Adjust quorum based on node reputation:

```
if all high-reputation nodes:
    quorumPercentage = 51%
else:
    quorumPercentage = 75%
```

### Cross-Channel Reputation

Share node statistics across all channels:

```
globalReputation = avg(allChannels.nodeAccuracy)
```

## Conclusion

Multi-node payment channels provide a trustless, economically secure way to obtain verified data from multiple sources. By conditioning payment on consensus, the system creates strong economic incentives for honest behavior while maintaining the scalability benefits of payment channels.

**Key Advantages**:
- ✓ Trustless verification through consensus
- ✓ Economic penalties for dishonest nodes
- ✓ Fault tolerance (works with some node failures)
- ✓ Better data quality than single-source
- ✓ Still 1000x cheaper than transaction-per-query

**Trade-offs**:
- Higher cost per query than single-node
- Increased complexity
- Requires N responsive nodes

**Recommended Use Cases**:
- Financial data queries (high-value, need accuracy)
- Cross-chain oracles
- Blockchain state verification
- Any scenario requiring trustless data validation

---

**Version**: 1.0
**Date**: 2025-11-01
**Status**: Implementation Complete
