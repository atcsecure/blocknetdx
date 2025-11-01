// Multi-Node Payment Channels - User Guide

## Overview

Multi-node payment channels enable clients to query **multiple service nodes simultaneously** with built-in **consensus validation**. Only nodes that provide matching responses receive payment, creating strong economic incentives for accuracy and honesty.

## Key Concepts

### What's Different from Single-Node Channels?

| Feature | Single-Node | Multi-Node |
|---------|------------|------------|
| Nodes queried | 1 | N (3-10) |
| Trust required | High | None (consensus) |
| Response verification | Manual | Automatic |
| Payment condition | Always paid | Only if correct |
| Cost | Lower | Higher (but verified) |
| Fault tolerance | None | N-1 failures OK |

### How It Works

1. **Client opens channel** with N service nodes (e.g., 5 nodes)
2. **Client submits request** with total fee to channel
3. **All nodes execute** query independently
4. **Nodes submit response hashes** to smart contract
5. **Contract determines consensus** (majority vote)
6. **Payment distributed** only to nodes with consensus response
7. **Dishonest nodes get nothing**

### Example Scenario

```
Query: "What is Bitcoin block height?"
Nodes: 5 service nodes
Total Fee: 0.05 BLOCK

Responses:
  Node A: "750000" ✓ (hash: 0xabc...)
  Node B: "750000" ✓ (hash: 0xabc...)
  Node C: "750000" ✓ (hash: 0xabc...)
  Node D: "750000" ✓ (hash: 0xabc...)
  Node E: "749999" ✗ (hash: 0xdef...)

Consensus: "750000" (4/5 votes)
Payment: 0.0125 BLOCK to each of A, B, C, D
Node E receives: 0 BLOCK (incorrect response)
```

## Quick Start

### 1. Open a Multi-Node Channel

```bash
xrouter-cli xrOpenMultiNodeChannel \
  '["0xNode1...","0xNode2...","0xNode3..."]' \
  2 \
  67 \
  10.0
```

**Parameters**:
- **Service nodes**: Array of Ethereum addresses
- **Min nodes**: Minimum for consensus (2)
- **Quorum**: Percentage required to agree (67 = 67%)
- **Deposit**: Amount to deposit (10.0 BLOCK)

**Returns**:
```json
{
  "channelId": "0xabc123...",
  "serviceNodeCount": 3,
  "minNodes": 2,
  "quorumPercentage": 67,
  "depositAmount": 10.00000000
}
```

### 2. Submit a Request

```bash
xrouter-cli xrSubmitMultiNodeRequest \
  "0xabc123..." \
  "xrGetBlockCount BTC" \
  0.03
```

**Parameters**:
- **Channel ID**: Your channel identifier
- **Query**: The query to send to all nodes
- **Total fee**: Amount to distribute among honest nodes

**Returns**:
```json
{
  "requestId": "0xdef456...",
  "query": "xrGetBlockCount BTC",
  "totalFee": 0.03000000,
  "nodeCount": 3
}
```

### 3. Check Consensus

```bash
xrouter-cli xrGetConsensusResponse \
  "0xabc123..." \
  "0xdef456..."
```

**Returns**:
```json
{
  "consensusReached": true,
  "consensusResponse": "750000",
  "honestNodeCount": 3,
  "dishonestNodeCount": 0,
  "honestNodes": ["0xNode1...", "0xNode2...", "0xNode3..."],
  "dishonestNodes": []
}
```

### 4. Settle and Pay

```bash
xrouter-cli xrSettleMultiNodeRequest \
  "0xabc123..." \
  "0xdef456..."
```

**Returns**:
```json
{
  "settled": true,
  "honestNodes": ["0xNode1...", "0xNode2...", "0xNode3..."],
  "feePerNode": 0.01000000,
  "totalFee": 0.03000000
}
```

## Configuration Guide

### Choosing Number of Nodes

| Nodes | Best For | Cost Multiplier | Fault Tolerance |
|-------|----------|-----------------|-----------------|
| 3     | Basic consensus | 3x | 1 node can fail |
| 5     | Standard security | 5x | 2 nodes can fail |
| 7     | High security | 7x | 3 nodes can fail |
| 9+    | Critical data | 9x+ | 4+ nodes can fail |

**Recommendation**: Start with 3-5 nodes for most use cases.

### Choosing Quorum Percentage

| Quorum | Security | Availability | Use Case |
|--------|----------|-------------|----------|
| 51%    | Low | High | Trusted node set |
| 67%    | Good | Good | **Recommended default** |
| 75%    | High | Medium | Important queries |
| 100%   | Maximum | Low | Critical operations |

**Recommendation**: Use 67% (2/3 majority) for good security/availability balance.

### Calculating Costs

**Formula**:
```
Total Cost = Fee Per Node × Expected Honest Nodes
```

