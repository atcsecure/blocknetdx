// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_V2_H
#define BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_V2_H

#include <xrouter/xrouterpaymentchannel.h>

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
 * @brief Off-chain node response with signature
 *
 * Nodes send responses via XRouter P2P network (NOT to smart contract)
 * This saves gas - no on-chain transactions per response!
 */
struct OffChainNodeResponse {
    std::string nodeAddress;            // Node's Ethereum address
    std::string nodeXRouterAddr;        // Node's XRouter P2P address
    std::string responseData;           // Actual response (e.g., "750000")
    uint256 responseHash;               // SHA256(responseData)
    std::vector<unsigned char> nodeSignature; // Node signs the hash
    uint64_t timestamp;                 // Response time

    OffChainNodeResponse() : nodeAddress(""), nodeXRouterAddr(""),
        responseData(""), responseHash(), nodeSignature(), timestamp(0) {}

    /**
     * @brief Calculate hash of response data
     */
    uint256 GetHash() const {
        return Hash(responseData.begin(), responseData.end());
    }

    /**
     * @brief Verify node's signature on response hash
     */
    bool VerifySignature(const CPubKey& nodePubKey) const;

    /**
     * @brief Sign response hash
     */
    bool Sign(const CKey& nodeKey);
};

/**
 * @brief Off-chain multi-node request with local consensus validation
 *
 * All consensus calculation happens CLIENT-SIDE (not in smart contract)
 * This is FREE - no gas costs!
 */
struct OffChainMultiNodeRequest {
    uint256 requestId;                  // Unique request ID
    std::string query;                  // Query string (e.g., "xrGetBlockCount BTC")
    CAmount totalFee;                   // Total fee to distribute
    uint64_t timestamp;                 // Request timestamp
    uint64_t quorumPercentage;          // Required consensus (51-100)

    // Collected responses (off-chain, via P2P)
    std::vector<OffChainNodeResponse> responses;

    // Local consensus calculation (client-side)
    std::map<uint256, uint64_t> voteCount;      // responseHash => count
    uint256 consensusHash;                       // Hash with most votes
    uint64_t consensusCount;                     // Number of matching responses
    std::string consensusResponse;               // The actual consensus response

    // Identified nodes
    std::set<std::string> honestNodes;          // Nodes with consensus response
    std::set<std::string> dishonestNodes;       // Nodes with wrong response

    OffChainMultiNodeRequest() : requestId(), query(""), totalFee(0),
        timestamp(0), quorumPercentage(67), responses(), voteCount(),
        consensusHash(), consensusCount(0), consensusResponse(),
        honestNodes(), dishonestNodes() {}

    /**
     * @brief Add response from a node (off-chain)
     */
    bool AddResponse(const OffChainNodeResponse& response);

    /**
     * @brief Calculate consensus locally (client-side, FREE!)
     * @return True if consensus reached
     */
    bool CalculateConsensusLocally();

    /**
     * @brief Check if consensus reached
     */
    bool HasConsensus() const {
        if (responses.empty()) return false;
        uint64_t required = (responses.size() * quorumPercentage) / 100;
        return consensusCount >= required;
    }

    /**
     * @brief Get honest node count
     */
    size_t GetHonestNodeCount() const { return honestNodes.size(); }

    /**
     * @brief Get dishonest node count
     */
    size_t GetDishonestNodeCount() const { return dishonestNodes.size(); }
};

/**
 * @brief Off-chain channel state update
 *
 * This represents the current balances in the channel.
 * Signed by all parties but kept OFF-CHAIN (not sent to contract)
 * until channel close.
 */
struct OffChainChannelState {
    uint256 channelId;                  // Channel ID
    uint64_t nonce;                     // Monotonic nonce
    CAmount clientBalance;              // Client's balance
    std::map<std::string, CAmount> nodeBalances; // Node address => balance

