# XRouter Payment Channel Deployment Guide

This guide provides step-by-step instructions for deploying the XRouter payment channel system.

## Prerequisites

### For Smart Contract Deployment

- Node.js (v14 or later)
- Hardhat or Truffle for contract deployment
- Ethereum node access (Infura, Alchemy, or local node)
- ETH for gas fees
- Solidity compiler (v0.8.0+)

### For Blocknet Core Integration

- Blocknet Core source code
- C++ compiler (GCC 7+ or Clang 5+)
- Development libraries (OpenSSL, Boost, etc.)
- Ethereum node RPC access

## Part 1: Smart Contract Deployment

### Option A: Using Hardhat

#### 1. Install Hardhat

```bash
cd src/xrouter/contracts
npm init -y
npm install --save-dev hardhat @nomiclabs/hardhat-ethers ethers
```

#### 2. Initialize Hardhat Project

```bash
npx hardhat init
```

Select "Create an empty hardhat.config.js"

#### 3. Configure Hardhat

Create `hardhat.config.js`:

```javascript
require("@nomiclabs/hardhat-ethers");

const PRIVATE_KEY = process.env.DEPLOYER_PRIVATE_KEY;
const INFURA_API_KEY = process.env.INFURA_API_KEY;

module.exports = {
  solidity: {
    version: "0.8.0",
    settings: {
      optimizer: {
        enabled: true,
        runs: 200
      }
    }
  },
  networks: {
    // Ethereum Mainnet
    mainnet: {
      url: `https://mainnet.infura.io/v3/${INFURA_API_KEY}`,
      accounts: [`0x${PRIVATE_KEY}`],
      chainId: 1
    },
    // Ethereum Goerli Testnet
    goerli: {
      url: `https://goerli.infura.io/v3/${INFURA_API_KEY}`,
      accounts: [`0x${PRIVATE_KEY}`],
      chainId: 5
    },
    // Ethereum Sepolia Testnet
    sepolia: {
      url: `https://sepolia.infura.io/v3/${INFURA_API_KEY}`,
      accounts: [`0x${PRIVATE_KEY}`],
      chainId: 11155111
    },
    // Local development
    localhost: {
      url: "http://127.0.0.1:8545"
    }
  }
};
```

#### 4. Create Deployment Script

Create `scripts/deploy.js`:

```javascript
const hre = require("hardhat");

async function main() {
  console.log("Deploying XRouterPaymentChannel...");

  const XRouterPaymentChannel = await hre.ethers.getContractFactory("XRouterPaymentChannel");
  const paymentChannel = await XRouterPaymentChannel.deploy();

  await paymentChannel.deployed();

  console.log("XRouterPaymentChannel deployed to:", paymentChannel.address);

  // Save deployment info
  const fs = require("fs");
  const deploymentInfo = {
    network: hre.network.name,
    contractAddress: paymentChannel.address,
    deploymentBlock: await hre.ethers.provider.getBlockNumber(),
    timestamp: new Date().toISOString(),
    deployer: (await hre.ethers.getSigners())[0].address
  };

  fs.writeFileSync(
    `deployment-${hre.network.name}.json`,
    JSON.stringify(deploymentInfo, null, 2)
  );

  console.log("Deployment info saved to deployment-" + hre.network.name + ".json");

  // Verify on Etherscan (if not local)
  if (hre.network.name !== "localhost" && hre.network.name !== "hardhat") {
    console.log("Waiting for block confirmations...");
    await paymentChannel.deployTransaction.wait(6);

    console.log("Verifying contract on Etherscan...");
    try {
      await hre.run("verify:verify", {
        address: paymentChannel.address,
        constructorArguments: []
      });
      console.log("Contract verified on Etherscan");
    } catch (error) {
      console.log("Verification failed:", error.message);
    }
  }
}

main()
  .then(() => process.exit(0))
  .catch((error) => {
    console.error(error);
    process.exit(1);
  });
```

#### 5. Deploy to Testnet (Sepolia)

```bash
# Set environment variables
export DEPLOYER_PRIVATE_KEY="your_private_key_without_0x"
export INFURA_API_KEY="your_infura_api_key"

# Deploy to Sepolia testnet
npx hardhat run scripts/deploy.js --network sepolia
```

#### 6. Deploy to Mainnet

```bash
# IMPORTANT: Ensure you have sufficient ETH for gas fees
# Double-check contract code and configuration

npx hardhat run scripts/deploy.js --network mainnet
```

### Option B: Using Remix IDE

1. Go to [https://remix.ethereum.org](https://remix.ethereum.org)
2. Create new file: `XRouterPaymentChannel.sol`
3. Paste the contract code
4. Compile with Solidity 0.8.0+
5. Connect to MetaMask
6. Deploy to desired network
7. Save the deployed contract address

## Part 2: Verify Deployment

### Check Contract on Etherscan

```bash
# Sepolia
https://sepolia.etherscan.io/address/YOUR_CONTRACT_ADDRESS