**Example pricing**:
```
Standard single-node fee: 0.01 BLOCK
Multi-node (3 nodes): 0.03 BLOCK (3x)
Multi-node (5 nodes): 0.05 BLOCK (5x)
```

**Cost vs. Value**:
- Higher cost, but **guaranteed accuracy**
- No need for manual verification
- Protection against dishonest nodes
- Peace of mind for critical data

## Node Selection Strategy

### 1. Diversify Node Operators

Don't use nodes from same operator:
```bash
# BAD: All nodes from same provider
["provider1_node1", "provider1_node2", "provider1_node3"]

# GOOD: Nodes from different providers
["providerA_node1", "providerB_node1", "providerC_node1"]
```

### 2. Check Node Reputation

```bash
# Check node statistics
xrouter-cli xrGetNodeStats "0xabc123..." "0xNode1..."
```

Look for:
- **High accuracy**: >95% correct responses
- **Many requests served**: >100 queries
- **Low incorrect responses**: <5% failures

### 3. Geographic Distribution

Use nodes in different regions:
- Reduces single-point-of-failure risk
- Better against regional outages
- Harder to collude

### 4. Balance Cost and Reliability

| Strategy | Nodes | Cost | Reliability |
|----------|-------|------|-------------|
| Budget | 3 low-cost | Low | Good |
| Balanced | 3-5 mid-tier | Medium | **Recommended** |
| Premium | 5-7 high-reputation | High | Excellent |
| Mission-critical | 7+ top-rated | Very High | Maximum |

## Advanced Usage

### Monitoring Node Performance

```bash
# Get detailed channel info
xrouter-cli xrGetMultiNodeChannel "0xabc123..."
```

**Track these metrics**:
- `requestsServed`: Total queries handled
- `correctResponses`: Honest responses
- `incorrectResponses`: Dishonest responses
- `accuracyPercentage`: Overall accuracy

### Handling Dishonest Nodes

If a node frequently provides incorrect data:

1. **Check statistics**:
```bash
xrouter-cli xrGetNodeStats "0xabc123..." "0xSuspiciousNode..."
```

2. **If accuracy < 90%**:
   - Node may be malicious or buggy
   - Consider removing from future channels
   - Report to node operator

3. **Automatic penalty**:
   - Node receives no payment
   - Reputation damaged
   - Economic incentive to improve

### Optimizing for Different Use Cases

#### High-Frequency Trading (Speed Priority)

```bash
# Use 3 fast nodes, 51% quorum
xrOpenMultiNodeChannel \
  '["fast_node_1","fast_node_2","fast_node_3"]' \
  2 \
  51 \
  50.0
```

- Minimum nodes for speed
- Lower quorum for faster consensus
- Larger deposit for many queries

#### Financial Data (Accuracy Priority)

```bash
# Use 7 trusted nodes, 75% quorum
xrOpenMultiNodeChannel \
  '["trusted_1",...,"trusted_7"]' \
  5 \
  75 \
  100.0
```

- More nodes for redundancy
- Higher quorum for accuracy
- Accept higher cost

#### General Purpose (Balanced)

```bash
# Use 5 mixed nodes, 67% quorum
xrOpenMultiNodeChannel \
  '["node_1",...,"node_5"]' \
  3 \
  67 \
  25.0
```

- Good balance
- Standard quorum
- Moderate cost

## Troubleshooting

### Problem: "Consensus not reached"

**Causes**:
- Too many nodes offline
- Nodes providing different data
- Quorum too high

**Solutions**:
1. Check node status
2. Verify query is deterministic
3. Lower quorum percentage
4. Replace offline nodes

### Problem: "Request settlement fails"

**Causes**:
- Not enough responses yet
- Consensus not reached
- Request already settled

**Solutions**:
1. Wait for all nodes to respond
2. Check consensus status first
3. Verify request ID is correct

### Problem: "High rate of dishonest responses"

**Causes**:
- Malicious nodes
- Buggy node software
- Network issues

**Solutions**:
1. Check node statistics
2. Replace poorly-performing nodes
3. Increase quorum requirement
4. Use more nodes

## Best Practices

### ✓ DO

- ✓ Use diverse node operators
- ✓ Monitor node performance regularly
- ✓ Start with 3-5 nodes
- ✓ Use 67% quorum for balance
- ✓ Increase nodes for critical data
- ✓ Check consensus before settling
- ✓ Keep sufficient channel balance

### ✗ DON'T

- ✗ Use all nodes from same operator
- ✗ Set quorum below 51%
- ✗ Ignore node statistics
- ✗ Use only 1-2 nodes (defeats purpose)
- ✗ Set quorum too high (>90%)
- ✗ Forget to settle requests
- ✗ Reuse nodes with <90% accuracy

## RPC Command Reference

### Channel Management