    // Signatures (collected off-chain via P2P)
    std::vector<unsigned char> clientSignature;
    std::map<std::string, std::vector<unsigned char>> nodeSignatures;

    uint64_t timestamp;                 // State timestamp

    OffChainChannelState() : channelId(), nonce(0), clientBalance(0),
        nodeBalances(), clientSignature(), nodeSignatures(), timestamp(0) {}

    /**
     * @brief Get hash for signing
     */
    uint256 GetHash() const;

    /**
     * @brief Sign state (client or node)
     */
    bool SignByClient(const CKey& clientKey);
    bool SignByNode(const CKey& nodeKey, const std::string& nodeAddress);

    /**
     * @brief Verify all signatures
     */
    bool VerifySignatures(
        const CPubKey& clientPubKey,
        const std::map<std::string, CPubKey>& nodePubKeys
    ) const;

    /**
     * @brief Serialize for P2P transmission
     */
    std::vector<unsigned char> Serialize() const;
    bool Deserialize(const std::vector<unsigned char>& data);
};

/**
 * @brief Multi-node channel with OFF-CHAIN consensus
 */
struct OffChainMultiNodeChannel {
    uint256 channelId;                  // Channel ID
    std::string contractAddress;        // Smart contract address
    std::string clientAddress;          // Client's Ethereum address
    std::string clientXRouterAddr;      // Client's XRouter P2P address
    CPubKey clientPubKey;               // Client's public key

    CAmount clientDeposit;              // Initial deposit
    uint64_t quorumPercentage;          // Required consensus (51-100)
    uint64_t challengePeriod;           // Challenge period (seconds)
    uint64_t openTime;                  // Open timestamp
    ChannelState state;                 // Channel state

    // Service nodes
    std::vector<std::string> serviceNodeAddresses;  // Ethereum addresses
    std::map<std::string, CPubKey> nodePubKeys;     // For signature verification
    std::map<std::string, std::string> nodeXRouterAddrs; // For P2P communication

    // Current state (OFF-CHAIN, updated after each query)
    OffChainChannelState currentState;

    // All historical states (for dispute resolution)
    std::vector<OffChainChannelState> stateHistory;

    // Request history (all queries, consensus results, etc.)
    std::map<uint256, OffChainMultiNodeRequest> requests;

    OffChainMultiNodeChannel() : channelId(), contractAddress(""),
        clientAddress(""), clientXRouterAddr(""), clientPubKey(),
        clientDeposit(0), quorumPercentage(67), challengePeriod(3600),
        openTime(0), state(ChannelState::INVALID), serviceNodeAddresses(),
        nodePubKeys(), nodeXRouterAddrs(), currentState(), stateHistory(),
        requests() {}

    /**
     * @brief Check if channel is open
     */
    bool IsOpen() const { return state == ChannelState::OPEN; }

    /**
     * @brief Get number of service nodes
     */
    size_t GetNodeCount() const { return serviceNodeAddresses.size(); }

    /**
     * @brief Get client's available balance
     */
    CAmount GetClientBalance() const { return currentState.clientBalance; }

    /**
     * @brief Get node's earned balance
     */
    CAmount GetNodeBalance(const std::string& nodeAddress) const {
        auto it = currentState.nodeBalances.find(nodeAddress);
        return (it != currentState.nodeBalances.end()) ? it->second : 0;
    }
};

/**
 * @brief Manager for off-chain multi-node channels
 *
 * KEY FEATURES:
 * - Queries sent via XRouter P2P (not to contract) → FREE
 * - Responses collected off-chain → FREE
 * - Consensus calculated locally → FREE
 * - Payment states signed off-chain → FREE
 * - Only settlement goes on-chain → $3 once
 *
 * Result: 1000s of queries for cost of single transaction!
 */
class OffChainMultiNodeChannelManager
{
public:
    OffChainMultiNodeChannelManager() {}
    ~OffChainMultiNodeChannelManager() {}

