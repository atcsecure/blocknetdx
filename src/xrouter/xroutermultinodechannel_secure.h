// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_SECURE_H
#define BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_SECURE_H

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
 * @brief Hash commitment from a node
 *
 * Nodes commit to their response with a hash BEFORE revealing the actual data.
 * This prevents:
 * - Client from lying about which nodes agreed
 * - Nodes from changing answers after seeing others
 * - Client from getting data without paying
 */
struct ResponseHashCommitment {
    std::string nodeAddress;            // Node's address
    uint256 responseHash;               // SHA256(response data)
    std::vector<unsigned char> signature; // Node signs the hash
    uint64_t timestamp;                 // Commitment time

    ResponseHashCommitment() : nodeAddress(""), responseHash(),
        signature(), timestamp(0) {}

    /**
     * @brief Verify node's signature on the hash commitment
     */
    bool VerifySignature(const CPubKey& nodePubKey) const;

    /**
     * @brief Create commitment from response data
     */
    static ResponseHashCommitment Create(
        const std::string& nodeAddress,
        const std::string& responseData,
        const CKey& nodeKey
    );

    /**
     * @brief Serialize for P2P broadcast
     */
    std::vector<unsigned char> Serialize() const;
    bool Deserialize(const std::vector<unsigned char>& data);
};

/**
 * @brief Full response revealed after payment secured
 */
struct RevealedResponse {
    std::string nodeAddress;            // Node's address
    std::string responseData;           // Actual response (e.g., "750000")
    uint256 responseHash;               // SHA256(responseData) - must match commitment
    std::vector<unsigned char> signature; // Node signs the full response

    RevealedResponse() : nodeAddress(""), responseData(""),
        responseHash(), signature() {}

    /**
     * @brief Verify response matches commitment
     */
    bool MatchesCommitment(const ResponseHashCommitment& commitment) const;

    /**
     * @brief Calculate hash of response data
     */
    uint256 CalculateHash() const;
};

/**
 * @brief Consensus validation data shared between nodes via P2P
 *
 * All nodes broadcast their hash commitments to each other.
 * This allows each node to independently calculate consensus
 * and verify the client's payment state is fair.
 */
struct P2PConsensusData {
    uint256 requestId;                  // Request identifier
    std::string queryHash;              // Hash of the query

    // Hash commitments from all nodes (including self)
    std::map<std::string, ResponseHashCommitment> commitments;

    // Locally calculated consensus (each node computes independently)
    uint256 consensusHash;              // Hash with most votes
    uint64_t consensusCount;            // Number of votes for consensus
    std::set<std::string> honestNodes;  // Nodes with consensus hash
    std::set<std::string> dishonestNodes; // Nodes with different hash

    P2PConsensusData() : requestId(), queryHash(""), commitments(),
        consensusHash(), consensusCount(0), honestNodes(), dishonestNodes() {}

    /**
     * @brief Add commitment from a node (via P2P)
     */
    bool AddCommitment(const ResponseHashCommitment& commitment);

    /**
     * @brief Calculate consensus independently
     */
    bool CalculateConsensus(uint64_t quorumPercentage);

    /**
     * @brief Check if this node should be paid
     */
    bool ShouldNodeBePaid(const std::string& nodeAddress) const {
        return honestNodes.find(nodeAddress) != honestNodes.end();
    }

    /**
     * @brief Get expected payment for a node
     */
    CAmount GetExpectedPayment(const std::string& nodeAddress, CAmount feePerHonestNode) const {
        return ShouldNodeBePaid(nodeAddress) ? feePerHonestNode : 0;
    }
};

/**
 * @brief Secure off-chain request with hash commitment protocol
 */
struct SecureOffChainRequest {
    uint256 requestId;                  // Unique request ID
    std::string query;                  // Query string
    CAmount totalFee;                   // Total fee to distribute
    uint64_t timestamp;                 // Request timestamp
    uint64_t quorumPercentage;          // Required consensus

    // Phase 1: Hash commitments (from client's perspective)
    std::vector<ResponseHashCommitment> commitments;

    // Phase 2: P2P consensus data (from nodes' perspective)
    P2PConsensusData p2pConsensus;

    // Phase 3: Payment state created by client
    bool paymentStateCreated;
    std::map<std::string, CAmount> proposedPayments; // What client proposes to pay

    // Phase 4: Revealed responses (after payment secured)
    std::map<std::string, RevealedResponse> revealedResponses;

