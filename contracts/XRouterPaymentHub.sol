// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "@openzeppelin/contracts/security/ReentrancyGuard.sol";
import "@openzeppelin/contracts/access/Ownable.sol";
import "@openzeppelin/contracts/token/ERC20/IERC20.sol";
import "@openzeppelin/contracts/utils/cryptography/ECDSA.sol";

/**
 * @title XRouterPaymentHub
 * @dev Payment channel contract for XRouter and XCloud services
 *
 * Features:
 * - Service node registration with payment addresses
 * - Payment channels for gas-efficient micro-payments
 * - Off-chain voucher system with on-chain settlement
 * - Dispute resolution mechanism
 * - Multi-token support (native ETH + ERC20)
 */
contract XRouterPaymentHub is ReentrancyGuard, Ownable {
    using ECDSA for bytes32;

    // ============ Structs ============

    struct Channel {
        address client;
        address serviceNode;
        uint256 deposit;
        uint256 settled;
        uint256 nonce;
        uint256 timeout;
        uint256 lastUpdated;
        bool active;
    }

    struct ServiceNode {
        address paymentAddress;
        bytes32 xrouterPubkey;
        string services;
        uint256 registeredAt;
        uint256 reputation;
        bool registered;
    }

    struct Settlement {
        bytes32 channelId;
        uint256 amount;
        uint256 nonce;
        uint256 timestamp;
        bytes32 txHash;
    }

    // ============ State Variables ============

    // Payment token (address(0) for native ETH)
    IERC20 public paymentToken;

    // Channel storage
    mapping(bytes32 => Channel) public channels;
    mapping(address => bytes32[]) public userChannels;
    mapping(address => bytes32[]) public nodeChannels;

    // Service node storage
    mapping(address => ServiceNode) public serviceNodes;
    mapping(bytes32 => address) public xrouterPubkeyToAddress;
    address[] public registeredNodes;

    // Settlement tracking
    mapping(bytes32 => Settlement[]) public channelSettlements;
    mapping(bytes32 => mapping(uint256 => bool)) public usedNonces;

    // Configuration
    uint256 public minChannelDeposit = 10 ether;  // 10 tokens minimum
    uint256 public maxChannelDeposit = 10000 ether;  // 10,000 tokens maximum
    uint256 public channelTimeout = 7 days;
    uint256 public disputePeriod = 2 days;
    uint256 public settlementFee = 0;  // Fee percentage (0-100, 0 = no fee)

    // Emergency controls
    bool public paused = false;

    // ============ Events ============

    event NodeRegistered(
        address indexed node,
        bytes32 indexed xrouterPubkey,
        string services
    );

    event NodeUpdated(
        address indexed node,
        string services
    );

    event NodeUnregistered(
        address indexed node
    );

    event ChannelOpened(
        bytes32 indexed channelId,
        address indexed client,
        address indexed serviceNode,
        uint256 deposit
    );

    event DepositAdded(
        bytes32 indexed channelId,
        uint256 amount,
        uint256 newDeposit
    );

    event PaymentClaimed(
        bytes32 indexed channelId,
        address indexed serviceNode,
        uint256 amount,
        uint256 nonce
    );

    event ChannelClosed(
        bytes32 indexed channelId,
        uint256 finalAmount,
        uint256 refunded
    );

    event DisputeRaised(
        bytes32 indexed channelId,
        address indexed challenger,
        string reason
    );

    event EmergencyWithdraw(
        bytes32 indexed channelId,
        address indexed client,
        uint256 amount
    );

    // ============ Modifiers ============

    modifier whenNotPaused() {
        require(!paused, "Contract is paused");
        _;
    }

    modifier onlyClient(bytes32 channelId) {
        require(channels[channelId].client == msg.sender, "Not channel client");
        _;
    }

    modifier onlyServiceNode(bytes32 channelId) {
        require(channels[channelId].serviceNode == msg.sender, "Not service node");
        _;
    }

    modifier channelExists(bytes32 channelId) {
        require(channels[channelId].active, "Channel not active");
        _;
    }

    // ============ Constructor ============

    /**
     * @dev Constructor
     * @param _paymentToken Address of payment token (address(0) for native ETH)
     */
    constructor(address _paymentToken) {
        paymentToken = IERC20(_paymentToken);
    }

    // ============ Service Node Management ============

    /**
     * @dev Register as a service node
     * @param xrouterPubkey XRouter public key hash
     * @param services JSON string listing offered services
     */
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
            reputation: 100,  // Starting reputation
            registered: true
        });

        xrouterPubkeyToAddress[xrouterPubkey] = msg.sender;
        registeredNodes.push(msg.sender);

        emit NodeRegistered(msg.sender, xrouterPubkey, services);
    }

    /**
     * @dev Update service offerings
     * @param services New service list
     */
    function updateNodeServices(string memory services) external {
        require(serviceNodes[msg.sender].registered, "Not registered");

        serviceNodes[msg.sender].services = services;

        emit NodeUpdated(msg.sender, services);
    }

    /**
     * @dev Unregister as a service node
     */
    function unregisterNode() external {
        require(serviceNodes[msg.sender].registered, "Not registered");

        // Check no active channels
        bytes32[] memory nodeChannelIds = nodeChannels[msg.sender];
        for (uint256 i = 0; i < nodeChannelIds.length; i++) {
            require(!channels[nodeChannelIds[i]].active, "Has active channels");
        }

        bytes32 pubkey = serviceNodes[msg.sender].xrouterPubkey;
        delete xrouterPubkeyToAddress[pubkey];
        delete serviceNodes[msg.sender];

        emit NodeUnregistered(msg.sender);
    }

    // ============ Channel Management ============

    /**
     * @dev Open a payment channel
     * @param serviceNode Address of the service node
     * @param deposit Initial deposit amount
     * @return channelId Unique channel identifier
     */
    function openChannel(
        address serviceNode,
        uint256 deposit
    ) external payable nonReentrant whenNotPaused returns (bytes32) {
        require(serviceNodes[serviceNode].registered, "Node not registered");
        require(deposit >= minChannelDeposit, "Deposit too low");
        require(deposit <= maxChannelDeposit, "Deposit too high");

        // Generate unique channel ID
        bytes32 channelId = keccak256(
            abi.encodePacked(msg.sender, serviceNode, block.timestamp, block.number)
        );
        require(!channels[channelId].active, "Channel already exists");

        // Handle payment
        if (address(paymentToken) == address(0)) {
            // Native ETH
            require(msg.value == deposit, "Incorrect ETH amount");
        } else {
            // ERC20 token
            require(msg.value == 0, "ETH not accepted");
            require(
                paymentToken.transferFrom(msg.sender, address(this), deposit),
                "Transfer failed"
            );
        }

        // Create channel
        channels[channelId] = Channel({
            client: msg.sender,
            serviceNode: serviceNode,
            deposit: deposit,
            settled: 0,
            nonce: 0,
            timeout: block.timestamp + channelTimeout,
            lastUpdated: block.timestamp,
            active: true
        });

        userChannels[msg.sender].push(channelId);
        nodeChannels[serviceNode].push(channelId);

        emit ChannelOpened(channelId, msg.sender, serviceNode, deposit);

        return channelId;
    }

    /**
     * @dev Add more funds to an existing channel
     * @param channelId Channel identifier
     * @param amount Amount to add
     */
    function addDeposit(
        bytes32 channelId,
        uint256 amount
    ) external payable nonReentrant channelExists(channelId) onlyClient(channelId) {
        Channel storage channel = channels[channelId];

        require(
            channel.deposit + amount <= maxChannelDeposit,
            "Exceeds max deposit"
        );

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

        channel.deposit += amount;
        channel.lastUpdated = block.timestamp;

        emit DepositAdded(channelId, amount, channel.deposit);
    }

    /**
     * @dev Close a channel and refund remaining balance
     * @param channelId Channel identifier
     */
    function closeChannel(
        bytes32 channelId
    ) external nonReentrant channelExists(channelId) {
        Channel storage channel = channels[channelId];

        // Only client can close, or node after timeout
        bool isClient = msg.sender == channel.client;
        bool isNodeAfterTimeout = msg.sender == channel.serviceNode &&
                                   block.timestamp > channel.timeout;

        require(isClient || isNodeAfterTimeout, "Not authorized");

        uint256 refund = channel.deposit - channel.settled;
        channel.active = false;

        // Refund remaining balance to client
        if (refund > 0) {
            if (address(paymentToken) == address(0)) {
                (bool success, ) = channel.client.call{value: refund}("");
                require(success, "ETH transfer failed");
            } else {
                require(
                    paymentToken.transfer(channel.client, refund),
                    "Transfer failed"
                );
            }
        }

        emit ChannelClosed(channelId, channel.settled, refund);
    }

    // ============ Payment Settlement ============

    /**
     * @dev Claim payment with signed voucher
     * @param channelId Channel identifier
     * @param amount Cumulative amount to claim
     * @param nonce Request nonce
     * @param signature Client's signature
     */
    function claimPayment(
        bytes32 channelId,
        uint256 amount,
        uint256 nonce,
        bytes memory signature
    ) external nonReentrant channelExists(channelId) onlyServiceNode(channelId) {
        Channel storage channel = channels[channelId];

        // Verify nonce
        require(nonce > channel.nonce, "Invalid nonce");
        require(!usedNonces[channelId][nonce], "Nonce already used");

        // Verify amount
        require(amount > channel.settled, "Amount not increased");
        require(amount <= channel.deposit, "Exceeds deposit");

        // Verify signature
        bytes32 message = getPaymentHash(channelId, amount, nonce);
        address signer = message.toEthSignedMessageHash().recover(signature);
        require(signer == channel.client, "Invalid signature");

        // Calculate claimable amount
        uint256 claimable = amount - channel.settled;

        // Apply settlement fee (if any)
        uint256 fee = (claimable * settlementFee) / 100;
        uint256 payout = claimable - fee;

        // Update channel state
        channel.settled = amount;
        channel.nonce = nonce;
        channel.lastUpdated = block.timestamp;
        usedNonces[channelId][nonce] = true;

        // Record settlement
        channelSettlements[channelId].push(Settlement({
            channelId: channelId,
            amount: payout,
            nonce: nonce,
            timestamp: block.timestamp,
            txHash: blockhash(block.number - 1)
        }));

        // Transfer payment
        if (address(paymentToken) == address(0)) {
            (bool success, ) = channel.serviceNode.call{value: payout}("");
            require(success, "ETH transfer failed");
        } else {
            require(
                paymentToken.transfer(channel.serviceNode, payout),
                "Transfer failed"
            );
        }

        emit PaymentClaimed(channelId, channel.serviceNode, payout, nonce);
    }

    /**
     * @dev Batch claim payments from multiple channels
     * @param channelIds Array of channel identifiers
     * @param amounts Array of cumulative amounts
     * @param nonces Array of nonces
     * @param signatures Array of signatures
     */
    function batchClaimPayments(
        bytes32[] memory channelIds,
        uint256[] memory amounts,
        uint256[] memory nonces,
        bytes[] memory signatures
    ) external nonReentrant {
        require(
            channelIds.length == amounts.length &&
            amounts.length == nonces.length &&
            nonces.length == signatures.length,
            "Array length mismatch"
        );

        require(channelIds.length <= 100, "Batch too large");

        for (uint256 i = 0; i < channelIds.length; i++) {
            // Verify this is the service node for all channels
            require(
                channels[channelIds[i]].serviceNode == msg.sender,
                "Not service node for all channels"
            );

            // Process each claim internally
            _claimPaymentInternal(
                channelIds[i],
                amounts[i],
                nonces[i],
                signatures[i]
            );
        }
    }

    /**
     * @dev Internal claim payment function for batch processing
     */
    function _claimPaymentInternal(
        bytes32 channelId,
        uint256 amount,
        uint256 nonce,
        bytes memory signature
    ) internal {
        Channel storage channel = channels[channelId];

        require(channel.active, "Channel not active");
        require(nonce > channel.nonce, "Invalid nonce");
        require(!usedNonces[channelId][nonce], "Nonce already used");
        require(amount > channel.settled, "Amount not increased");
        require(amount <= channel.deposit, "Exceeds deposit");

        // Verify signature
        bytes32 message = getPaymentHash(channelId, amount, nonce);
        address signer = message.toEthSignedMessageHash().recover(signature);
        require(signer == channel.client, "Invalid signature");

        // Calculate claimable amount
        uint256 claimable = amount - channel.settled;
        uint256 fee = (claimable * settlementFee) / 100;
        uint256 payout = claimable - fee;

        // Update state
        channel.settled = amount;
        channel.nonce = nonce;
        channel.lastUpdated = block.timestamp;
        usedNonces[channelId][nonce] = true;

        // Record settlement
        channelSettlements[channelId].push(Settlement({
            channelId: channelId,
            amount: payout,
            nonce: nonce,
            timestamp: block.timestamp,
            txHash: blockhash(block.number - 1)
        }));

        // Transfer payment
        if (address(paymentToken) == address(0)) {
            (bool success, ) = channel.serviceNode.call{value: payout}("");
            require(success, "ETH transfer failed");
        } else {
            require(
                paymentToken.transfer(channel.serviceNode, payout),
                "Transfer failed"
            );
        }

        emit PaymentClaimed(channelId, channel.serviceNode, payout, nonce);
    }

    // ============ Dispute Resolution ============

    /**
     * @dev Raise a dispute for a channel
     * @param channelId Channel identifier
     * @param reason Dispute reason
     */
    function raiseDispute(
        bytes32 channelId,
        string memory reason
    ) external channelExists(channelId) {
        Channel storage channel = channels[channelId];

        require(
            msg.sender == channel.client || msg.sender == channel.serviceNode,
            "Not a channel participant"
        );

        // Extend timeout for dispute period
        channel.timeout = block.timestamp + disputePeriod;

        emit DisputeRaised(channelId, msg.sender, reason);
    }

    /**
     * @dev Emergency withdraw after timeout (for stuck channels)
     * @param channelId Channel identifier
     */
    function emergencyWithdraw(
        bytes32 channelId
    ) external nonReentrant onlyClient(channelId) {
        Channel storage channel = channels[channelId];

        require(channel.active, "Channel not active");
        require(
            block.timestamp > channel.timeout + disputePeriod,
            "Timeout not reached"
        );

        uint256 refund = channel.deposit - channel.settled;
        channel.active = false;

        if (refund > 0) {
            if (address(paymentToken) == address(0)) {
                (bool success, ) = channel.client.call{value: refund}("");
                require(success, "ETH transfer failed");
            } else {
                require(
                    paymentToken.transfer(channel.client, refund),
                    "Transfer failed"
                );
            }
        }

        emit EmergencyWithdraw(channelId, channel.client, refund);
    }

    // ============ View Functions ============

    /**
     * @dev Get payment hash for signature verification
     * @param channelId Channel identifier
     * @param amount Cumulative amount
     * @param nonce Request nonce
     * @return Message hash
     */
    function getPaymentHash(
        bytes32 channelId,
        uint256 amount,
        uint256 nonce
    ) public pure returns (bytes32) {
        return keccak256(abi.encodePacked(channelId, amount, nonce));
    }

    /**
     * @dev Get channel details
     * @param channelId Channel identifier
     * @return Channel struct
     */
    function getChannel(bytes32 channelId) external view returns (Channel memory) {
        return channels[channelId];
    }

    /**
     * @dev Get all channels for a user
     * @param user User address
     * @return Array of channel IDs
     */
    function getUserChannels(address user) external view returns (bytes32[] memory) {
        return userChannels[user];
    }

    /**
     * @dev Get all channels for a service node
     * @param node Service node address
     * @return Array of channel IDs
     */
    function getNodeChannels(address node) external view returns (bytes32[] memory) {
        return nodeChannels[node];
    }

    /**
     * @dev Get service node details
     * @param node Service node address
     * @return ServiceNode struct
     */
    function getServiceNode(address node) external view returns (ServiceNode memory) {
        return serviceNodes[node];
    }

    /**
     * @dev Get all registered nodes
     * @return Array of node addresses
     */
    function getRegisteredNodes() external view returns (address[] memory) {
        return registeredNodes;
    }

    /**
     * @dev Get settlement history for a channel
     * @param channelId Channel identifier
     * @return Array of settlements
     */
    function getChannelSettlements(
        bytes32 channelId
    ) external view returns (Settlement[] memory) {
        return channelSettlements[channelId];
    }

    /**
     * @dev Get available balance in a channel
     * @param channelId Channel identifier
     * @return Available balance
     */
    function getAvailableBalance(bytes32 channelId) external view returns (uint256) {
        Channel storage channel = channels[channelId];
        return channel.deposit - channel.settled;
    }

    // ============ Admin Functions ============

    /**
     * @dev Set minimum channel deposit
     * @param amount New minimum deposit
     */
    function setMinChannelDeposit(uint256 amount) external onlyOwner {
        minChannelDeposit = amount;
    }

    /**
     * @dev Set maximum channel deposit
     * @param amount New maximum deposit
     */
    function setMaxChannelDeposit(uint256 amount) external onlyOwner {
        maxChannelDeposit = amount;
    }

    /**
     * @dev Set channel timeout period
     * @param timeout New timeout in seconds
     */
    function setChannelTimeout(uint256 timeout) external onlyOwner {
        channelTimeout = timeout;
    }

    /**
     * @dev Set dispute period
     * @param period New dispute period in seconds
     */
    function setDisputePeriod(uint256 period) external onlyOwner {
        disputePeriod = period;
    }

    /**
     * @dev Set settlement fee percentage
     * @param fee Fee percentage (0-100)
     */
    function setSettlementFee(uint256 fee) external onlyOwner {
        require(fee <= 100, "Fee too high");
        settlementFee = fee;
    }

    /**
     * @dev Pause contract (emergency)
     */
    function pause() external onlyOwner {
        paused = true;
    }

    /**
     * @dev Unpause contract
     */
    function unpause() external onlyOwner {
        paused = false;
    }

    /**
     * @dev Withdraw accumulated fees (if any)
     */
    function withdrawFees() external onlyOwner {
        uint256 balance;

        if (address(paymentToken) == address(0)) {
            balance = address(this).balance;
            (bool success, ) = owner().call{value: balance}("");
            require(success, "ETH transfer failed");
        } else {
            balance = paymentToken.balanceOf(address(this));
            require(paymentToken.transfer(owner(), balance), "Transfer failed");
        }
    }

    // ============ Fallback ============

    receive() external payable {
        require(address(paymentToken) == address(0), "ETH not accepted");
    }
}