# Mainnet
https://etherscan.io/address/YOUR_CONTRACT_ADDRESS
```

Verify:
- ✓ Contract is verified (source code visible)
- ✓ No compilation warnings
- ✓ Matches expected bytecode

### Test Contract Functions

Use Hardhat console:

```bash
npx hardhat console --network sepolia
```

```javascript
const PaymentChannel = await ethers.getContractFactory("XRouterPaymentChannel");
const contract = await PaymentChannel.attach("YOUR_CONTRACT_ADDRESS");

// Test reading (free)
const channelId = "0x0000000000000000000000000000000000000000000000000000000000000000";
const exists = await contract.isChannelOpen(channelId);
console.log("Channel exists:", exists);

// Test opening a channel (costs gas)
const tx = await contract.openChannel(
  "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb", // service node address
  3600, // challenge period
  { value: ethers.utils.parseEther("1.0") } // 1 ETH deposit
);

await tx.wait();
console.log("Channel opened, tx hash:", tx.hash);
```

## Part 3: Blocknet Core Integration

### 1. Update Build Configuration

Edit `src/Makefile.am` to include payment channel files:

```makefile
BITCOIN_CORE_H += \
  xrouter/xrouterpaymentchannel.h

libbitcoin_server_a_SOURCES += \
  xrouter/xrouterpaymentchannel.cpp \
  xrouter/rpcpaymentchannel.cpp
```

### 2. Register RPC Commands

Edit `src/rpc/register.h`:

```cpp
void RegisterPaymentChannelRPCCommands(CRPCTable &t);
```

Edit `src/rpc/server.cpp`:

```cpp
#include <xrouter/rpcpaymentchannel.cpp>

void RegisterAllCoreRPCCommands(CRPCTable &t)
{
    // ... existing registrations ...
    RegisterPaymentChannelRPCCommands(t);
}
```

### 3. Initialize Payment Channel Manager

Edit `src/init.cpp`:

```cpp
#include <xrouter/xrouterpaymentchannel.h>

// Global instance
namespace xrouter {
    XRouterPaymentChannelManager g_paymentChannelManager;
}

bool AppInitMain()
{
    // ... existing initialization ...

    // Initialize payment channel manager
    if (gArgs.GetBoolArg("-xrouterpaymentchannels", false)) {
        std::string ethRpcUrl = gArgs.GetArg("-xrouter.ethereum.rpc", "");
        std::string contractAddr = gArgs.GetArg("-xrouter.paymentchannel.contract", "");

        if (ethRpcUrl.empty() || contractAddr.empty()) {
            return InitError("Payment channels enabled but missing configuration");
        }

        if (!xrouter::g_paymentChannelManager.Init(ethRpcUrl, contractAddr)) {
            return InitError("Failed to initialize payment channel manager");
        }

        LogPrintf("XRouter payment channels initialized\n");
    }

    // ... rest of initialization ...
}
```

### 4. Build Blocknet Core

```bash
cd /path/to/blocknet
./autogen.sh
./configure --enable-debug
make -j$(nproc)
```

### 5. Configure Blocknet Core

Create/edit `blocknet.conf`:

```ini
# Enable payment channels
xrouterpaymentchannels=1

# Ethereum RPC (use your own node or service)
xrouter.ethereum.rpc=https://mainnet.infura.io/v3/YOUR_API_KEY

# Contract address (from deployment)
xrouter.paymentchannel.contract=0xYOUR_DEPLOYED_CONTRACT_ADDRESS

# Ethereum wallet (for signing)
xrouter.ethereum.address=0xYOUR_ETHEREUM_ADDRESS
xrouter.ethereum.privatekey=0xYOUR_ETHEREUM_PRIVATE_KEY

# Channel defaults
xrouter.paymentchannel.challengeperiod=3600
xrouter.paymentchannel.mincapacity=1.0
xrouter.paymentchannel.autoopen=1

# Enable XRouter (if not already enabled)
xrouter=1
```

**IMPORTANT SECURITY NOTES:**
- Never commit private keys to version control
- Use environment variables for sensitive data
- Consider using hardware wallets for mainnet
- Encrypt wallet files

### 6. Start Blocknet Core

```bash
./src/blocknetd -daemon
```

Check logs:

```bash
tail -f ~/.blocknet/debug.log | grep -i "payment\|channel"
```

Expected output:
```
XRouter payment channels initialized
Payment channel manager initialized with 0 channels
```

## Part 4: Testing the Deployment

### 1. Open a Test Channel

```bash
./src/blocknet-cli xrOpenPaymentChannel \
  "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb" \
  5.0 \
  7200
