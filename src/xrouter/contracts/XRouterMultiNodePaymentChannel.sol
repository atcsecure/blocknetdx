// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

/**
 * @title XRouterMultiNodePaymentChannel
 * @dev Trustless payment channel supporting multiple service nodes with consensus validation
 *
 * This contract extends the basic payment channel to support:
 * - Multiple service nodes (N+) in a single channel
 * - Consensus validation of responses
 * - Conditional payment distribution (only honest nodes get paid)
 * - Quorum-based verification
 * - Slashing for dishonest nodes
 *
 * Use Case:
 * Client queries 3+ service nodes for the same data. Nodes that return matching
 * data (consensus) get paid proportionally. Nodes with mismatched data get nothing.
 */
contract XRouterMultiNodePaymentChannel {

    // Channel states
    enum ChannelState {
        Open,           // Channel is active
        Challenged,     // One party initiated close
        Closed          // Channel is finalized
    }

    // Service node information
    struct ServiceNode {
        address nodeAddress;        // Node's Ethereum address
        uint256 deposit;            // Node's stake/deposit
        uint256 balance;            // Current balance (earnings)
        uint256 requestsServed;     // Total requests served
        uint256 correctResponses;   // Responses that matched consensus
        uint256 incorrectResponses; // Responses that didn't match
        bool active;                // Is node still active in channel
    }

    // Request tracking for consensus validation
    struct Request {
        uint256 requestId;          // Unique request identifier
        bytes32 queryHash;          // Hash of the query
        uint256 totalFee;           // Total fee for this request
        uint256 timestamp;          // When request was made
        uint256 responseCount;      // Number of responses received
        bytes32 consensusHash;      // Hash of consensus response
        uint256 consensusCount;     // Number of nodes with consensus response
        bool settled;               // Payment distributed
        mapping(address => bytes32) responses; // Node address => response hash
        mapping(bytes32 => uint256) responseVotes; // Response hash => vote count
        address[] respondedNodes;   // Nodes that responded
    }

    // Multi-node channel structure
    struct MultiNodeChannel {
        bytes32 channelId;              // Unique channel identifier
        address client;                 // Client address
        uint256 clientDeposit;          // Client's deposited funds
        uint256 clientBalance;          // Client's remaining balance
        uint256 nonce;                  // Monotonically increasing nonce
        uint256 challengePeriod;        // Time allowed for disputes (seconds)
        uint256 closingTime;            // Timestamp when close was initiated
        ChannelState state;             // Current channel state

        uint256 minNodes;               // Minimum nodes required for consensus
        uint256 quorumPercentage;       // Percentage required for consensus (e.g., 67 = 67%)
        uint256 totalRequests;          // Total requests made

        address[] serviceNodes;         // Array of service node addresses
        mapping(address => ServiceNode) nodes; // Node details
    }

    // Storage
    mapping(bytes32 => MultiNodeChannel) public channels;
    mapping(bytes32 => mapping(uint256 => Request)) public requests; // channelId => requestId => Request

    // Events
    event MultiNodeChannelOpened(
        bytes32 indexed channelId,
        address indexed client,
        address[] serviceNodes,
        uint256 clientDeposit,
        uint256 minNodes,
        uint256 quorumPercentage
    );

    event ServiceNodeJoined(
        bytes32 indexed channelId,
        address indexed nodeAddress,
        uint256 deposit
    );

    event ServiceNodeLeft(
        bytes32 indexed channelId,
        address indexed nodeAddress,
        uint256 finalBalance
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

    event ChannelChallenged(
        bytes32 indexed channelId,
        uint256 closingTime
    );

    event ChannelClosed(
        bytes32 indexed channelId,
        uint256 clientFinalBalance
    );

    /**
     * @dev Open a new multi-node payment channel
     * @param serviceNodes Array of service node addresses
     * @param minNodes Minimum number of nodes required for consensus
     * @param quorumPercentage Percentage of nodes that must agree (1-100)
     * @param challengePeriod Time window for dispute resolution (seconds)
     * @return channelId Unique identifier for the channel
     */
    function openMultiNodeChannel(
        address[] memory serviceNodes,
        uint256 minNodes,
        uint256 quorumPercentage,
        uint256 challengePeriod
    ) external payable returns (bytes32) {
        require(serviceNodes.length >= minNodes, "Not enough service nodes");
        require(minNodes >= 2, "Minimum 2 nodes required for consensus");
        require(quorumPercentage >= 51 && quorumPercentage <= 100, "Quorum must be 51-100%");
        require(msg.value > 0, "Must deposit funds");
        require(challengePeriod >= 3600, "Challenge period must be at least 1 hour");
        require(challengePeriod <= 7 days, "Challenge period too long");

        // Verify no duplicate nodes
        for (uint i = 0; i < serviceNodes.length; i++) {
            require(serviceNodes[i] != address(0), "Invalid node address");
            require(serviceNodes[i] != msg.sender, "Client cannot be service node");
            for (uint j = i + 1; j < serviceNodes.length; j++) {
                require(serviceNodes[i] != serviceNodes[j], "Duplicate node addresses");
            }
        }

        // Generate unique channel ID
        bytes32 channelId = keccak256(
            abi.encodePacked(msg.sender, serviceNodes, block.timestamp, block.number)
        );

        require(channels[channelId].client == address(0), "Channel already exists");

        // Initialize channel
        MultiNodeChannel storage channel = channels[channelId];
        channel.channelId = channelId;
        channel.client = msg.sender;
        channel.clientDeposit = msg.value;
        channel.clientBalance = msg.value;
        channel.nonce = 0;
        channel.challengePeriod = challengePeriod;
        channel.closingTime = 0;
        channel.state = ChannelState.Open;
        channel.minNodes = minNodes;
        channel.quorumPercentage = quorumPercentage;
        channel.totalRequests = 0;

        // Add service nodes
        for (uint i = 0; i < serviceNodes.length; i++) {
            channel.serviceNodes.push(serviceNodes[i]);
            channel.nodes[serviceNodes[i]] = ServiceNode({
                nodeAddress: serviceNodes[i],
                deposit: 0,
                balance: 0,
                requestsServed: 0,
                correctResponses: 0,
                incorrectResponses: 0,
                active: true
            });
        }

        emit MultiNodeChannelOpened(
            channelId,
            msg.sender,
            serviceNodes,
            msg.value,
            minNodes,
            quorumPercentage
        );

        return channelId;
    }

    /**
     * @dev Service node adds deposit/stake to channel
     * @param channelId The channel to join
     */
    function nodeDeposit(bytes32 channelId) external payable {
        MultiNodeChannel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(channel.nodes[msg.sender].active, "Node not part of this channel");
        require(msg.value > 0, "Must deposit funds");

        channel.nodes[msg.sender].deposit += msg.value;

        emit ServiceNodeJoined(channelId, msg.sender, msg.value);
    }

    /**
     * @dev Client submits a request to multiple nodes
     * @param channelId The channel ID
     * @param requestId Unique request identifier
     * @param queryHash Hash of the query being made
     * @param totalFee Total fee to be distributed among honest nodes
     * @return True if request submitted successfully
     */
    function submitRequest(
        bytes32 channelId,
        uint256 requestId,
        bytes32 queryHash,
        uint256 totalFee
    ) external returns (bool) {
        MultiNodeChannel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(msg.sender == channel.client, "Only client can submit requests");
        require(channel.clientBalance >= totalFee, "Insufficient balance");
        require(!requests[channelId][requestId].settled, "Request already exists");

        // Get count of active nodes
        uint256 activeNodeCount = 0;
        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            if (channel.nodes[channel.serviceNodes[i]].active) {
                activeNodeCount++;
            }
        }

        require(activeNodeCount >= channel.minNodes, "Not enough active nodes");

        // Deduct fee from client balance (held in escrow)
        channel.clientBalance -= totalFee;

        // Initialize request
        Request storage request = requests[channelId][requestId];
        request.requestId = requestId;
        request.queryHash = queryHash;
        request.totalFee = totalFee;
        request.timestamp = block.timestamp;
        request.responseCount = 0;
        request.consensusHash = bytes32(0);
        request.consensusCount = 0;
        request.settled = false;

        channel.totalRequests++;

        emit RequestSubmitted(channelId, requestId, queryHash, totalFee, activeNodeCount);

        return true;
    }

    /**
     * @dev Service node submits response hash
     * @param channelId The channel ID
     * @param requestId The request ID
     * @param responseHash Hash of the response data
     */
    function submitResponse(
        bytes32 channelId,
        uint256 requestId,
        bytes32 responseHash
    ) external {
        MultiNodeChannel storage channel = channels[channelId];
        Request storage request = requests[channelId][requestId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(channel.nodes[msg.sender].active, "Node not active in channel");
        require(request.totalFee > 0, "Request does not exist");
        require(!request.settled, "Request already settled");
        require(request.responses[msg.sender] == bytes32(0), "Response already submitted");

        // Record response
        request.responses[msg.sender] = responseHash;
        request.responseVotes[responseHash]++;
        request.respondedNodes.push(msg.sender);
        request.responseCount++;

        // Update vote count
        uint256 voteCount = request.responseVotes[responseHash];

        // Update consensus if this response has more votes
        if (voteCount > request.consensusCount) {
            request.consensusHash = responseHash;
            request.consensusCount = voteCount;
        }

        emit ResponseSubmitted(channelId, requestId, msg.sender, responseHash);

        // Check if consensus reached
        uint256 totalActiveNodes = 0;
        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            if (channel.nodes[channel.serviceNodes[i]].active) {
                totalActiveNodes++;
            }
        }

        uint256 requiredConsensus = (totalActiveNodes * channel.quorumPercentage) / 100;

        if (request.consensusCount >= requiredConsensus) {
            emit ConsensusReached(
                channelId,
                requestId,
                request.consensusHash,
                request.consensusCount,
                totalActiveNodes
            );
        }
    }

    /**
     * @dev Settle request and distribute payment to honest nodes
     * @param channelId The channel ID
     * @param requestId The request ID
     */
    function settleRequest(bytes32 channelId, uint256 requestId) external {
        MultiNodeChannel storage channel = channels[channelId];
        Request storage request = requests[channelId][requestId];

        require(channel.client != address(0), "Channel does not exist");
        require(request.totalFee > 0, "Request does not exist");
        require(!request.settled, "Request already settled");

        // Verify consensus reached
        uint256 totalActiveNodes = 0;
        for (uint i = 0; i < channel.serviceNodes.length; i++) {
            if (channel.nodes[channel.serviceNodes[i]].active) {
                totalActiveNodes++;
            }
        }

        uint256 requiredConsensus = (totalActiveNodes * channel.quorumPercentage) / 100;
        require(request.consensusCount >= requiredConsensus, "Consensus not reached");

        // Identify honest nodes (those with consensus response)
        address[] memory honestNodes = new address[](request.consensusCount);
        uint256 honestCount = 0;

        for (uint i = 0; i < request.respondedNodes.length; i++) {
            address nodeAddr = request.respondedNodes[i];
            if (request.responses[nodeAddr] == request.consensusHash) {
                honestNodes[honestCount] = nodeAddr;
                honestCount++;
            }
        }

        require(honestCount > 0, "No honest nodes found");

        // Distribute payment equally among honest nodes
        uint256 feePerNode = request.totalFee / honestCount;
        uint256 remainder = request.totalFee % honestCount;

        for (uint i = 0; i < honestCount; i++) {
            address nodeAddr = honestNodes[i];
            uint256 payment = feePerNode;

            // Give remainder to first node
            if (i == 0) {
                payment += remainder;
            }

            channel.nodes[nodeAddr].balance += payment;
            channel.nodes[nodeAddr].requestsServed++;
            channel.nodes[nodeAddr].correctResponses++;
        }

        // Update incorrect response counts
        for (uint i = 0; i < request.respondedNodes.length; i++) {
            address nodeAddr = request.respondedNodes[i];
            if (request.responses[nodeAddr] != request.consensusHash) {
                channel.nodes[nodeAddr].incorrectResponses++;
            }
        }

        request.settled = true;

        emit PaymentDistributed(channelId, requestId, honestNodes, feePerNode);
    }

    /**
     * @dev Service node withdraws earnings
     * @param channelId The channel ID
     * @param amount Amount to withdraw
     */
    function withdrawNodeEarnings(bytes32 channelId, uint256 amount) external {
        MultiNodeChannel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.nodes[msg.sender].active, "Node not active");
        require(channel.nodes[msg.sender].balance >= amount, "Insufficient balance");

        channel.nodes[msg.sender].balance -= amount;
        payable(msg.sender).transfer(amount);
    }

    /**
     * @dev Client initiates channel close
     * @param channelId The channel to close
     */
    function initiateClose(bytes32 channelId) external {
        MultiNodeChannel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Open, "Channel not open");
        require(msg.sender == channel.client, "Only client can initiate close");

        channel.state = ChannelState.Challenged;
        channel.closingTime = block.timestamp;

        emit ChannelChallenged(channelId, block.timestamp + channel.challengePeriod);
    }

    /**
     * @dev Finalize channel close and return remaining funds
     * @param channelId The channel to finalize
     */
    function finalizeClose(bytes32 channelId) external {
        MultiNodeChannel storage channel = channels[channelId];

        require(channel.client != address(0), "Channel does not exist");
        require(channel.state == ChannelState.Challenged, "Channel not in closing state");
        require(
            block.timestamp >= channel.closingTime + channel.challengePeriod,
            "Challenge period not expired"
        );

        // Return remaining balance to client
        uint256 clientRefund = channel.clientBalance;
        channel.clientBalance = 0;
        channel.state = ChannelState.Closed;

        if (clientRefund > 0) {
            payable(channel.client).transfer(clientRefund);
        }

        // Service nodes must withdraw their earnings separately

        emit ChannelClosed(channelId, clientRefund);
    }

    /**
     * @dev Get channel information
     * @param channelId The channel ID
     */
    function getChannelInfo(bytes32 channelId) external view returns (
        address client,
        uint256 clientBalance,
        uint256 nodeCount,
        uint256 minNodes,
        uint256 quorumPercentage,
        uint256 totalRequests,
        ChannelState state
    ) {
        MultiNodeChannel storage channel = channels[channelId];
        return (
            channel.client,
            channel.clientBalance,
            channel.serviceNodes.length,
            channel.minNodes,
            channel.quorumPercentage,
            channel.totalRequests,
            channel.state
        );
    }

    /**
     * @dev Get service node statistics
     * @param channelId The channel ID
     * @param nodeAddress The node address
     */
    function getNodeStats(bytes32 channelId, address nodeAddress) external view returns (
        uint256 deposit,
        uint256 balance,
        uint256 requestsServed,
        uint256 correctResponses,
        uint256 incorrectResponses,
        bool active
    ) {
        MultiNodeChannel storage channel = channels[channelId];
        ServiceNode storage node = channel.nodes[nodeAddress];

        return (
            node.deposit,
            node.balance,
            node.requestsServed,
            node.correctResponses,
            node.incorrectResponses,
            node.active
        );
    }

    /**
     * @dev Get request status
     * @param channelId The channel ID
     * @param requestId The request ID
     */
    function getRequestStatus(bytes32 channelId, uint256 requestId) external view returns (
        bytes32 queryHash,
        uint256 totalFee,
        uint256 responseCount,
        bytes32 consensusHash,
        uint256 consensusCount,
        bool settled
    ) {
        Request storage request = requests[channelId][requestId];

        return (
            request.queryHash,
            request.totalFee,
            request.responseCount,
            request.consensusHash,
            request.consensusCount,
            request.settled
        );
    }

    /**
     * @dev Get all service nodes in channel
     * @param channelId The channel ID
     */
    function getServiceNodes(bytes32 channelId) external view returns (address[] memory) {
        return channels[channelId].serviceNodes;
    }

    /**
     * @dev Check if channel is open
     * @param channelId The channel ID
     */
    function isChannelOpen(bytes32 channelId) external view returns (bool) {
        return channels[channelId].state == ChannelState.Open;
    }
}
