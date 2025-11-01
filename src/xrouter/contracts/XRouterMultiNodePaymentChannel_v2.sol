// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

/**
 * @title XRouterMultiNodePaymentChannel v2
 * @dev Efficient multi-node payment channel with OFF-CHAIN consensus validation
 *
 * Key Improvement: All responses and consensus validation happen OFF-CHAIN.
 * Only the final payment distribution is recorded on-chain during settlement.
 *
 * Flow:
 * 1. Client opens channel with N nodes (on-chain, one time)
 * 2. Client queries all N nodes via XRouter P2P (off-chain)
 * 3. Nodes respond off-chain with signed responses
 * 4. Client validates consensus locally (off-chain)
 * 5. Client creates payment state update for honest nodes only (off-chain)
 * 6. Nodes sign the state update (off-chain)
 * 7. Settlement happens via standard payment channel mechanism
 *
 * Gas Savings: No per-response or per-request on-chain transactions!
 */
contract XRouterMultiNodePaymentChannel_v2 {

    // Channel states
    enum ChannelState {
        Open,
        Challenged,
        Closed
    }

    // Service node in channel
    struct ServiceNode {
        address nodeAddress;
        bool active;
    }

    // Multi-node channel with off-chain consensus
    struct Channel {
        address client;                     // Client address
        uint256 clientDeposit;              // Client's deposit
        uint256 clientBalance;              // Client's current balance
        uint256 nonce;                      // State nonce
        uint256 challengePeriod;            // Challenge period
        uint256 closingTime;                // Close initiation time
        ChannelState state;                 // Channel state

        uint256 quorumPercentage;           // Required consensus (51-100)
        address[] serviceNodes;             // Array of node addresses
        mapping(address => uint256) nodeBalances; // Per-node balances
        mapping(address => bool) nodeActive;      // Node active status
    }

    // Mapping from channel ID to channel
    mapping(bytes32 => Channel) public channels;

    // Events
    event ChannelOpened(
        bytes32 indexed channelId,
        address indexed client,
        address[] serviceNodes,
        uint256 clientDeposit,
        uint256 quorumPercentage
    );

    event ChannelStateUpdated(
        bytes32 indexed channelId,
        uint256 nonce,
        uint256 clientBalance,
        bytes32 nodeBalancesHash  // Hash of all node balances
    );

    event ChannelClosed(
        bytes32 indexed channelId,
        uint256 clientFinalBalance
    );

    /**
     * @dev Open a new multi-node channel
     * @param serviceNodes Array of service node addresses
     * @param quorumPercentage Required consensus percentage (51-100)
     * @param challengePeriod Challenge period in seconds
     * @return channelId Unique channel identifier
     */
    function openChannel(
        address[] memory serviceNodes,
        uint256 quorumPercentage,
        uint256 challengePeriod
    ) external payable returns (bytes32) {
        require(serviceNodes.length >= 2, "Minimum 2 nodes required");
        require(quorumPercentage >= 51 && quorumPercentage <= 100, "Invalid quorum");
        require(msg.value > 0, "Must deposit funds");
        require(challengePeriod >= 3600, "Challenge period too short");

        // Verify no duplicate nodes
        for (uint i = 0; i < serviceNodes.length; i++) {
            require(serviceNodes[i] != address(0), "Invalid node address");
            require(serviceNodes[i] != msg.sender, "Client cannot be node");
            for (uint j = i + 1; j < serviceNodes.length; j++) {
                require(serviceNodes[i] != serviceNodes[j], "Duplicate nodes");
            }
        }

        // Generate channel ID
        bytes32 channelId = keccak256(
            abi.encodePacked(msg.sender, serviceNodes, block.timestamp, block.number)
        );

        require(channels[channelId].client == address(0), "Channel exists");

        // Initialize channel
        Channel storage channel = channels[channelId];
        channel.client = msg.sender;
        channel.clientDeposit = msg.value;
        channel.clientBalance = msg.value;
        channel.nonce = 0;
        channel.challengePeriod = challengePeriod;
        channel.closingTime = 0;
        channel.state = ChannelState.Open;
        channel.quorumPercentage = quorumPercentage;

        // Add service nodes
        for (uint i = 0; i < serviceNodes.length; i++) {
            channel.serviceNodes.push(serviceNodes[i]);
            channel.nodeBalances[serviceNodes[i]] = 0;
            channel.nodeActive[serviceNodes[i]] = true;
        }

        emit ChannelOpened(
            channelId,
            msg.sender,
            serviceNodes,
            msg.value,
            quorumPercentage
        );

        return channelId;
    }

    /**
     * @dev Cooperative close with final signed state
     *
     * The client and nodes reach consensus OFF-CHAIN on:
     * 1. Which nodes were honest (provided correct responses)
     * 2. How to distribute the remaining funds
     *
     * All parties sign the final state, then submit for instant settlement.
     *
     * @param channelId The channel to close
     * @param nonce Final state nonce
     * @param clientBalance Client's final balance
     * @param nodeBalances Array of node balances (same order as serviceNodes)
     * @param clientSignature Client's signature
     * @param nodeSignatures Array of node signatures
     */
    function cooperativeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256[] memory nodeBalances,
        bytes memory clientSignature,
        bytes[] memory nodeSignatures
    ) external {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(
            msg.sender == channel.client || channel.nodeActive[msg.sender],
            "Not a participant"
        );

        require(nodeBalances.length == channel.serviceNodes.length, "Invalid balances array");
        require(nodeSignatures.length == channel.serviceNodes.length, "Invalid signatures array");

        // Verify total balances equal deposit
        uint256 totalNodeBalances = 0;
        for (uint i = 0; i < nodeBalances.length; i++) {
            totalNodeBalances += nodeBalances[i];
        }
        require(
            clientBalance + totalNodeBalances == channel.clientDeposit,
            "Balances don't match deposit"
        );

        // Verify nonce
        require(nonce >= channel.nonce, "Invalid nonce");

        // Build state hash
        bytes32 stateHash = getStateHash(
            channelId,
            nonce,
            clientBalance,
            nodeBalances
        );

        // Verify client signature
        require(
            verifySignature(stateHash, clientSignature, channel.client),
            "Invalid client signature"
        );

        // Verify all node signatures
        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            require(
                verifySignature(stateHash, nodeSignatures[i], channel.serviceNodes[i]),
                "Invalid node signature"
            );
        }

        // Close channel immediately
        channel.state = ChannelState.Closed;

        // Transfer funds
        if (clientBalance > 0) {
            payable(channel.client).transfer(clientBalance);
        }

        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            if (nodeBalances[i] > 0) {
                payable(channel.serviceNodes[i]).transfer(nodeBalances[i]);
            }
        }

        emit ChannelClosed(channelId, clientBalance);
    }

    /**
     * @dev Initiate unilateral close (requires challenge period)
     * @param channelId The channel to close
     * @param nonce Latest state nonce
     * @param clientBalance Client's balance
     * @param nodeBalances Array of node balances
     * @param nodeSignatures Signatures from nodes (proving they agreed to this state)
     */
    function challengeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256[] memory nodeBalances,
        bytes[] memory nodeSignatures
    ) external {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(msg.sender == channel.client, "Only client can challenge close");

        require(nodeBalances.length == channel.serviceNodes.length, "Invalid balances");
        require(nodeSignatures.length == channel.serviceNodes.length, "Invalid signatures");

        // Verify balances
        uint256 totalNodeBalances = 0;
        for (uint i = 0; i < nodeBalances.length; i++) {
            totalNodeBalances += nodeBalances[i];
        }
        require(
            clientBalance + totalNodeBalances == channel.clientDeposit,
            "Balances mismatch"
        );

        // Verify nonce
        require(nonce >= channel.nonce, "Nonce too old");

        // Build state hash
        bytes32 stateHash = getStateHash(
            channelId,
            nonce,
            clientBalance,
            nodeBalances
        );

        // Verify node signatures
        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            require(
                verifySignature(stateHash, nodeSignatures[i], channel.serviceNodes[i]),
                "Invalid node signature"
            );
        }

        // Start challenge period
        channel.state = ChannelState.Challenged;
        channel.clientBalance = clientBalance;
        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            channel.nodeBalances[channel.serviceNodes[i]] = nodeBalances[i];
        }
        channel.nonce = nonce;
        channel.closingTime = block.timestamp;

        emit ChannelStateUpdated(
            channelId,
            nonce,
            clientBalance,
            keccak256(abi.encodePacked(nodeBalances))
        );
    }

    /**
     * @dev Dispute a closing state with newer state
     * @param channelId The channel being closed
     * @param nonce Newer state nonce
     * @param clientBalance Client's balance in newer state
     * @param nodeBalances Node balances in newer state
     * @param clientSignature Client's signature on newer state
     */
    function disputeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256[] memory nodeBalances,
        bytes memory clientSignature
    ) external {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Challenged, "Not challenged");
        require(
            block.timestamp < channel.closingTime + channel.challengePeriod,
            "Challenge period expired"
        );
        require(channel.nodeActive[msg.sender], "Not a node in channel");

        // Verify nonce is newer
        require(nonce > channel.nonce, "Nonce not newer");

        // Verify balances
        uint256 totalNodeBalances = 0;
        for (uint i = 0; i < nodeBalances.length; i++) {
            totalNodeBalances += nodeBalances[i];
        }
        require(
            clientBalance + totalNodeBalances == channel.clientDeposit,
            "Balances mismatch"
        );

        // Verify client signature
        bytes32 stateHash = getStateHash(
            channelId,
            nonce,
            clientBalance,
            nodeBalances
        );

        require(
            verifySignature(stateHash, clientSignature, channel.client),
            "Invalid client signature"
        );

        // Update to newer state
        channel.clientBalance = clientBalance;
        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            channel.nodeBalances[channel.serviceNodes[i]] = nodeBalances[i];
        }
        channel.nonce = nonce;
        channel.closingTime = block.timestamp; // Reset challenge period

        emit ChannelStateUpdated(
            channelId,
            nonce,
            clientBalance,
            keccak256(abi.encodePacked(nodeBalances))
        );
    }

    /**
     * @dev Finalize channel close after challenge period
     * @param channelId The channel to finalize
     */
    function finalizeClose(bytes32 channelId) external {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Challenged, "Not challenged");
        require(
            block.timestamp >= channel.closingTime + channel.challengePeriod,
            "Challenge period not expired"
        );

        // Close channel
        channel.state = ChannelState.Closed;

        // Transfer funds
        if (channel.clientBalance > 0) {
            payable(channel.client).transfer(channel.clientBalance);
        }

        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            address nodeAddr = channel.serviceNodes[i];
            uint256 balance = channel.nodeBalances[nodeAddr];
            if (balance > 0) {
                payable(nodeAddr).transfer(balance);
            }
        }

        emit ChannelClosed(channelId, channel.clientBalance);
    }

    /**
     * @dev Get state hash for signing
     */
    function getStateHash(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256[] memory nodeBalances
    ) public pure returns (bytes32) {
        return keccak256(
            abi.encodePacked(
                "\x19Ethereum Signed Message:\n32",
                keccak256(abi.encodePacked(
                    channelId,
                    nonce,
                    clientBalance,
                    nodeBalances
                ))
            )
        );
    }

    /**
     * @dev Verify signature
     */
    function verifySignature(
        bytes32 message,
        bytes memory signature,
        address expectedSigner
    ) public pure returns (bool) {
        require(signature.length == 65, "Invalid signature length");

        bytes32 r;
        bytes32 s;
        uint8 v;

        assembly {
            r := mload(add(signature, 32))
            s := mload(add(signature, 64))
            v := byte(0, mload(add(signature, 96)))
        }

        if (v < 27) {
            v += 27;
        }

        require(v == 27 || v == 28, "Invalid v value");

        address signer = ecrecover(message, v, r, s);
        return signer == expectedSigner;
    }

    /**
     * @dev Get channel information
     */
    function getChannel(bytes32 channelId) external view returns (
        address client,
        uint256 clientBalance,
        uint256 nodeCount,
        uint256 quorumPercentage,
        ChannelState state
    ) {
        Channel storage channel = channels[channelId];
        return (
            channel.client,
            channel.clientBalance,
            channel.serviceNodes.length,
            channel.quorumPercentage,
            channel.state
        );
    }

    /**
     * @dev Get service nodes for channel
     */
    function getServiceNodes(bytes32 channelId) external view returns (address[] memory) {
        return channels[channelId].serviceNodes;
    }

    /**
     * @dev Get node balance
     */
    function getNodeBalance(bytes32 channelId, address nodeAddress) external view returns (uint256) {
        return channels[channelId].nodeBalances[nodeAddress];
    }

    /**
     * @dev Check if channel is open
     */
    function isChannelOpen(bytes32 channelId) external view returns (bool) {
        return channels[channelId].state == ChannelState.Open;
    }
}
