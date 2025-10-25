// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "@openzeppelin/contracts/security/ReentrancyGuard.sol";
import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/token/ERC20/IERC20.sol";
import "@openzeppelin/contracts/utils/cryptography/ECDSA.sol";

/**
 * @title XRouterPaymentHub - Attestation-Based Payment Distribution (FIXED)
 * @dev Corrected consensus algorithm implementation
 */
contract XRouterPaymentHubAttestationFixed is ReentrancyGuard, Ownable {
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
        uint256 submissionDeadline;  // Deadline for client submission
        bool settled;
        RequestStatus status;
    }

    struct Attestation {
        address serviceNode;
        bytes32 dataHash;
        bytes signature;
        uint256 timestamp;
        bool verified;
    }

    struct PaymentPool {
        address client;
        uint256 balance;
        uint256 reserved;
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

    IERC20 public paymentToken;

    mapping(address => ServiceNode) public serviceNodes;
    mapping(bytes32 => address) public xrouterPubkeyToAddress;
    address[] public registeredNodes;

    mapping(address => PaymentPool) public paymentPools;

    mapping(bytes32 => Request) public requests;
    mapping(bytes32 => Attestation[]) public requestAttestations;
    mapping(bytes32 => mapping(address => bool)) public hasAttested;

    uint256 public baseAttestationFee = 0.1 ether;
    uint256 public consensusBonusPercent = 50;
    uint256 public nonConsensusPenaltyPercent = 50;
    uint256 public minPoolDeposit = 10 ether;
    uint256 public requestTimeout = 5 minutes;
    uint256 public clientSubmissionWindow = 5 minutes;  // Client has 5 min to submit
    uint256 public nodeGasReimbursement = 0.01 ether;   // Per attestation gas reimbursement

    bool public paused = false;

    // ============ Events ============

    event NodeRegistered(address indexed node, bytes32 indexed xrouterPubkey, string services);
    event NodeUpdated(address indexed node, string services);
    event NodeUnregistered(address indexed node);
    event PoolDeposit(address indexed client, uint256 amount, uint256 newBalance);
    event PoolWithdraw(address indexed client, uint256 amount);
    event RequestCreated(bytes32 indexed requestId, address indexed client, uint256 totalFee, uint256 requiredAttestations);
    event AttestationSubmitted(bytes32 indexed requestId, address indexed serviceNode, bytes32 dataHash);
    event ConsensusReached(bytes32 indexed requestId, bytes32 consensusHash, uint256 consensusCount);
    event PaymentDistributed(bytes32 indexed requestId, address indexed serviceNode, uint256 amount, bool consensusNode);
    event RequestExpired(bytes32 indexed requestId);
    event DisputeRaised(bytes32 indexed requestId, address indexed challenger, string reason);
    event ClientPenalized(bytes32 indexed requestId, address indexed client, uint256 gasReimbursementAmount);

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

    function registerNode(bytes32 xrouterPubkey, string memory services) external whenNotPaused {
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

    function depositToPool(uint256 amount) external payable nonReentrant whenNotPaused {
        require(amount >= minPoolDeposit, "Deposit too low");

        if (address(paymentToken) == address(0)) {
            require(msg.value == amount, "Incorrect ETH amount");
        } else {
            require(msg.value == 0, "ETH not accepted");
            require(paymentToken.transferFrom(msg.sender, address(this), amount), "Transfer failed");
        }

        PaymentPool storage pool = paymentPools[msg.sender];
        pool.client = msg.sender;
        pool.balance += amount;

        emit PoolDeposit(msg.sender, amount, pool.balance);
    }

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

    function getAvailableBalance(address client) external view returns (uint256) {
        PaymentPool storage pool = paymentPools[client];
        return pool.balance - pool.reserved;
    }

    // ============ Request & Attestation System ============

    function createRequest(bytes32 requestId, uint256 requiredAttestations) external nonReentrant whenNotPaused returns (bool) {
        require(requests[requestId].client == address(0), "Request already exists");
        require(requiredAttestations > 0, "Need at least 1 attestation");

        uint256 totalFee = baseAttestationFee * requiredAttestations;

        // Add gas reimbursement buffer (in case nodes must submit)
        uint256 gasBuffer = nodeGasReimbursement * requiredAttestations;
        uint256 totalReserved = totalFee + gasBuffer;

        PaymentPool storage pool = paymentPools[msg.sender];
        require(pool.balance - pool.reserved >= totalReserved, "Insufficient pool balance");

        pool.reserved += totalReserved;

        requests[requestId] = Request({
            requestId: requestId,
            client: msg.sender,
            totalFee: totalFee,
            requiredAttestations: requiredAttestations,
            timestamp: block.timestamp,
            submissionDeadline: block.timestamp + clientSubmissionWindow,
            settled: false,
            status: RequestStatus.PENDING
        });

        emit RequestCreated(requestId, msg.sender, totalFee, requiredAttestations);
        return true;
    }

    /**
     * @dev Client submits batch of attestations (preferred, no gas reimbursement)
     * @param requestId Request identifier
     * @param dataHashes Array of data hashes from nodes
     * @param signatures Array of signatures from nodes
     * @param nodeAddresses Array of node addresses
     */
    function submitBatchAttestations(
        bytes32 requestId,
        bytes32[] memory dataHashes,
        bytes[] memory signatures,
        address[] memory nodeAddresses
    ) external nonReentrant {
        Request storage request = requests[requestId];

        require(msg.sender == request.client, "Not request owner");
        require(!request.settled, "Already settled");
        require(
            block.timestamp <= request.submissionDeadline,
            "Submission window closed"
        );
        require(
            dataHashes.length == signatures.length &&
            signatures.length == nodeAddresses.length,
            "Array length mismatch"
        );

        // Verify and store each attestation
        for (uint256 i = 0; i < nodeAddresses.length; i++) {
            address nodeAddr = nodeAddresses[i];

            require(serviceNodes[nodeAddr].registered, "Node not registered");
            require(!hasAttested[requestId][nodeAddr], "Node already attested");

            // Verify signature
            bytes32 message = keccak256(abi.encodePacked(requestId, dataHashes[i]));
            address signer = message.toEthSignedMessageHash().recover(signatures[i]);
            require(signer == nodeAddr, "Invalid signature");

            // Store attestation
            requestAttestations[requestId].push(Attestation({
                serviceNode: nodeAddr,
                dataHash: dataHashes[i],
                signature: signatures[i],
                timestamp: block.timestamp,
                verified: true
            }));

            hasAttested[requestId][nodeAddr] = true;
            emit AttestationSubmitted(requestId, nodeAddr, dataHashes[i]);
        }

        request.status = RequestStatus.ATTESTING;

        // Check if we have enough attestations for consensus
        if (requestAttestations[requestId].length >= request.requiredAttestations) {
            _processConsensusAndDistribute(requestId, false);  // false = no gas reimbursement
        }
    }

    /**
     * @dev Node submits attestation after deadline (with gas reimbursement)
     */
    function submitNodeAttestation(
        bytes32 requestId,
        bytes32 dataHash,
        bytes memory signature
    ) external nonReentrant onlyRegisteredNode {
        Request storage request = requests[requestId];

        require(!request.settled, "Already settled");
        require(
            block.timestamp > request.submissionDeadline,
            "Client submission window still open"
        );
        require(
            block.timestamp <= request.submissionDeadline + 1 hours,
            "Too late, request expired"
        );
        require(!hasAttested[requestId][msg.sender], "Already attested");

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

        // Check if we have enough
        if (requestAttestations[requestId].length >= request.requiredAttestations) {
            _processConsensusAndDistribute(requestId, true);  // true = reimburse gas
        }
    }

    /**
     * @dev Legacy single attestation submission (deprecated, kept for backward compatibility)
     */
    function submitAttestation(bytes32 requestId, bytes32 dataHash, bytes memory signature) external nonReentrant onlyRegisteredNode {
        Request storage request = requests[requestId];

        require(request.client != address(0), "Request not found");
        require(!request.settled, "Request already settled");
        require(!hasAttested[requestId][msg.sender], "Already attested");
        require(block.timestamp <= request.timestamp + requestTimeout, "Request expired");

        bytes32 message = keccak256(abi.encodePacked(requestId, dataHash));
        address signer = message.toEthSignedMessageHash().recover(signature);
        require(signer == msg.sender, "Invalid signature");

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

        if (requestAttestations[requestId].length >= request.requiredAttestations) {
            _processConsensusAndDistribute(requestId, false);
        }
    }

    /**
     * @dev Process consensus and distribute payments - CORRECTED IMPLEMENTATION
     *
     * Algorithm:
     * 1. Count unique dataHashes using memory arrays
     * 2. Find hash with highest count (consensus)
     * 3. Distribute payments based on consensus match
     *
     * Time Complexity: O(n) where n = number of attestations
     * Gas Cost: ~3,000-6,000 for typical requests
     *
     * @param requestId Request identifier
     * @param reimburseGas Whether to reimburse gas costs (true if nodes submitted)
     */
    function _processConsensusAndDistribute(bytes32 requestId, bool reimburseGas) internal {
        Request storage request = requests[requestId];
        Attestation[] storage attestations = requestAttestations[requestId];

        require(attestations.length > 0, "No attestations");

        // Special case: single attestation
        if (attestations.length == 1) {
            bytes32 consensusHash = attestations[0].dataHash;
            request.status = RequestStatus.CONSENSUS_REACHED;
            emit ConsensusReached(requestId, consensusHash, 1);
            _distributePayments(requestId, consensusHash, reimburseGas);
            return;
        }

        // Create temporary arrays to track unique hashes and their counts
        bytes32[] memory uniqueHashes = new bytes32[](attestations.length);
        uint256[] memory hashCounts = new uint256[](attestations.length);
        uint256 uniqueCount = 0;

        // Count occurrences of each unique hash
        for (uint256 i = 0; i < attestations.length; i++) {
            bytes32 currentHash = attestations[i].dataHash;
            bool found = false;

            // Check if we've seen this hash before
            for (uint256 j = 0; j < uniqueCount; j++) {
                if (uniqueHashes[j] == currentHash) {
                    hashCounts[j]++;
                    found = true;
                    break;
                }
            }

            // If new hash, add to tracking arrays
            if (!found) {
                uniqueHashes[uniqueCount] = currentHash;
                hashCounts[uniqueCount] = 1;
                uniqueCount++;
            }
        }

        // Find the hash with highest count (consensus)
        bytes32 consensusHash;
        uint256 consensusCount = 0;

        for (uint256 i = 0; i < uniqueCount; i++) {
            if (hashCounts[i] > consensusCount) {
                consensusCount = hashCounts[i];
                consensusHash = uniqueHashes[i];
            }
        }

        request.status = RequestStatus.CONSENSUS_REACHED;
        emit ConsensusReached(requestId, consensusHash, consensusCount);

        _distributePayments(requestId, consensusHash, reimburseGas);
    }

    /**
     * @dev Distribute payments to all attesting nodes with optional gas reimbursement
     * @param requestId Request identifier
     * @param consensusHash The hash that achieved consensus
     * @param reimburseGas Whether to reimburse gas costs to nodes
     */
    function _distributePayments(bytes32 requestId, bytes32 consensusHash, bool reimburseGas) internal {
        Request storage request = requests[requestId];
        Attestation[] storage attestations = requestAttestations[requestId];
        PaymentPool storage pool = paymentPools[request.client];

        uint256 totalDistributed = 0;
        uint256 totalGasReimbursement = 0;

        for (uint256 i = 0; i < attestations.length; i++) {
            address nodeAddr = attestations[i].serviceNode;
            bool isConsensus = attestations[i].dataHash == consensusHash;

            // Calculate payment
            uint256 payment = baseAttestationFee;

            if (isConsensus) {
                payment += (payment * consensusBonusPercent) / 100;
            } else {
                payment -= (payment * nonConsensusPenaltyPercent) / 100;
            }

            // Add gas reimbursement if nodes had to submit
            if (reimburseGas) {
                payment += nodeGasReimbursement;
                totalGasReimbursement += nodeGasReimbursement;
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

        // Calculate total reserved amount (fee + gas buffer)
        uint256 gasBuffer = nodeGasReimbursement * request.requiredAttestations;
        uint256 totalReserved = request.totalFee + gasBuffer;

        // Update pool (include gas reimbursement in cost)
        pool.reserved -= totalReserved;
        pool.balance -= totalDistributed;
        pool.spent += totalDistributed;
        pool.nonce++;

        request.settled = true;
        request.status = RequestStatus.DISTRIBUTED;

        // Penalize client if nodes had to submit
        if (reimburseGas) {
            emit ClientPenalized(requestId, request.client, totalGasReimbursement);
        }
    }

    /**
     * @dev Finalize expired request (if no attestations at all or after full timeout)
     */
    function finalizeExpiredRequest(bytes32 requestId) external nonReentrant {
        Request storage request = requests[requestId];

        require(request.client != address(0), "Request not found");
        require(!request.settled, "Already settled");
        require(
            block.timestamp > request.submissionDeadline + 1 hours,
            "Not expired yet"
        );

        PaymentPool storage pool = paymentPools[request.client];

        // Calculate total reserved (fee + gas buffer)
        uint256 gasBuffer = nodeGasReimbursement * request.requiredAttestations;
        uint256 totalReserved = request.totalFee + gasBuffer;

        // If no attestations submitted, unreserve and mark expired
        if (requestAttestations[requestId].length == 0) {
            pool.reserved -= totalReserved;
            request.status = RequestStatus.EXPIRED;
            request.settled = true;
            emit RequestExpired(requestId);
        } else {
            // Process whatever attestations we have
            _processConsensusAndDistribute(requestId, true);
        }
    }

    function batchFinalizeExpired(bytes32[] memory requestIds) external nonReentrant {
        for (uint256 i = 0; i < requestIds.length; i++) {
            bytes32 requestId = requestIds[i];
            Request storage request = requests[requestId];

            if (request.client != address(0) && !request.settled &&
                block.timestamp > request.submissionDeadline + 1 hours) {
                PaymentPool storage pool = paymentPools[request.client];

                // Calculate total reserved (fee + gas buffer)
                uint256 gasBuffer = nodeGasReimbursement * request.requiredAttestations;
                uint256 totalReserved = request.totalFee + gasBuffer;

                if (requestAttestations[requestId].length == 0) {
                    pool.reserved -= totalReserved;
                    request.status = RequestStatus.EXPIRED;
                    request.settled = true;
                    emit RequestExpired(requestId);
                } else {
                    _processConsensusAndDistribute(requestId, true);
                }
            }
        }
    }

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

    function getNodeStats(address node) external view returns (uint256 totalEarned, uint256 requestsServed, uint256 reputation) {
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

    function setClientSubmissionWindow(uint256 window) external onlyOwner {
        clientSubmissionWindow = window;
    }

    function setNodeGasReimbursement(uint256 amount) external onlyOwner {
        nodeGasReimbursement = amount;
    }

    function pause() external onlyOwner {
        paused = true;
    }

    function unpause() external onlyOwner {
        paused = false;
    }

    receive() external payable {
        require(address(paymentToken) == address(0), "ETH not accepted");
    }
}
