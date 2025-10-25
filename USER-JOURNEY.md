# Complete User Journey: Getting Block Height with XRouter

## 🎯 Goal
User wants to get the current ETH block height using XRouter with trustless multi-node verification.

---

## 📋 The Complete Flow

```
┌──────────────────────────────────────────────────────────────┐
│                    USER JOURNEY                              │
│                                                              │
│  1. User deposits to payment pool (one-time setup)          │
│  2. User makes XRouter request (off-chain, instant)         │
│  3. XRouter creates on-chain request (triggers attestation) │
│  4. Multiple nodes respond with data (off-chain)            │
│  5. Each node submits attestation hash (on-chain)           │
│  6. Smart contract finds consensus automatically            │
│  7. Smart contract pays all nodes automatically             │
│  8. User receives verified data (trustless consensus)       │
└──────────────────────────────────────────────────────────────┘
```

---

## Step 1: One-Time Setup - Deposit to Payment Pool

**You only do this once, then use it for many requests!**

### Option A: Using Web3/Ethers.js

```javascript
const { ethers } = require("ethers");

// Connect to Ethereum
const provider = new ethers.providers.JsonRpcProvider("https://mainnet.infura.io/v3/YOUR_KEY");
const wallet = new ethers.Wallet("YOUR_PRIVATE_KEY", provider);

// Connect to payment contract
const contractAddress = "0x...";  // Deployed XRouterPaymentHub address
const contract = new ethers.Contract(contractAddress, ABI, wallet);

// Deposit 100 ETH to payment pool (one-time)
const depositAmount = ethers.utils.parseEther("100");
const tx = await contract.depositToPool(depositAmount, {
    value: depositAmount  // For native ETH
});

await tx.wait();
console.log("✅ Deposited 100 ETH to payment pool");
console.log("💰 You can now make ~1000 requests (0.1 ETH per request)");
```

### Option B: Using XRouter CLI (Future)

```bash
# Deposit to payment pool
xrouter-cli depositpool 100

# Output:
# ✅ Deposited 100 ETH to payment pool
# 💰 Available balance: 100 ETH
# 📊 Can make ~1000 requests at 0.1 ETH per request
```

### Check Your Balance

```javascript
const pool = await contract.getPaymentPool(wallet.address);
console.log("Balance:", ethers.utils.formatEther(pool.balance), "ETH");
console.log("Reserved:", ethers.utils.formatEther(pool.reserved), "ETH");
console.log("Available:", ethers.utils.formatEther(pool.balance - pool.reserved), "ETH");
```

---

## Step 2: Make XRouter Request (The Easy Part!)

**This is what you actually call - the rest happens automatically!**

### Using XRouter Client Library

```javascript
const XRouterClient = require('xrouter-client');

// Initialize XRouter client
const xrouter = new XRouterClient({
    rpcUrl: "http://localhost:41414",  // Your Blocknet node
    paymentContract: "0x...",          // Payment contract address
    wallet: wallet                     // Your wallet (from Step 1)
});

// Make request for ETH block count (this is the ONLY call you make!)
const result = await xrouter.getBlockCount("ETH", {
    consensus: 5  // Require 5 nodes to attest
});

console.log("Current ETH block height:", result.blockCount);
console.log("Consensus:", result.consensusCount, "out of", result.totalNodes, "nodes agreed");
console.log("Verified data hash:", result.consensusHash);
```

**That's it! Behind the scenes, here's what happens...**

---

## Step 3: Behind the Scenes - XRouter Creates On-Chain Request

**Automatically handled by XRouter client**

```javascript
// XRouter client internally does this:

// 1. Generate unique request ID
const requestId = ethers.utils.keccak256(
    ethers.utils.toUtf8Bytes("getBlockCount-ETH-" + Date.now())
);

// 2. Create on-chain request (reserves payment)
const createTx = await contract.createRequest(requestId, 5);  // 5 attestations
await createTx.wait();

// This reserves: 5 attestations × 0.1 ETH = 0.5 ETH from your pool
```

**On-Chain State:**
```
Request created:
- ID: 0xabc123...
- Client: 0xYourAddress
- Required attestations: 5
- Fee reserved: 0.5 ETH
- Status: PENDING
```

---

## Step 4: XRouter Sends Request to Nodes (Off-Chain)

**Automatically handled by XRouter client**