```bash
# Open multi-node channel
xrOpenMultiNodeChannel [nodes] minNodes quorum amount [period]

# Get channel info
xrGetMultiNodeChannel "channelId"

# List all channels
xrListMultiNodeChannels

# Close channel
xrCloseMultiNodeChannel "channelId"
```

### Request Operations

```bash
# Submit request
xrSubmitMultiNodeRequest "channelId" "query" totalFee

# Check consensus
xrGetConsensusResponse "channelId" "requestId"

# Settle request
xrSettleMultiNodeRequest "channelId" "requestId"
```

### Node Statistics

```bash
# Get node stats
xrGetNodeStats "channelId" "nodeAddress"
```

## Example Workflows

### Workflow 1: One-Time Query with Verification

```bash
# 1. Open channel
CHANNEL=$(xrouter-cli xrOpenMultiNodeChannel \
  '["0xA...","0xB...","0xC..."]' 2 67 1.0 | jq -r .channelId)

# 2. Submit query
REQUEST=$(xrouter-cli xrSubmitMultiNodeRequest \
  "$CHANNEL" "xrGetBlockCount BTC" 0.03 | jq -r .requestId)

# 3. Wait for responses (simulate with sleep)
sleep 10

# 4. Check consensus
xrouter-cli xrGetConsensusResponse "$CHANNEL" "$REQUEST"

# 5. Settle if consensus reached
xrouter-cli xrSettleMultiNodeRequest "$CHANNEL" "$REQUEST"

# 6. Close channel
xrouter-cli xrCloseMultiNodeChannel "$CHANNEL"
```

### Workflow 2: Continuous Monitoring

```bash
# 1. Open long-lived channel
CHANNEL=$(xrouter-cli xrOpenMultiNodeChannel \
  '["0xA...","0xB...","0xC...","0xD...","0xE..."]' \
  3 67 100.0 | jq -r .channelId)

# 2. Loop: submit requests periodically
while true; do
  # Submit request
  REQUEST=$(xrouter-cli xrSubmitMultiNodeRequest \
    "$CHANNEL" "xrGetBlockCount BTC" 0.05 | jq -r .requestId)

  # Wait for consensus
  sleep 10

  # Get and log response
  RESPONSE=$(xrouter-cli xrGetConsensusResponse "$CHANNEL" "$REQUEST")
  echo "$RESPONSE" | jq -r .consensusResponse

  # Settle
  xrouter-cli xrSettleMultiNodeRequest "$CHANNEL" "$REQUEST"

  # Wait before next query
  sleep 300  # 5 minutes
done
```

### Workflow 3: Node Performance Audit

```bash
# Get channel info
CHANNEL="0xabc123..."
INFO=$(xrouter-cli xrGetMultiNodeChannel "$CHANNEL")

# Extract node addresses
NODES=$(echo "$INFO" | jq -r '.serviceNodes[].nodeAddress')

# Check each node
for NODE in $NODES; do
  echo "Node: $NODE"
  xrouter-cli xrGetNodeStats "$CHANNEL" "$NODE" | jq '{
    accuracy: .accuracyPercentage,
    correct: .correctResponses,
    incorrect: .incorrectResponses,
    total: .requestsServed
  }'
  echo "---"
done
```

## FAQ

**Q: How is this different from single-node channels?**
A: Multi-node channels query multiple nodes and only pay those with matching responses, providing trustless verification.

**Q: What happens if no consensus is reached?**
A: The request cannot be settled and fees remain in escrow. You can retry or refund.

**Q: How many nodes should I use?**
A: Start with 3-5. Use more (7-9) for critical data or if some nodes are unreliable.

**Q: What quorum should I use?**
A: 67% (2/3 majority) is recommended for good security and availability.

**Q: Do I pay dishonest nodes?**
A: No. Only nodes with the consensus response receive payment.

**Q: Can I mix node types?**
A: Yes! You can use different node implementations, which actually increases security.

**Q: How do I know if a node is dishonest?**
A: Check `incorrectResponses` and `accuracyPercentage` in node stats.

**Q: What if all nodes give different responses?**
A: Consensus fails. This might indicate: non-deterministic query, all nodes faulty, or network issues.

**Q: Is it more expensive than single-node?**
A: Yes, N times more (where N = number of nodes). But you get verified, trustless responses.

**Q: Can I add/remove nodes from an existing channel?**
A: Not in current version. You need to close and open a new channel with different nodes.

## Support

- **Documentation**: See XROUTER_MULTINODE_CHANNELS.md for technical details
- **Issues**: Report at https://github.com/blocknetdx/blocknet/issues
- **Community**: Blocknet Discord and Telegram

---

**Next Steps**:
1. Try opening a test channel with 3 nodes
2. Submit a simple query
3. Monitor consensus and settlement
4. Experiment with different quorum settings
5. Track node performance over time
