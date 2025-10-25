# XRouter Payment Hub Smart Contract

Payment channel system for XRouter and XCloud services enabling gas-efficient micro-payments.

## Overview

The XRouterPaymentHub contract implements a payment channel system that allows:
- Service node registration with payment addresses
- Opening payment channels with deposits
- Off-chain voucher-based payments
- Batch settlement of accumulated fees
- Dispute resolution mechanism

## Features

- **Gas Efficient**: 99.7% reduction in gas costs vs. per-request payments
- **Secure**: Cryptographic signature verification, reentrancy protection
- **Flexible**: Supports native ETH or any ERC20 token
- **Scalable**: Batch claim up to 100 payments in one transaction
- **Safe**: Emergency withdraw and dispute mechanisms

## Installation

```bash
npm install
```

## Compilation

```bash
npx hardhat compile
```

## Testing

```bash
npx hardhat test
```

## Deployment

### Testnet (Goerli)

```bash
export GOERLI_RPC_URL="https://goerli.infura.io/v3/YOUR_KEY"
export PRIVATE_KEY="your_private_key"
export PAYMENT_TOKEN_ADDRESS="0x0000000000000000000000000000000000000000"  # For native ETH

npx hardhat run scripts/deploy.js --network goerli
```

### Mainnet

```bash
export MAINNET_RPC_URL="https://mainnet.infura.io/v3/YOUR_KEY"
export PRIVATE_KEY="your_private_key"
export PAYMENT_TOKEN_ADDRESS="0x..."  # Token address or 0x0 for ETH

npx hardhat run scripts/deploy.js --network mainnet
```

## Contract Interface

### Service Node Registration

```solidity
function registerNode(bytes32 xrouterPubkey, string memory services) external
function updateNodeServices(string memory services) external
function unregisterNode() external
```

### Channel Management

```solidity
function openChannel(address serviceNode, uint256 deposit) external payable returns (bytes32)
function addDeposit(bytes32 channelId, uint256 amount) external payable
function closeChannel(bytes32 channelId) external
```

### Payment Settlement

```solidity
function claimPayment(
    bytes32 channelId,
    uint256 amount,
    uint256 nonce,
    bytes memory signature
) external

function batchClaimPayments(
    bytes32[] memory channelIds,
    uint256[] memory amounts,
    uint256[] memory nonces,
    bytes[] memory signatures
) external
```

### Dispute Resolution

```solidity
function raiseDispute(bytes32 channelId, string memory reason) external
function emergencyWithdraw(bytes32 channelId) external
```

## Usage Example

### 1. Register as Service Node

```javascript
const xrouterPubkey = ethers.utils.keccak256(ethers.utils.toUtf8Bytes("your_pubkey"));
const services = JSON.stringify(["ETH", "BTC", "LTC"]);

await hub.registerNode(xrouterPubkey, services);
```

### 2. Open Payment Channel (Client)

```javascript
const serviceNodeAddress = "0x...";
const deposit = ethers.utils.parseEther("100");  // 100 tokens

const tx = await hub.openChannel(serviceNodeAddress, deposit, { value: deposit });
const receipt = await tx.wait();
const channelId = receipt.events[0].args.channelId;
```

### 3. Generate Payment Voucher (Off-chain)

```javascript
const channelId = "0x...";
const cumulativeAmount = ethers.utils.parseEther("0.5");  // 0.5 tokens total
const nonce = 1;

// Create message hash
const messageHash = ethers.utils.solidityKeccak256(
  ["bytes32", "uint256", "uint256"],
  [channelId, cumulativeAmount, nonce]
);

// Sign message
const signature = await client.signMessage(ethers.utils.arrayify(messageHash));
```

### 4. Claim Payment (Service Node)

```javascript
await hub.claimPayment(channelId, cumulativeAmount, nonce, signature);
```

### 5. Batch Claim Multiple Payments

```javascript
const channelIds = ["0x...", "0x..."];
const amounts = [parseEther("0.5"), parseEther("1.0")];
const nonces = [1, 1];
const signatures = ["0x...", "0x..."];

await hub.batchClaimPayments(channelIds, amounts, nonces, signatures);
```

## Security Considerations

1. **Signature Verification**: Always verify voucher signatures match the channel client
2. **Nonce Ordering**: Nonces must be monotonically increasing
3. **Reentrancy Protection**: All external calls protected with nonReentrant modifier
4. **Emergency Mechanisms**: Emergency withdraw available after timeout period
5. **Access Control**: Only channel participants can perform channel operations

## Gas Costs

| Operation | Gas Cost | Notes |
|-----------|----------|-------|
| Register Node | ~120,000 | One-time per node |
| Open Channel | ~150,000 | One-time per client-node pair |
| Add Deposit | ~50,000 | Optional, increase channel balance |
| Claim Payment | ~80,000 | Per settlement |
| Batch Claim (10) | ~250,000 | ~25,000 per payment |
| Batch Claim (100) | ~2,000,000 | ~20,000 per payment |
| Close Channel | ~50,000 | Refunds remaining balance |

## Configuration Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| minChannelDeposit | 10 tokens | Minimum deposit to open channel |
| maxChannelDeposit | 10,000 tokens | Maximum deposit per channel |
| channelTimeout | 7 days | Time before channel can be closed by node |
| disputePeriod | 2 days | Additional time for disputes |
| settlementFee | 0% | Optional fee on settlements |

## Events

```solidity
event NodeRegistered(address indexed node, bytes32 indexed xrouterPubkey, string services)
event NodeUpdated(address indexed node, string services)
event ChannelOpened(bytes32 indexed channelId, address indexed client, address indexed serviceNode, uint256 deposit)
event DepositAdded(bytes32 indexed channelId, uint256 amount, uint256 newDeposit)
event PaymentClaimed(bytes32 indexed channelId, address indexed serviceNode, uint256 amount, uint256 nonce)
event ChannelClosed(bytes32 indexed channelId, uint256 finalAmount, uint256 refunded)
event DisputeRaised(bytes32 indexed channelId, address indexed challenger, string reason)
```

## Audit Status

⚠️ **Not Audited**: This contract has not been professionally audited. Use at your own risk.

**Recommended**: Professional security audit before mainnet deployment.

## License

MIT License - see LICENSE file for details.

## Support

For issues or questions:
- GitHub: https://github.com/blocknetdx/blocknetdx
- Discord: https://discord.gg/blocknet