```javascript
// XRouter client internally does this:

// 1. Find available nodes that support ETH
const nodes = await xrouter.findNodes("ETH", 7);  // Find 7 nodes

// 2. Send request to all nodes via XRouter network
for (const node of nodes) {
    await xrouter.sendRequest(node, {
        command: "xrGetBlockCount",
        currency: "ETH",
        requestId: requestId  // Include on-chain request ID
    });
}
```

**XRouter Packet Sent:**
```
To: Node A, B, C, D, E, F, G
Command: xrGetBlockCount
Currency: ETH
RequestId: 0xabc123...
```

---

## Step 5: Nodes Respond and Submit Attestations

**This happens automatically on each node**

### On Node A (and all other nodes):

```cpp
// XRouterServer receives request
void XRouterServer::onMessageReceived(CNode* node, XRouterPacketPtr packet, ...) {
    // 1. Extract request details
    std::string currency = packet->getCurrency();  // "ETH"
    std::string requestId = packet->getRequestId();  // "0xabc123..."

    // 2. Query local ETH node for block count
    std::string response = processGetBlockCount(currency);
    // Response: {"result": 18500000}

    // 3. Send response to client (off-chain, via XRouter network)
    sendResponseToClient(node, response);

    // 4. Calculate hash of response
    bytes32 dataHash = keccak256(response);
    // dataHash: 0xdef456...

    // 5. Sign attestation
    bytes signature = signMessage(requestId, dataHash, nodePrivateKey);

    // 6. Submit attestation to smart contract (on-chain)
    ethBridge.submitAttestation(requestId, dataHash, signature);
}
```

**On-Chain - Node A submits attestation:**
```solidity
contract.submitAttestation(
    requestId: 0xabc123...,
    dataHash: 0xdef456...,  // Hash of {"result": 18500000}
    signature: 0x...        // Node A's signature
)
```

**This happens on ALL 7 nodes:**

| Node | Local ETH Query | Data Hash | Attestation Submitted |
|------|----------------|-----------|----------------------|
| A | Block: 18500000 | 0xdef456... | ✅ On-chain |
| B | Block: 18500000 | 0xdef456... | ✅ On-chain |
| C | Block: 18500000 | 0xdef456... | ✅ On-chain |
| D | Block: 18500000 | 0xdef456... | ✅ On-chain |
| E | Block: 18500000 | 0xdef456... | ✅ On-chain |
| F | Block: 18500001 | 0x789abc... | ✅ On-chain (wrong!) |
| G | Block: 18500001 | 0x789abc... | ✅ On-chain (wrong!) |

---

## Step 6: Smart Contract Finds Consensus (Automatic!)

**Triggered automatically when 5th attestation arrives**

```solidity
// In smart contract:
function submitAttestation(...) external {
    // Store attestation
    requestAttestations[requestId].push(attestation);

    // Check if we have enough
    if (requestAttestations[requestId].length >= 5) {
        // AUTOMATICALLY TRIGGER CONSENSUS
        _processConsensusAndDistribute(requestId);
    }
}

function _processConsensusAndDistribute(requestId) internal {
    // Count votes
    Count[0xdef456...] = 5 votes ← CONSENSUS
    Count[0x789abc...] = 2 votes

    // Find winner
    consensusHash = 0xdef456...

    // Emit event
    emit ConsensusReached(requestId, 0xdef456..., 5);

    // Distribute payments automatically
    _distributePayments(requestId, 0xdef456...);
}
```

---

## Step 7: Smart Contract Pays All Nodes (Automatic!)

**No manual claiming needed!**

```solidity
function _distributePayments(requestId, consensusHash) internal {
    // Node A: Consensus match ✓
    paymentToken.transfer(NodeA, 0.15 ETH);  // Base + bonus
    serviceNodes[NodeA].reputation++;

    // Node B: Consensus match ✓
    paymentToken.transfer(NodeB, 0.15 ETH);
    serviceNodes[NodeB].reputation++;

    // Node C, D, E: Same as A, B
    // ...

    // Node F: No consensus ✗
    paymentToken.transfer(NodeF, 0.05 ETH);  // Base - penalty
    serviceNodes[NodeF].reputation--;

    // Node G: No consensus ✗
    paymentToken.transfer(NodeG, 0.05 ETH);
    serviceNodes[NodeG].reputation--;

    // Update client's pool
    pool.reserved -= 0.5 ETH;
    pool.balance -= 0.85 ETH;
    pool.spent += 0.85 ETH;
}
```