    SecureOffChainRequest() : requestId(), query(""), totalFee(0),
        timestamp(0), quorumPercentage(67), commitments(), p2pConsensus(),
        paymentStateCreated(false), proposedPayments(), revealedResponses() {}

    /**
     * @brief Add hash commitment from node
     */
    bool AddCommitment(const ResponseHashCommitment& commitment);

    /**
     * @brief Calculate consensus from commitments
     */
    bool CalculateConsensus();

    /**
     * @brief Add revealed response (after payment)
     */
    bool AddRevealedResponse(const RevealedResponse& response);

    /**
     * @brief Get consensus response data
     */
    std::string GetConsensusResponse() const;

    /**
     * @brief Check if all honest nodes revealed
     */
    bool AllHonestNodesRevealed() const;
};

/**
 * @brief Secure channel state update with payment verification
 */
struct SecureChannelState {
    uint256 channelId;
    uint64_t nonce;
    CAmount clientBalance;
    std::map<std::string, CAmount> nodeBalances;

    // Associated request (for verification)
    uint256 requestId;
    P2PConsensusData consensusData; // Used by nodes to verify fairness

    // Signatures
    std::vector<unsigned char> clientSignature;
    std::map<std::string, std::vector<unsigned char>> nodeSignatures;

    uint64_t timestamp;

    SecureChannelState() : channelId(), nonce(0), clientBalance(0),
        nodeBalances(), requestId(), consensusData(), clientSignature(),
        nodeSignatures(), timestamp(0) {}

    /**
     * @brief Get hash for signing
     */
    uint256 GetHash() const;

    /**
     * @brief Sign by client
     */
    bool SignByClient(const CKey& clientKey);

    /**
     * @brief Sign by node (WITH VERIFICATION)
     *
     * Node verifies:
     * 1. My hash commitment matches consensus → I should be paid
     * 2. Payment amount matches expected amount
     * 3. All honest nodes are paid fairly
     * 4. No dishonest nodes are paid
     *
     * Only signs if ALL checks pass!
     */
    bool SignByNodeWithVerification(
        const CKey& nodeKey,
        const std::string& nodeAddress,
        CAmount totalFee
    );

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
 * @brief Secure multi-node channel with anti-lying protection
 */
struct SecureMultiNodeChannel {
    uint256 channelId;
    std::string contractAddress;
    std::string clientAddress;
    CPubKey clientPubKey;

    CAmount clientDeposit;
    uint64_t quorumPercentage;
    uint64_t challengePeriod;
    uint64_t openTime;
    ChannelState state;

    // Service nodes
    std::vector<std::string> serviceNodeAddresses;
    std::map<std::string, CPubKey> nodePubKeys;
    std::map<std::string, std::string> nodeXRouterAddrs; // For P2P

    // Current state
    SecureChannelState currentState;

    // Historical states
    std::vector<SecureChannelState> stateHistory;

    // Request history
    std::map<uint256, SecureOffChainRequest> requests;

    SecureMultiNodeChannel() : channelId(), contractAddress(""),
        clientAddress(""), clientPubKey(), clientDeposit(0),
        quorumPercentage(67), challengePeriod(3600), openTime(0),
        state(ChannelState::INVALID), serviceNodeAddresses(),
        nodePubKeys(), nodeXRouterAddrs(), currentState(),
        stateHistory(), requests() {}

    bool IsOpen() const { return state == ChannelState::OPEN; }
    size_t GetNodeCount() const { return serviceNodeAddresses.size(); }
    CAmount GetClientBalance() const { return currentState.clientBalance; }
    CAmount GetNodeBalance(const std::string& nodeAddress) const;
};

/**
 * @brief Secure multi-node channel manager with hash commitment protocol
 *
 * SECURITY FEATURES:
 * 1. Hash commitments prevent client lying
 * 2. P2P hash exchange enables independent consensus verification
 * 3. Nodes verify payment fairness before signing
 * 4. Two-phase reveal prevents free data access
 * 5. All parties have cryptographic proof
 */
class SecureMultiNodeChannelManager
{
public:
    SecureMultiNodeChannelManager() {}
    ~SecureMultiNodeChannelManager() {}

    /**
     * @brief Initialize manager
     */
    bool Init(const std::string& ethereumRpcUrl, const std::string& contractAddress);

    /**
     * @brief Open channel
     */
    uint256 OpenChannel(
        const std::vector<std::string>& serviceNodes,
        const std::map<std::string, CPubKey>& serviceNodePubKeys,
        const std::map<std::string, std::string>& nodeXRouterAddrs,
        uint64_t quorumPercentage,
        CAmount depositAmount,
        uint64_t challengePeriod = 3600
    );

