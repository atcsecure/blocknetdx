// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_H
#define BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_H

#include <amount.h>
#include <key.h>
#include <pubkey.h>
#include <uint256.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set>

namespace xrouter
{

/**
 * @brief Service node information for multi-node channels
 */
struct ServiceNodeInfo {
    std::string nodeAddress;        // Ethereum address
    std::string xrouterAddress;     // XRouter service identifier
    CPubKey pubKey;                 // Public key for signing
    CAmount deposit;                // Node's stake
    CAmount balance;                // Earned balance
    uint64_t requestsServed;        // Total requests served
    uint64_t correctResponses;      // Responses matching consensus
    uint64_t incorrectResponses;    // Responses not matching consensus
    bool active;                    // Is node active

    ServiceNodeInfo() : nodeAddress(""), xrouterAddress(""), pubKey(),
        deposit(0), balance(0), requestsServed(0), correctResponses(0),
        incorrectResponses(0), active(true) {}
};

/**
 * @brief Response from a service node
 */
struct NodeResponse {
    std::string nodeAddress;        // Node's address
    std::string responseData;       // Actual response data
    uint256 responseHash;           // Hash of response
    std::vector<unsigned char> signature; // Node's signature
    uint64_t timestamp;             // Response timestamp

    NodeResponse() : nodeAddress(""), responseData(""), responseHash(),
        signature(), timestamp(0) {}

    /**
     * @brief Calculate hash of response data
     */
    uint256 GetHash() const;

    /**
     * @brief Verify node's signature
     */
    bool VerifySignature(const CPubKey& pubKey) const;
};

/**
 * @brief Request with multiple node responses for consensus
 */
struct MultiNodeRequest {
    uint256 requestId;              // Unique request ID
    std::string query;              // Query string
    uint256 queryHash;              // Hash of query
    CAmount totalFee;               // Total fee for request
    uint64_t timestamp;             // Request timestamp

    std::vector<NodeResponse> responses; // All responses received
    std::map<uint256, uint64_t> responseVotes; // Hash => vote count
    uint256 consensusHash;          // Hash with most votes
    uint64_t consensusCount;        // Number of matching responses
    bool settled;                   // Payment distributed

    std::set<std::string> honestNodes; // Nodes with correct response
    std::set<std::string> dishonestNodes; // Nodes with incorrect response

    MultiNodeRequest() : requestId(), query(""), queryHash(), totalFee(0),
        timestamp(0), responses(), responseVotes(), consensusHash(),
        consensusCount(0), settled(false), honestNodes(), dishonestNodes() {}

    /**
     * @brief Add response from a node
     */
    bool AddResponse(const NodeResponse& response);

    /**
     * @brief Calculate consensus from responses
     */
    bool CalculateConsensus(uint64_t quorumPercentage);

    /**
     * @brief Get the consensus response data
     */
    std::string GetConsensusResponse() const;
};

/**
 * @brief Multi-node payment channel
 */
struct MultiNodeChannel {
    uint256 channelId;              // Unique channel ID
    std::string contractAddress;    // Smart contract address
    std::string clientAddress;      // Client's Ethereum address
    CPubKey clientPubKey;           // Client's public key

    CAmount clientDeposit;          // Client's deposit
    CAmount clientBalance;          // Client's remaining balance
    uint64_t nonce;                 // State nonce
    uint64_t challengePeriod;       // Challenge period (seconds)
    uint64_t openTime;              // Channel open time
    uint64_t closingTime;           // Close initiation time

    uint64_t minNodes;              // Minimum nodes for consensus
    uint64_t quorumPercentage;      // Required consensus percentage (51-100)
    uint64_t totalRequests;         // Total requests made

    std::vector<std::string> serviceNodeAddresses; // Node Ethereum addresses
    std::map<std::string, ServiceNodeInfo> nodes; // Node details

    std::map<uint256, MultiNodeRequest> requests; // All requests

    ChannelState state;             // Current state

    MultiNodeChannel() : channelId(), contractAddress(""), clientAddress(""),
        clientPubKey(), clientDeposit(0), clientBalance(0), nonce(0),
        challengePeriod(3600), openTime(0), closingTime(0), minNodes(2),
        quorumPercentage(67), totalRequests(0), serviceNodeAddresses(),
        nodes(), requests(), state(ChannelState::INVALID) {}

    /**
     * @brief Check if channel is open
     */
    bool IsOpen() const { return state == ChannelState::OPEN; }

    /**
     * @brief Get number of active nodes
     */
    uint64_t GetActiveNodeCount() const;