**On-Chain Events Emitted:**
```
PaymentDistributed(requestId, NodeA, 0.15 ETH, true)
PaymentDistributed(requestId, NodeB, 0.15 ETH, true)
PaymentDistributed(requestId, NodeC, 0.15 ETH, true)
PaymentDistributed(requestId, NodeD, 0.15 ETH, true)
PaymentDistributed(requestId, NodeE, 0.15 ETH, true)
PaymentDistributed(requestId, NodeF, 0.05 ETH, false)
PaymentDistributed(requestId, NodeG, 0.05 ETH, false)
```

---

## Step 8: You Receive the Verified Data

**XRouter client returns consensus result**

```javascript
// Your original call returns:
const result = await xrouter.getBlockCount("ETH", { consensus: 5 });

// Result object:
{
    blockCount: 18500000,           // The actual data you wanted!
    consensusHash: "0xdef456...",   // Hash that won consensus
    consensusCount: 5,              // How many nodes agreed
    totalNodes: 7,                  // Total nodes that responded
    requestId: "0xabc123...",       // On-chain request ID
    cost: "0.85 ETH",              // Total cost (7 attestations)
    remainingBalance: "99.15 ETH"   // Your pool balance after
}

console.log("✅ Current ETH block height:", result.blockCount);
console.log("✅ Verified by", result.consensusCount, "nodes");
console.log("✅ Cost:", result.cost);
console.log("💰 Remaining balance:", result.remainingBalance);
```

---

## 🔄 Making More Requests (Super Easy!)

**No need to deposit again - just make another request!**

```javascript
// Request 2: Get ETH block hash
const blockHash = await xrouter.getBlockHash("ETH", 18500000, { consensus: 5 });
console.log("Block hash:", blockHash.hash);
// Cost: 0.85 ETH (another ~5-7 attestations)
// Remaining: 98.30 ETH

// Request 3: Get BTC block count
const btcHeight = await xrouter.getBlockCount("BTC", { consensus: 5 });
console.log("BTC height:", btcHeight.blockCount);
// Cost: 0.85 ETH
// Remaining: 97.45 ETH

// Request 4: Get transaction
const tx = await xrouter.getTransaction("ETH", "0x...", { consensus: 5 });
// Cost: 0.85 ETH
// Remaining: 96.60 ETH

// You can make ~100+ more requests before needing to top up!
```

---

## 💰 Top Up Your Pool (When Balance Gets Low)

**Yes! You can add more funds anytime**

```javascript
// Check balance
const pool = await contract.getPaymentPool(wallet.address);
const available = pool.balance - pool.reserved;
console.log("Available:", ethers.utils.formatEther(available), "ETH");

// Top up with another 50 ETH
const topUpTx = await contract.depositToPool(
    ethers.utils.parseEther("50"),
    { value: ethers.utils.parseEther("50") }
);
await topUpTx.wait();

console.log("✅ Topped up 50 ETH");
console.log("💰 New balance:", ethers.utils.formatEther(pool.balance + 50e18), "ETH");
```

**You can also withdraw unused funds:**

```javascript
// Withdraw 20 ETH from pool
const withdrawTx = await contract.withdrawFromPool(
    ethers.utils.parseEther("20")
);
await withdrawTx.wait();

console.log("✅ Withdrew 20 ETH");
```

---

## 📊 Complete Code Example - Real Usage

```javascript
const { ethers } = require("ethers");
const XRouterClient = require("xrouter-client");

async function main() {
    // Setup (one-time)
    const provider = new ethers.providers.JsonRpcProvider("https://mainnet.infura.io/v3/...");
    const wallet = new ethers.Wallet("YOUR_PRIVATE_KEY", provider);

    const contractAddress = "0x...";
    const contract = new ethers.Contract(contractAddress, ABI, wallet);

    // 1. Check if we have balance
    let pool = await contract.getPaymentPool(wallet.address);

    if (pool.balance == 0) {
        console.log("💸 No balance, depositing 100 ETH...");
        const tx = await contract.depositToPool(
            ethers.utils.parseEther("100"),
            { value: ethers.utils.parseEther("100") }
        );
        await tx.wait();
        console.log("✅ Deposited 100 ETH");
    }

    // 2. Initialize XRouter client
    const xrouter = new XRouterClient({
        rpcUrl: "http://localhost:41414",
        paymentContract: contractAddress,
        wallet: wallet
    });

    // 3. Make request - THIS IS ALL YOU NEED!
    console.log("🔍 Getting ETH block height...");

    const result = await xrouter.getBlockCount("ETH", {
        consensus: 5  // Require 5 nodes
    });

    // 4. Use the verified data
    console.log("\n✅ RESULT:");
    console.log("Block height:", result.blockCount);
    console.log("Verified by:", result.consensusCount, "nodes");
    console.log("Cost:", result.cost);

    // 5. Check remaining balance
    pool = await contract.getPaymentPool(wallet.address);
    console.log("💰 Remaining balance:", ethers.utils.formatEther(pool.balance - pool.reserved), "ETH");
}

main();
```

