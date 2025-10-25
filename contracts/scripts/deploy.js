const hre = require("hardhat");

async function main() {
  console.log("Deploying XRouterPaymentHub...");

  // Get deployment parameters
  const paymentTokenAddress = process.env.PAYMENT_TOKEN_ADDRESS || ethers.constants.AddressZero;

  console.log("Payment Token Address:", paymentTokenAddress);
  console.log("(Using native ETH)" + (paymentTokenAddress === ethers.constants.AddressZero ? " ✓" : ""));

  // Deploy contract
  const XRouterPaymentHub = await hre.ethers.getContractFactory("XRouterPaymentHub");
  const hub = await XRouterPaymentHub.deploy(paymentTokenAddress);

  await hub.deployed();

  console.log("XRouterPaymentHub deployed to:", hub.address);

  // Wait for block confirmations
  console.log("Waiting for block confirmations...");
  await hub.deployTransaction.wait(5);

  // Verify contract on Etherscan (if not local network)
  if (hre.network.name !== "hardhat" && hre.network.name !== "localhost") {
    console.log("Verifying contract on Etherscan...");
    try {
      await hre.run("verify:verify", {
        address: hub.address,
        constructorArguments: [paymentTokenAddress],
      });
      console.log("Contract verified successfully");
    } catch (error) {
      console.error("Verification failed:", error);
    }
  }

  // Print deployment summary
  console.log("\n=== Deployment Summary ===");
  console.log("Contract Address:", hub.address);
  console.log("Payment Token:", paymentTokenAddress);
  console.log("Network:", hre.network.name);
  console.log("Deployer:", (await hre.ethers.getSigners())[0].address);

  // Print configuration
  const minDeposit = await hub.minChannelDeposit();
  const maxDeposit = await hub.maxChannelDeposit();
  const timeout = await hub.channelTimeout();

  console.log("\n=== Initial Configuration ===");
  console.log("Min Channel Deposit:", hre.ethers.utils.formatEther(minDeposit), "tokens");
  console.log("Max Channel Deposit:", hre.ethers.utils.formatEther(maxDeposit), "tokens");
  console.log("Channel Timeout:", timeout.toString(), "seconds (" + timeout.div(86400).toString() + " days)");

  console.log("\n=== Next Steps ===");
  console.log("1. Save contract address to xrouter.conf:");
  console.log("   eth_payment_contract=" + hub.address);
  console.log("2. Register your service node:");
  console.log("   xrRegisterNode <xrouter_pubkey> <services_json>");
  console.log("3. Clients can now open payment channels");
}

main()
  .then(() => process.exit(0))
  .catch((error) => {
    console.error(error);
    process.exit(1);
  });
