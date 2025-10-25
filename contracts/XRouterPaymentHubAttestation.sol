// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "@openzeppelin/contracts/security/ReentrancyGuard.sol";
import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/token/ERC20/IERC20.sol";
import "@openzeppelin/contracts/utils/cryptography/ECDSA.sol";

/**
 * @title XRouterPaymentHub - Attestation-Based Payment Distribution
 * @dev Payment system for XRouter with multi-node attestation and automatic distribution
 *
 * Architecture:
 * 1. Client deposits funds to general pool
 * 2. Client makes API request requiring N attestations
 * 3. Multiple nodes respond and attest to data validity
 * 4. Contract automatically distributes payment to all attesting nodes
 * 5. Consensus bonus for nodes providing correct data
 *
 * Example:
 * - Client requests ETH block data with 5 attestations
 * - 7 nodes respond: 5 agree on block X, 2 provide different data
 * - Payment distributed: 5 consensus nodes get base fee + bonus
 *                        2 non-consensus nodes get reduced fee
 */
contract XRouterPaymentHub is ReentrancyGuard, Ownable {
    using ECDSA for bytes32;

    // ============ Structs ============

    struct ServiceNode {
        address paymentAddress;
        bytes32 xrouterPubkey;
        string services;
        uint256 registeredAt;
        uint256 reputation;
        uint256 totalEarned;
        uint256 requestsServed;
        bool registered;
    }

    struct Request {
        bytes32 requestId;
        address client;
        uint256 totalFee;
        uint256 requiredAttestations;
        uint256 timestamp;
        bool settled;
        RequestStatus status;
    }

    struct Attestation {
        address serviceNode;
        bytes32 dataHash;           // Hash of the response data
        bytes signature;            // Node's signature
        uint256 timestamp;
        bool verified;
    }

    struct PaymentPool {
        address client;
        uint256 balance;
        uint256 reserved;           // Amount reserved for pending requests
        uint256 spent;
        uint256 nonce;
    }

    enum RequestStatus {
        PENDING,
        ATTESTING,
        CONSENSUS_REACHED,
        DISTRIBUTED,
        DISPUTED,
        EXPIRED
    }

    // ============ State Variables ============

    // Payment token (address(0) for native ETH)
    IERC20 public paymentToken;

    // Service node storage
    mapping(address => ServiceNode) public serviceNodes;
    mapping(bytes32 => address) public xrouterPubkeyToAddress;
    address[] public registeredNodes;

    // Payment pools (client-specific deposits)
    mapping(address => PaymentPool) public paymentPools;

    // Request tracking
    mapping(bytes32 => Request) public requests;
    mapping(bytes32 => Attestation[]) public requestAttestations;
    mapping(bytes32 => mapping(address => bool)) public hasAttested;

    // Fee configuration
    uint256 public baseAttestationFee = 0.1 ether;      // Base fee per attestation
    uint256 public consensusBonusPercent = 50;          // 50% bonus for consensus nodes
    uint256 public nonConsensusPenaltyPercent = 50;     // 50% penalty for wrong data
    uint256 public minPoolDeposit = 10 ether;
    uint256 public requestTimeout = 5 minutes;

    // Emergency controls
    bool public paused = false;

    // ============ Events ============

    event NodeRegistered(address indexed node, bytes32 indexed xrouterPubkey, string services);
    event NodeUpdated(address indexed node, string services);
    event NodeUnregistered(address indexed node);

    event PoolDeposit(address indexed client, uint256 amount, uint256 newBalance);
    event PoolWithdraw(address indexed client, uint256 amount);

    event RequestCreated(
        bytes32 indexed requestId,
        address indexed client,
        uint256 totalFee,
        uint256 requiredAttestations
    );

    event AttestationSubmitted(
        bytes32 indexed requestId,
        address indexed serviceNode,
        bytes32 dataHash
    );

    event ConsensusReached(
        bytes32 indexed requestId,
        bytes32 consensusHash,
        uint256 consensusCount
    );

    event PaymentDistributed(
        bytes32 indexed requestId,
        address indexed serviceNode,
        uint256 amount,
        bool consensusNode
    );

    event RequestExpired(bytes32 indexed requestId);
    event DisputeRaised(bytes32 indexed requestId, address indexed challenger, string reason);

    // ============ Modifiers ============

    modifier whenNotPaused() {
        require(!paused, "Contract is paused");
        _;
    }

    modifier onlyRegisteredNode() {
        require(serviceNodes[msg.sender].registered, "Not a registered node");
        _;
    }

    // ============ Constructor ============

    constructor(address _paymentToken) {
        paymentToken = IERC20(_paymentToken);
    }

    // ============ Service Node Management ============

    function registerNode(
        bytes32 xrouterPubkey,
        string memory services
    ) external whenNotPaused {
        require(!serviceNodes[msg.sender].registered, "Already registered");
        require(xrouterPubkey != bytes32(0), "Invalid pubkey");
        require(xrouterPubkeyToAddress[xrouterPubkey] == address(0), "Pubkey in use");

        serviceNodes[msg.sender] = ServiceNode({
            paymentAddress: msg.sender,
            xrouterPubkey: xrouterPubkey,
            services: services,
            registeredAt: block.timestamp,
            reputation: 100,
            totalEarned: 0,
            requestsServed: 0,
            registered: true
        });

        xrouterPubkeyToAddress[xrouterPubkey] = msg.sender;
        registeredNodes.push(msg.sender);

        emit NodeRegistered(msg.sender, xrouterPubkey, services);
    }

    function updateNodeServices(string memory services) external onlyRegisteredNode {
        serviceNodes[msg.sender].services = services;
        emit NodeUpdated(msg.sender, services);
    }

    function unregisterNode() external onlyRegisteredNode {
        bytes32 pubkey = serviceNodes[msg.sender].xrouterPubkey;
        delete xrouterPubkeyToAddress[pubkey];
        delete serviceNodes[msg.sender];
        emit NodeUnregistered(msg.sender);
    }

    // ============ Payment Pool Management ============

    /**
     * @dev Client deposits funds to their payment pool
     * @param amount Amount to deposit
     */
    function depositToPool(uint256 amount) external payable nonReentrant whenNotPaused {
        require(amount >= minPoolDeposit, "Deposit too low");

        // Handle payment
        if (address(paymentToken) == address(0)) {
            require(msg.value == amount, "Incorrect ETH amount");
        } else {
            require(msg.value == 0, "ETH not accepted");
            require(
                paymentToken.transferFrom(msg.sender, address(this), amount),
                "Transfer failed"
            );
        }

        PaymentPool storage pool = paymentPools[msg.sender];
        pool.client = msg.sender;
        pool.balance += amount;

        emit PoolDeposit(msg.sender, amount, pool.balance);
    }

    /**
     * @dev Withdraw unreserved funds from payment pool
     * @param amount Amount to withdraw
     */
    function withdrawFromPool(uint256 amount) external nonReentrant {
        PaymentPool storage pool = paymentPools[msg.sender];

        uint256 available = pool.balance - pool.reserved;
        require(amount <= available, "Insufficient available balance");

        pool.balance -= amount;

        if (address(paymentToken) == address(0)) {
            (bool success, ) = msg.sender.call{value: amount}("");
            require(success, "ETH transfer failed");
        } else {
            require(paymentToken.transfer(msg.sender, amount), "Transfer failed");
        }

        emit PoolWithdraw(msg.sender, amount);
    }

    /**
     * @dev Get available (unreserved) balance in pool
     */
    function getAvailableBalance(address client) external view returns (uint256) {
        PaymentPool storage pool = paymentPools[client];
        return pool.balance - pool.reserved;
    }

    // ============ Request & Attestation System ============

    /**
     * @dev Create a new request requiring attestations
     * @param requestId Unique request identifier (from client)
     * @param requiredAttestations Number of attestations needed
     * @return success True if request created
     */
    function createRequest(
        bytes32 requestId,
        uint256 requiredAttestations
    ) external nonReentrant whenNotPaused returns (bool) {
        require(requests[requestId].client == address(0), "Request already exists");
        require(requiredAttestations > 0, "Need at least 1 attestation");

        // Calculate total fee
        uint256 totalFee = baseAttestationFee * requiredAttestations;

        PaymentPool storage pool = paymentPools[msg.sender];
        require(pool.balance - pool.reserved >= totalFee, "Insufficient pool balance");

        // Reserve funds
        pool.reserved += totalFee;

        // Create request
        requests[requestId] = Request({
            requestId: requestId,
            client: msg.sender,
            totalFee: totalFee,
            requiredAttestations: requiredAttestations,
            timestamp: block.timestamp,
            settled: false,
            status: RequestStatus.PENDING
        });

        emit RequestCreated(requestId, msg.sender, totalFee, requiredAttestations);

        return true;
    }

    /**
     * @dev Submit attestation for a request
     * @param requestId Request identifier
     * @param dataHash Hash of the response data
     * @param signature Node's signature on the data
     */
    function submitAttestation(
        bytes32 requestId,
        bytes32 dataHash,
        bytes memory signature
    ) external nonReentrant onlyRegisteredNode {
        Request storage request = requests[requestId];

        require(request.client != address(0), "Request not found");
        require(!request.settled, "Request already settled");
        require(!hasAttested[requestId][msg.sender], "Already attested");
        require(block.timestamp <= request.timestamp + requestTimeout, "Request expired");

        // Verify signature
        bytes32 message = keccak256(abi.encodePacked(requestId, dataHash));
        address signer = message.toEthSignedMessageHash().recover(signature);
        require(signer == msg.sender, "Invalid signature");

        // Store attestation
        requestAttestations[requestId].push(Attestation({
            serviceNode: msg.sender,
            dataHash: dataHash,
            signature: signature,
            timestamp: block.timestamp,
            verified: true
        }));

        hasAttested[requestId][msg.sender] = true;
        request.status = RequestStatus.ATTESTING;

        emit AttestationSubmitted(requestId, msg.sender, dataHash);

        // Check if we have enough attestations
        if (requestAttestations[requestId].length >= request.requiredAttestations) {
            _processConsensusAndDistribute(requestId);
        }
    }

    /**
     * @dev Process consensus and distribute payments
     * @param requestId Request identifier
     */
    function _processConsensusAndDistribute(bytes32 requestId) internal {
        Request storage request = requests[requestId];
        Attestation[] storage attestations = requestAttestations[requestId];

        // Find consensus (most common dataHash)
        bytes32 consensusHash;
        uint256 consensusCount;

        // Count occurrences of each dataHash
        mapping(bytes32 => uint256) storage hashCounts;
        for (uint256 i = 0; i < attestations.length; i++) {
            bytes32 hash = attestations[i].dataHash;
            hashCounts[hash]++;

            if (hashCounts[hash] > consensusCount) {
                consensusCount = hashCounts[hash];
                consensusHash = hash;
            }
        }

        request.status = RequestStatus.CONSENSUS_REACHED;
        emit ConsensusReached(requestId, consensusHash, consensusCount);

        // Distribute payments
        _distributePayments(requestId, consensusHash);
    }

    /**
     * @dev Distribute payments to attesting nodes
     * @param requestId Request identifier
     * @param consensusHash The agreed-upon data hash
     */
    function _distributePayments(bytes32 requestId, bytes32 consensusHash) internal {
        Request storage request = requests[requestId];
        Attestation[] storage attestations = requestAttestations[requestId];
        PaymentPool storage pool = paymentPools[request.client];

        uint256 totalDistributed = 0;

        for (uint256 i = 0; i < attestations.length; i++) {
            address nodeAddr = attestations[i].serviceNode;
            bool isConsensus = attestations[i].dataHash == consensusHash;

            // Calculate payment
            uint256 payment = baseAttestationFee;

            if (isConsensus) {
                // Bonus for correct data
                payment += (payment * consensusBonusPercent) / 100;
            } else {
                // Penalty for incorrect data
                payment -= (payment * nonConsensusPenaltyPercent) / 100;
            }

            // Transfer payment
            if (address(paymentToken) == address(0)) {
                (bool success, ) = nodeAddr.call{value: payment}("");
                require(success, "Payment failed");
            } else {
                require(paymentToken.transfer(nodeAddr, payment), "Payment failed");
            }

            // Update node stats
            ServiceNode storage node = serviceNodes[nodeAddr];
            node.totalEarned += payment;
            node.requestsServed++;

            if (isConsensus) {
                node.reputation = node.reputation < 200 ? node.reputation + 1 : 200;
            } else {
                node.reputation = node.reputation > 0 ? node.reputation - 1 : 0;
            }

            totalDistributed += payment;

            emit PaymentDistributed(requestId, nodeAddr, payment, isConsensus);
        }

        // Update pool
        pool.reserved -= request.totalFee;
        pool.balance -= totalDistributed;
        pool.spent += totalDistributed;
        pool.nonce++;

        request.settled = true;
        request.status = RequestStatus.DISTRIBUTED;
    }

    /**
     * @dev Finalize expired requests (can be called by anyone)
     * @param requestId Request identifier
     */
    function finalizeExpiredRequest(bytes32 requestId) external nonReentrant {
        Request storage request = requests[requestId];

        require(request.client != address(0), "Request not found");
        require(!request.settled, "Already settled");
        require(
            block.timestamp > request.timestamp + requestTimeout,
            "Not expired yet"
        );

        PaymentPool storage pool = paymentPools[request.client];

        // Unreserve funds if not enough attestations
        if (requestAttestations[requestId].length < request.requiredAttestations) {
            pool.reserved -= request.totalFee;
            request.status = RequestStatus.EXPIRED;
            emit RequestExpired(requestId);
        } else {
            // Process whatever attestations we have
            _processConsensusAndDistribute(requestId);
        }
    }

    /**
     * @dev Batch finalize multiple expired requests
     */
    function batchFinalizeExpired(bytes32[] memory requestIds) external nonReentrant {
        for (uint256 i = 0; i < requestIds.length; i++) {
            bytes32 requestId = requestIds[i];
            Request storage request = requests[requestId];

            if (request.client != address(0) &&
                !request.settled &&
                block.timestamp > request.timestamp + requestTimeout) {

                PaymentPool storage pool = paymentPools[request.client];

                if (requestAttestations[requestId].length < request.requiredAttestations) {
                    pool.reserved -= request.totalFee;
                    request.status = RequestStatus.EXPIRED;
                    emit RequestExpired(requestId);
                } else {
                    _processConsensusAndDistribute(requestId);
                }
            }
        }
    }

    // ============ Dispute Resolution ============

    function raiseDispute(bytes32 requestId, string memory reason) external {
        Request storage request = requests[requestId];
        require(request.client == msg.sender, "Not request owner");
        require(!request.settled, "Already settled");

        request.status = RequestStatus.DISPUTED;
        emit DisputeRaised(requestId, msg.sender, reason);
    }

    // ============ View Functions ============

    function getRequest(bytes32 requestId) external view returns (Request memory) {
        return requests[requestId];
    }

    function getAttestations(bytes32 requestId) external view returns (Attestation[] memory) {
        return requestAttestations[requestId];
    }

    function getAttestationCount(bytes32 requestId) external view returns (uint256) {
        return requestAttestations[requestId].length;
    }

    function getServiceNode(address node) external view returns (ServiceNode memory) {
        return serviceNodes[node];
    }

    function getRegisteredNodes() external view returns (address[] memory) {
        return registeredNodes;
    }

    function getPaymentPool(address client) external view returns (PaymentPool memory) {
        return paymentPools[client];
    }

    function getNodeStats(address node) external view returns (
        uint256 totalEarned,
        uint256 requestsServed,
        uint256 reputation
    ) {
        ServiceNode storage snode = serviceNodes[node];
        return (snode.totalEarned, snode.requestsServed, snode.reputation);
    }

    // ============ Admin Functions ============

    function setBaseAttestationFee(uint256 fee) external onlyOwner {
        baseAttestationFee = fee;
    }

    function setConsensusBonusPercent(uint256 percent) external onlyOwner {
        require(percent <= 100, "Invalid percentage");
        consensusBonusPercent = percent;
    }

    function setNonConsensusPenalty(uint256 percent) external onlyOwner {
        require(percent <= 100, "Invalid percentage");
        nonConsensusPenaltyPercent = percent;
    }

    function setMinPoolDeposit(uint256 amount) external onlyOwner {
        minPoolDeposit = amount;
    }

    function setRequestTimeout(uint256 timeout) external onlyOwner {
        requestTimeout = timeout;
    }

    function pause() external onlyOwner {
        paused = true;
    }

    function unpause() external onlyOwner {
        paused = false;
    }

    // ============ Fallback ============

    receive() external payable {
        require(address(paymentToken) == address(0), "ETH not accepted");
    }
}