**Output:**
```
🔍 Getting ETH block height...

✅ RESULT:
Block height: 18500000
Verified by: 5 nodes
Cost: 0.85 ETH
💰 Remaining balance: 99.15 ETH
```

---

## 🎯 Who Does What?

### You (The User):
1. ✅ Deposit to payment pool (once)
2. ✅ Call `xrouter.getBlockCount("ETH")` (simple!)
3. ✅ Receive verified data
4. ✅ Top up pool when needed

### XRouter Client (Automatic):
1. ✅ Creates on-chain request
2. ✅ Finds available nodes
3. ✅ Sends requests to nodes
4. ✅ Collects responses
5. ✅ Returns consensus result to you

### Service Nodes (Automatic):
1. ✅ Receive request
2. ✅ Query their local blockchain node
3. ✅ Send response to you (off-chain)
4. ✅ Submit attestation hash (on-chain)
5. ✅ Get paid automatically

### Smart Contract (Automatic):
1. ✅ Reserves payment when request created
2. ✅ Collects attestations from nodes
3. ✅ Finds consensus when threshold reached
4. ✅ Distributes payments to all nodes
5. ✅ Updates balances and reputation

---

## 📝 Summary - Your Perspective

### First Time Setup:
```javascript
// 1. Deposit once
await contract.depositToPool(ethers.utils.parseEther("100"));
```

### Every Request After:
```javascript
// 2. Just make the call - everything else is automatic!
const result = await xrouter.getBlockCount("ETH", { consensus: 5 });
console.log("Block:", result.blockCount);  // ← This is all you care about!
```

### Occasionally:
```javascript
// 3. Top up when low
await contract.depositToPool(ethers.utils.parseEther("50"));

// Or withdraw if you're done
await contract.withdrawFromPool(ethers.utils.parseEther("20"));
```

---

## ✨ The Magic

**From your perspective:**
- ✅ Make ONE simple call: `xrouter.getBlockCount("ETH")`
- ✅ Get trustless verified data (5+ nodes attested)
- ✅ Pay only when successful
- ✅ No manual payments or settlements
- ✅ Top up as needed

**Behind the scenes:**
- 7 nodes query their ETH nodes
- Each submits attestation to smart contract
- Smart contract finds consensus automatically
- All nodes paid automatically
- You get verified data with cryptographic proof

**It's like calling Infura, but:**
- ✅ Decentralized (not one company)
- ✅ Trustless (consensus of multiple nodes)
- ✅ Verifiable (on-chain attestations)
- ✅ Fair (automatic payment distribution)

---

## 🔐 Data Flow Summary

```
YOU                     XROUTER               NODES (7)           SMART CONTRACT
 │                         │                      │                      │
 │──deposit(100 ETH)───────────────────────────────────────────────────>│
 │                         │                      │                      │
 │──getBlockCount("ETH")──>│                      │                      │
 │                         │                      │                      │
 │                         │──createRequest()─────────────────────────>│
 │                         │                      │                      │
 │                         │──request packets────>│                      │
 │                         │                      │                      │
 │                         │<─responses (off-chain)                      │
 │                         │                      │                      │
 │                         │                      │──submitAttestation()->│
 │                         │                      │  (on-chain)          │
 │                         │                      │                      │
 │                         │                      │<──payments (auto)────│
 │                         │                      │  0.15 ETH each       │
 │                         │                      │                      │
 │<─result: 18500000───────│                      │                      │
 │  (consensus verified)   │                      │                      │
```

**Key Points:**
1. **Off-chain**: Request/response via XRouter network (fast, free)
2. **On-chain**: Payment pool + attestations + distribution (trustless, verifiable)
3. **Hybrid**: Best of both worlds - fast queries + trustless verification

That's the complete journey! Simple from your perspective, powerful under the hood. 🚀
