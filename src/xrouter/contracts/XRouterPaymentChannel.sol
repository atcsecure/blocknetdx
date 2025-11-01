// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

/**
 * @title XRouterPaymentChannel
 * @dev Trustless bidirectional payment channel for XRouter service payments
 *
 * This contract implements a Layer 2 payment channel that allows:
 * - Multiple off-chain payments without on-chain transactions
 * - Bidirectional payments between client and service node
 * - Trustless operation with cryptographic signatures
 * - Dispute resolution with challenge period
 * - Efficient batch settlement
 *
 * Based on Ethereum state channel patterns with enhancements for XRouter.
 */
contract XRouterPaymentChannel {

    // Channel states
    enum ChannelState {
        Open,           // Channel is active
        Challenged,     // One party initiated close
        Closed          // Channel is finalized
    }

    // Channel structure
    struct Channel {
        address client;              // Client address
        address serviceNode;         // Service node address
        uint256 clientDeposit;       // Client's deposited funds
        uint256 serviceNodeDeposit;  // Service node's deposited funds
        uint256 clientBalance;       // Client's current balance
        uint256 serviceNodeBalance;  // Service node's current balance
        uint256 nonce;               // Monotonically increasing nonce
        uint256 challengePeriod;     // Time allowed for disputes (in seconds)
        uint256 closingTime;         // Timestamp when close was initiated
        ChannelState state;          // Current channel state
    }

    // Mapping from channel ID to channel data
    mapping(bytes32 => Channel) public channels;

    // Events
    event ChannelOpened(
        bytes32 indexed channelId,
        address indexed client,
        address indexed serviceNode,
        uint256 clientDeposit,
        uint256 serviceNodeDeposit,
        uint256 challengePeriod
    );

    event ChannelChallenged(
        bytes32 indexed channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256 serviceNodeBalance,
        uint256 closingTime
    );

    event ChannelClosed(
        bytes32 indexed channelId,
        uint256 clientFinalBalance,
        uint256 serviceNodeFinalBalance
    );

    event ChannelDisputed(
        bytes32 indexed channelId,
        uint256 newNonce,
        uint256 newClientBalance,
        uint256 newServiceNodeBalance
    );

    /**
     * @dev Open a new payment channel
     * @param serviceNode Address of the service node
     * @param challengePeriod Time window for dispute resolution (seconds)
     * @return channelId Unique identifier for the channel
     */
    function openChannel(
        address serviceNode,
        uint256 challengePeriod
    ) external payable returns (bytes32) {
        require(serviceNode != address(0), "Invalid service node address");
        require(serviceNode != msg.sender, "Cannot open channel with self");
        require(msg.value > 0, "Must deposit funds");
        require(challengePeriod >= 3600, "Challenge period must be at least 1 hour");
        require(challengePeriod <= 7 days, "Challenge period too long");

        // Generate unique channel ID
        bytes32 channelId = keccak256(
            abi.encodePacked(msg.sender, serviceNode, block.timestamp, block.number)
        );

        require(channels[channelId].client == address(0), "Channel already exists");

        // Initialize channel
        channels[channelId] = Channel({
            client: msg.sender,
            serviceNode: serviceNode,
            clientDeposit: msg.value,
            serviceNodeDeposit: 0,
            clientBalance: msg.value,
            serviceNodeBalance: 0,
            nonce: 0,
            challengePeriod: challengePeriod,
            closingTime: 0,
            state: ChannelState.Open
        });

        emit ChannelOpened(
            channelId,
            msg.sender,
            serviceNode,
            msg.value,
            0,
            challengePeriod
        );

        return channelId;
    }

    /**
     * @dev Service node adds deposit to existing channel
     * @param channelId The channel to deposit into
     */
    function depositServiceNode(bytes32 channelId) external payable {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(msg.sender == channel.serviceNode, "Only service node can deposit");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(msg.value > 0, "Must deposit funds");

        channel.serviceNodeDeposit += msg.value;
        channel.serviceNodeBalance += msg.value;
    }

    /**
     * @dev Client adds more deposit to existing channel
     * @param channelId The channel to deposit into
     */
    function depositClient(bytes32 channelId) external payable {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(msg.sender == channel.client, "Only client can deposit");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(msg.value > 0, "Must deposit funds");

        channel.clientDeposit += msg.value;
        channel.clientBalance += msg.value;
    }

    /**
     * @dev Initiate cooperative close with final signed state
     * @param channelId The channel to close
     * @param nonce The nonce of the final state
     * @param clientBalance Client's final balance
     * @param serviceNodeBalance Service node's final balance
     * @param clientSignature Client's signature of the final state
     * @param serviceNodeSignature Service node's signature of the final state
     */
    function cooperativeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256 serviceNodeBalance,
        bytes memory clientSignature,
        bytes memory serviceNodeSignature
    ) external {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(
            msg.sender == channel.client || msg.sender == channel.serviceNode,
            "Not a channel participant"
        );

        // Verify balances
        uint256 totalDeposit = channel.clientDeposit + channel.serviceNodeDeposit;
        require(
            clientBalance + serviceNodeBalance == totalDeposit,
            "Balances don't match deposits"
        );

        // Verify nonce is newer than current
        require(nonce >= channel.nonce, "Nonce must be >= current nonce");

        // Verify signatures
        bytes32 message = getStateHash(
            channelId,
            nonce,
            clientBalance,
            serviceNodeBalance
        );

        require(
            verifySignature(message, clientSignature, channel.client),
            "Invalid client signature"
        );
        require(
            verifySignature(message, serviceNodeSignature, channel.serviceNode),
            "Invalid service node signature"
        );

        // Close channel immediately with final balances
        channel.state = ChannelState.Closed;
        channel.clientBalance = clientBalance;
        channel.serviceNodeBalance = serviceNodeBalance;
        channel.nonce = nonce;

        // Transfer funds
        if (clientBalance > 0) {
            payable(channel.client).transfer(clientBalance);
        }
        if (serviceNodeBalance > 0) {
            payable(channel.serviceNode).transfer(serviceNodeBalance);
        }

        emit ChannelClosed(channelId, clientBalance, serviceNodeBalance);
    }

    /**
     * @dev Initiate unilateral close (requires challenge period)
     * @param channelId The channel to close
     * @param nonce The nonce of the latest state
     * @param clientBalance Client's balance in latest state
     * @param serviceNodeBalance Service node's balance in latest state
     * @param signature Signature from the other party
     */
    function challengeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256 serviceNodeBalance,
        bytes memory signature
    ) external {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(
            msg.sender == channel.client || msg.sender == channel.serviceNode,
            "Not a channel participant"
        );

        // Verify balances
        uint256 totalDeposit = channel.clientDeposit + channel.serviceNodeDeposit;
        require(
            clientBalance + serviceNodeBalance == totalDeposit,
            "Balances don't match deposits"
        );

        // Verify nonce is newer
        require(nonce >= channel.nonce, "Nonce must be >= current nonce");

        // Verify signature from other party
        bytes32 message = getStateHash(
            channelId,
            nonce,
            clientBalance,
            serviceNodeBalance
        );

        address otherParty = (msg.sender == channel.client)
            ? channel.serviceNode
            : channel.client;

        require(
            verifySignature(message, signature, otherParty),
            "Invalid signature from other party"
        );

        // Start challenge period
        channel.state = ChannelState.Challenged;
        channel.clientBalance = clientBalance;
        channel.serviceNodeBalance = serviceNodeBalance;
        channel.nonce = nonce;
        channel.closingTime = block.timestamp;

        emit ChannelChallenged(
            channelId,
            nonce,
            clientBalance,
            serviceNodeBalance,
            block.timestamp + channel.challengePeriod
        );
    }

    /**
     * @dev Challenge a closing state with a newer state
     * @param channelId The channel being closed
     * @param nonce The nonce of the newer state
     * @param clientBalance Client's balance in newer state
     * @param serviceNodeBalance Service node's balance in newer state
     * @param signature Signature from the party who initiated close
     */
    function disputeClose(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256 serviceNodeBalance,
        bytes memory signature
    ) external {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Challenged, "Channel not challenged");
        require(
            block.timestamp < channel.closingTime + channel.challengePeriod,
            "Challenge period expired"
        );

        // Verify nonce is newer than challenged state
        require(nonce > channel.nonce, "Nonce must be greater than current");

        // Verify balances
        uint256 totalDeposit = channel.clientDeposit + channel.serviceNodeDeposit;
        require(
            clientBalance + serviceNodeBalance == totalDeposit,
            "Balances don't match deposits"
        );

        // Verify signature
        bytes32 message = getStateHash(
            channelId,
            nonce,
            clientBalance,
            serviceNodeBalance
        );

        address closingParty = (msg.sender == channel.client)
            ? channel.serviceNode
            : channel.client;

        require(
            verifySignature(message, signature, closingParty),
            "Invalid signature"
        );

        // Update to newer state
        channel.clientBalance = clientBalance;
        channel.serviceNodeBalance = serviceNodeBalance;
        channel.nonce = nonce;
        channel.closingTime = block.timestamp; // Reset challenge period

        emit ChannelDisputed(channelId, nonce, clientBalance, serviceNodeBalance);
    }

    /**
     * @dev Finalize channel close after challenge period
     * @param channelId The channel to finalize
     */
    function finalizeClose(bytes32 channelId) external {
        Channel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Challenged, "Channel not challenged");
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
        if (channel.serviceNodeBalance > 0) {
            payable(channel.serviceNode).transfer(channel.serviceNodeBalance);
        }

        emit ChannelClosed(
            channelId,
            channel.clientBalance,
            channel.serviceNodeBalance
        );
    }

    /**
     * @dev Get the hash of a channel state for signing
     * @param channelId The channel ID
     * @param nonce The state nonce
     * @param clientBalance Client's balance
     * @param serviceNodeBalance Service node's balance
     * @return Hash of the state
     */
    function getStateHash(
        bytes32 channelId,
        uint256 nonce,
        uint256 clientBalance,
        uint256 serviceNodeBalance
    ) public pure returns (bytes32) {
        return keccak256(
            abi.encodePacked(
                "\x19Ethereum Signed Message:\n32",
                keccak256(abi.encodePacked(
                    channelId,
                    nonce,
                    clientBalance,
                    serviceNodeBalance
                ))
            )
        );
    }

    /**
     * @dev Verify a signature
     * @param message The signed message hash
     * @param signature The signature to verify
     * @param expectedSigner The expected signer address
     * @return True if signature is valid
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

        // Handle EIP-2 change
        if (v < 27) {
            v += 27;
        }

        require(v == 27 || v == 28, "Invalid signature v value");

        address recoveredSigner = ecrecover(message, v, r, s);
        return recoveredSigner == expectedSigner;
    }

    /**
     * @dev Get channel details
     * @param channelId The channel ID
     * @return Channel struct
     */
    function getChannel(bytes32 channelId) external view returns (Channel memory) {
        return channels[channelId];
    }

    /**
     * @dev Check if channel exists and is open
     * @param channelId The channel ID
     * @return True if channel is open
     */
    function isChannelOpen(bytes32 channelId) external view returns (bool) {
        return channels[channelId].state == ChannelState.Open;
    }

    /**
     * @dev Get channel balance for a specific participant
     * @param channelId The channel ID
     * @param participant The participant address
     * @return The participant's balance
     */
    function getBalance(bytes32 channelId, address participant) external view returns (uint256) {
        Channel storage channel = channels[channelId];

        if (participant == channel.client) {
            return channel.clientBalance;
        } else if (participant == channel.serviceNode) {
            return channel.serviceNodeBalance;
        }

        return 0;
    }
}