```

Expected response:
```json
{
  "channelId": "4f3d2a1b5c8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4",
  "serviceNodeAddress": "0x742d35Cc6634C0532925a3b844Bc9e7595f0bEb",
  "depositAmount": 5.00000000,
  "challengePeriod": 7200
}
```

### 2. Verify Channel on Blockchain

Check Etherscan for the transaction:
```
https://etherscan.io/tx/[transaction_hash]
```

### 3. Test Payment Creation

```bash
./src/blocknet-cli xrCreateChannelPayment \
  "4f3d2a1b5c8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4" \
  0.01
```

### 4. Check Channel Status

```bash
./src/blocknet-cli xrGetPaymentChannel \
  "4f3d2a1b5c8e9f0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4"
```

### 5. List All Channels

```bash
./src/blocknet-cli xrListPaymentChannels
```

## Part 5: Service Node Setup

### 1. Service Node Configuration

```ini
# blocknet.conf for service node

# Accept payment channels
xrouterpaymentchannels=1
xrouter.acceptchannels=1

# Ethereum configuration
xrouter.ethereum.rpc=https://mainnet.infura.io/v3/YOUR_API_KEY
xrouter.paymentchannel.contract=0xYOUR_DEPLOYED_CONTRACT_ADDRESS
xrouter.ethereum.address=0xYOUR_SERVICE_NODE_ETHEREUM_ADDRESS
xrouter.ethereum.privatekey=0xYOUR_SERVICE_NODE_PRIVATE_KEY

# XRouter service configuration
xrouter=1
```

### 2. Register Service Node

Service nodes should register their Ethereum addresses:

```bash
# Set Ethereum address in XRouter configuration
./src/blocknet-cli xrUpdateConfig \
  paymentAddress \
  "0xYOUR_SERVICE_NODE_ETHEREUM_ADDRESS"
```

### 3. Monitor Channels

Service nodes should run monitoring scripts:

```bash
# Create monitoring script
cat > monitor_channels.sh << 'EOF'
#!/bin/bash

while true; do
  # Check for new channels
  CHANNELS=$(./src/blocknet-cli xrListPaymentChannels)

  # Log channel status
  echo "[$(date)] Active channels: $(echo $CHANNELS | jq 'length')"

  # Check for challenged channels
  CHALLENGED=$(echo $CHANNELS | jq '[.[] | select(.state == "challenged")]')
  if [ "$(echo $CHALLENGED | jq 'length')" -gt 0 ]; then
    echo "WARNING: Challenged channels detected!"
    echo $CHALLENGED | jq
  fi

  sleep 300  # Check every 5 minutes
done
EOF

chmod +x monitor_channels.sh
./monitor_channels.sh &
```

## Part 6: Monitoring and Maintenance

### Monitoring Checklist

- [ ] Contract balance matches expected deposits
- [ ] No failed transactions
- [ ] Channels open/close as expected
- [ ] No pending disputes
- [ ] Gas usage within acceptable limits

### Maintenance Tasks

#### Weekly
- Review channel activity logs
- Check for stale channels
- Monitor gas costs
- Update channel statistics

#### Monthly
- Audit channel states
- Review and optimize gas usage
- Update configuration based on usage patterns
- Test disaster recovery procedures

### Troubleshooting

#### Problem: Contract deployment fails

**Solution:**
- Check gas price is sufficient
- Verify contract compiles without errors
- Ensure sufficient ETH in deployer account
- Check network connection

#### Problem: Channel opening fails

**Solution:**
- Verify contract address is correct
- Check Ethereum node connectivity
- Ensure sufficient ETH for gas
- Verify service node address is valid

#### Problem: Payment verification fails

**Solution:**
- Check signatures are valid
- Verify nonces are incrementing
- Ensure channel is still open
- Check balance calculations

## Part 7: Security Considerations

### Best Practices

1. **Key Management**
   - Use hardware wallets for large deposits
   - Never share private keys
   - Rotate keys periodically
   - Use multi-sig for contract upgrades

2. **Monitoring**
   - Monitor all channel events
   - Alert on suspicious activity
   - Track all state updates
   - Regular security audits

3. **Disaster Recovery**
   - Backup all channel states
   - Document recovery procedures
   - Test recovery processes
   - Maintain off-site backups

4. **Updates**
   - Keep node software updated
   - Monitor security advisories
   - Test updates on testnet first
   - Maintain rollback capability

## Part 8: Upgrade Path

### Contract Upgrades

If contract needs upgrading:

1. Deploy new contract version
2. Migrate existing channels
3. Update configuration
4. Deprecated old contract

### Code Updates

For Blocknet Core updates:

1. Test on testnet
2. Backup existing data
3. Upgrade node software
4. Verify channel functionality
5. Resume normal operations

## Conclusion

Following this deployment guide will result in a fully functional XRouter payment channel system. Always test thoroughly on testnet before mainnet deployment, and implement proper monitoring and security practices.

## Support and Resources

- **Documentation**: See README.md and INTEGRATION.md
- **Issues**: Report at https://github.com/blocknetdx/blocknet/issues
- **Community**: Join Blocknet Discord/Telegram
- **Security**: Report vulnerabilities privately to security@blocknet.org