    /**
     * @brief Initialize manager
     */
    bool Init(const std::string& ethereumRpcUrl, const std::string& contractAddress);

    /**
     * @brief Open multi-node channel
     * @param serviceNodes List of node Ethereum addresses
     * @param serviceNodePubKeys Public keys for signature verification
     * @param quorumPercentage Required consensus (51-100)
     * @param depositAmount Client's deposit
     * @param challengePeriod Challenge period (seconds)
     * @return Channel ID
     */
    uint256 OpenChannel(
        const std::vector<std::string>& serviceNodes,
        const std::map<std::string, CPubKey>& serviceNodePubKeys,
        uint64_t quorumPercentage,
        CAmount depositAmount,
        uint64_t challengePeriod = 3600
    );

    /**
     * @brief Submit query to all nodes and validate consensus
     *
     * This is the MAIN function that does everything OFF-CHAIN:
     * 1. Send query to all N nodes via XRouter P2P
     * 2. Collect responses off-chain
     * 3. Calculate consensus locally
     * 4. Create payment state update (honest nodes only)
     * 5. Get node signatures on state off-chain
     * 6. Update local channel state
     * 7. Return consensus response
     *
     * ALL FREE - NO GAS COSTS!
     *
     * @param channelId Channel identifier
     * @param query Query to send (e.g., "xrGetBlockCount BTC")
     * @param totalFee Fee to distribute among honest nodes
     * @param outRequestId Generated request ID
     * @param outConsensusResponse The consensus response from nodes
     * @return True if consensus reached and payment updated
     */
    bool QueryWithConsensus(
        const uint256& channelId,
        const std::string& query,
        CAmount totalFee,
        uint256& outRequestId,
        std::string& outConsensusResponse
    );

    /**
     * @brief Get channel information
     */
    std::shared_ptr<OffChainMultiNodeChannel> GetChannel(const uint256& channelId);

    /**
     * @brief Get request details
     */
    std::shared_ptr<OffChainMultiNodeRequest> GetRequest(
        const uint256& channelId,
        const uint256& requestId
    );

    /**
     * @brief Close channel cooperatively
     *
     * Submits final state to smart contract with all signatures
     * This is the ONLY on-chain transaction after channel open!
     */
    bool CooperativeClose(const uint256& channelId);

    /**
     * @brief Close channel unilaterally (with challenge period)
     */
    bool ChallengeClose(const uint256& channelId);

    /**
     * @brief Finalize challenged close
     */
    bool FinalizeClose(const uint256& channelId);

    /**
     * @brief Get all open channels
     */
    std::vector<uint256> GetOpenChannels();

private:
    std::string ethereumRpcUrl_;
    std::string contractAddress_;
    CKey clientKey_;
    std::string clientEthAddress_;
    std::string clientXRouterAddress_;

    std::map<uint256, std::shared_ptr<OffChainMultiNodeChannel>> channels_;

    /**
     * @brief Send query to node via XRouter P2P
     */
    bool SendQueryToNode(
        const std::string& nodeXRouterAddr,
        const std::string& query,
        OffChainNodeResponse& outResponse
    );

    /**
     * @brief Send state update to node for signature (via P2P)
     */
    bool SendStateUpdateToNode(
        const std::string& nodeXRouterAddr,
        const OffChainChannelState& state,
        std::vector<unsigned char>& outNodeSignature
    );

    /**
     * @brief Create new channel state after payment
     */
    OffChainChannelState CreatePaymentState(
        const OffChainMultiNodeChannel& channel,
        const OffChainMultiNodeRequest& request
    );

    /**
     * @brief Call smart contract (only for open/close)
     */
    bool CallContract(
        const std::string& method,
        const std::vector<std::string>& params,
        std::string& result
    );

    /**
     * @brief Send transaction to smart contract
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
    bool SaveChannel(const OffChainMultiNodeChannel& channel);
};

} // namespace xrouter

#endif // BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_V2_H