    /**
     * @brief Check if enough nodes for consensus
     */
    bool HasSufficientNodes() const { return GetActiveNodeCount() >= minNodes; }
};

/**
 * @brief Manager for multi-node payment channels
 */
class XRouterMultiNodeChannelManager
{
public:
    XRouterMultiNodeChannelManager() {}
    ~XRouterMultiNodeChannelManager() {}

    /**
     * @brief Initialize the manager
     */
    bool Init(const std::string& ethereumRpcUrl, const std::string& contractAddress);

    /**
     * @brief Open a new multi-node channel
     *
     * @param serviceNodes List of service node addresses
     * @param minNodes Minimum nodes required for consensus
     * @param quorumPercentage Percentage required for consensus (51-100)
     * @param depositAmount Client's deposit amount
     * @param challengePeriod Challenge period in seconds
     * @return Channel ID if successful
     */
    uint256 OpenMultiNodeChannel(
        const std::vector<std::string>& serviceNodes,
        uint64_t minNodes,
        uint64_t quorumPercentage,
        CAmount depositAmount,
        uint64_t challengePeriod = 3600
    );

    /**
     * @brief Submit request to multiple nodes
     *
     * @param channelId Channel identifier
     * @param query Query to send to nodes
     * @param feePerNode Fee to pay per honest node
     * @param outRequestId Generated request ID
     * @return True if request submitted successfully
     */
    bool SubmitMultiNodeRequest(
        const uint256& channelId,
        const std::string& query,
        CAmount totalFee,
        uint256& outRequestId
    );

    /**
     * @brief Add response from a service node
     *
     * @param channelId Channel identifier
     * @param requestId Request identifier
     * @param response Response from node
     * @return True if response added successfully
     */
    bool AddNodeResponse(
        const uint256& channelId,
        const uint256& requestId,
        const NodeResponse& response
    );

    /**
     * @brief Check if consensus reached for request
     *
     * @param channelId Channel identifier
     * @param requestId Request identifier
     * @return True if consensus reached
     */
    bool IsConsensusReached(
        const uint256& channelId,
        const uint256& requestId
    );

    /**
     * @brief Settle request and distribute payments
     *
     * @param channelId Channel identifier
     * @param requestId Request identifier
     * @return True if settlement successful
     */
    bool SettleRequest(
        const uint256& channelId,
        const uint256& requestId
    );

    /**
     * @brief Get consensus response for a request
     *
     * @param channelId Channel identifier
     * @param requestId Request identifier
     * @return Consensus response data
     */
    std::string GetConsensusResponse(
        const uint256& channelId,
        const uint256& requestId
    );

    /**
     * @brief Get node statistics
     *
     * @param channelId Channel identifier
     * @param nodeAddress Node's address
     * @return Pointer to node info or nullptr
     */
    std::shared_ptr<ServiceNodeInfo> GetNodeStats(
        const uint256& channelId,
        const std::string& nodeAddress
    );

    /**
     * @brief Close multi-node channel
     *
     * @param channelId Channel to close
     * @return True if close initiated
     */
    bool CloseChannel(const uint256& channelId);

    /**
     * @brief Finalize channel close
     *
     * @param channelId Channel to finalize
     * @return True if finalized
     */
    bool FinalizeClose(const uint256& channelId);

    /**
     * @brief Get channel information
     */
    std::shared_ptr<MultiNodeChannel> GetChannel(const uint256& channelId);

    /**
     * @brief Get all open multi-node channels
     */
    std::vector<uint256> GetOpenChannels();

    /**
     * @brief Withdraw node earnings (service node side)
     *
     * @param channelId Channel identifier
     * @param amount Amount to withdraw
     * @return True if withdrawal successful
     */
    bool WithdrawNodeEarnings(const uint256& channelId, CAmount amount);

private:
    std::string ethereumRpcUrl_;
    std::string contractAddress_;
    CKey clientKey_;
    std::string clientEthAddress_;

    std::map<uint256, std::shared_ptr<MultiNodeChannel>> channels_;

    /**
     * @brief Generate unique request ID
     */
    uint256 GenerateRequestId(const uint256& channelId, const std::string& query);

    /**
     * @brief Call smart contract method
     */
    bool CallContractMethod(
        const std::string& method,
        const std::vector<std::string>& params,
        std::string& result
    );

    /**
     * @brief Send smart contract transaction
     */
    bool SendContractTransaction(
        const std::string& method,
        const std::vector<std::string>& params,
        std::string& txHash
    );

    /**
     * @brief Load channels from storage
     */
    bool LoadChannels();

    /**
     * @brief Save channel to storage
     */
    bool SaveChannel(const MultiNodeChannel& channel);
};

} // namespace xrouter

#endif // BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_H