    /**
     * @brief Execute secure query with hash commitment protocol
     *
     * PROTOCOL:
     * 1. Send query to all nodes via P2P
     * 2. Collect hash commitments from nodes
     * 3. Wait for nodes to exchange hashes via P2P
     * 4. Calculate consensus from hashes
     * 5. Create payment state for honest nodes
     * 6. Send state to nodes for verification
     * 7. Nodes independently verify consensus and payment
     * 8. Nodes sign if fair, refuse if unfair
     * 9. Only reveal full responses after payment secured
     * 10. Return consensus response
     *
     * Client CANNOT lie because:
     * - Nodes saw each other's hash commitments
     * - Nodes independently calculated consensus
     * - Nodes verify payment matches their expectation
     * - Nodes refuse to sign unfair states
     * - Nodes only reveal data after fair payment
     */
    bool SecureQueryWithConsensus(
        const uint256& channelId,
        const std::string& query,
        CAmount totalFee,
        uint256& outRequestId,
        std::string& outConsensusResponse
    );

    /**
     * @brief Node-side: Process query and create hash commitment
     *
     * Called by service nodes when they receive a query.
     */
    ResponseHashCommitment NodeCreateCommitment(
        const uint256& requestId,
        const std::string& query,
        const std::string& nodeAddress,
        const CKey& nodeKey
    );

    /**
     * @brief Node-side: Broadcast hash commitment to other nodes
     */
    bool NodeBroadcastCommitment(
        const uint256& requestId,
        const ResponseHashCommitment& commitment,
        const std::vector<std::string>& otherNodes
    );

    /**
     * @brief Node-side: Receive hash commitment from another node
     */
    bool NodeReceiveCommitment(
        const uint256& requestId,
        const ResponseHashCommitment& commitment
    );

    /**
     * @brief Node-side: Verify payment state and sign if fair
     *
     * CRITICAL FUNCTION - Prevents client lying!
     *
     * Node checks:
     * 1. Do I have all commitments from other nodes? ✓
     * 2. What is the independent consensus? (calculate locally)
     * 3. Am I in the consensus group? (should I be paid?)
     * 4. Does the payment state match my expectation? ✓
     * 5. Are all honest nodes paid fairly? ✓
     * 6. Are dishonest nodes NOT paid? ✓
     *
     * Signs ONLY if all checks pass!
     */
    bool NodeVerifyAndSignPaymentState(
        const uint256& channelId,
        const SecureChannelState& proposedState,
        const CKey& nodeKey,
        const std::string& nodeAddress,
        std::vector<unsigned char>& outSignature
    );

    /**
     * @brief Node-side: Reveal full response after payment secured
     */
    RevealedResponse NodeRevealResponse(
        const uint256& requestId,
        const std::string& responseData,
        const CKey& nodeKey,
        const std::string& nodeAddress
    );

    /**
     * @brief Get channel
     */
    std::shared_ptr<SecureMultiNodeChannel> GetChannel(const uint256& channelId);

    /**
     * @brief Close channel
     */
    bool CooperativeClose(const uint256& channelId);
    bool ChallengeClose(const uint256& channelId);
    bool FinalizeClose(const uint256& channelId);

    /**
     * @brief Get open channels
     */
    std::vector<uint256> GetOpenChannels();

private:
    std::string ethereumRpcUrl_;
    std::string contractAddress_;
    CKey clientKey_;
    std::string clientEthAddress_;

    std::map<uint256, std::shared_ptr<SecureMultiNodeChannel>> channels_;

    // P2P communication
    bool SendHashCommitmentToNode(
        const std::string& nodeXRouterAddr,
        const ResponseHashCommitment& commitment
    );

    bool SendPaymentStateToNode(
        const std::string& nodeXRouterAddr,
        const SecureChannelState& state,
        std::vector<unsigned char>& outSignature
    );

    bool RequestRevealFromNode(
        const std::string& nodeXRouterAddr,
        const uint256& requestId,
        RevealedResponse& outResponse
    );

    // Contract interaction
    bool CallContract(const std::string& method, const std::vector<std::string>& params, std::string& result);
    bool SendContractTransaction(const std::string& method, const std::vector<std::string>& params, std::string& txHash);

    // Storage
    bool LoadChannels();
    bool SaveChannel(const SecureMultiNodeChannel& channel);
};

} // namespace xrouter

#endif // BLOCKNET_XROUTER_XROUTERMULTINODECHANNEL_SECURE_H
