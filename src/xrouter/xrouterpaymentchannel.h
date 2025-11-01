// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKNET_XROUTER_XROUTERPAYMENTCHANNEL_H
#define BLOCKNET_XROUTER_XROUTERPAYMENTCHANNEL_H

#include <amount.h>
#include <key.h>
#include <pubkey.h>
#include <uint256.h>

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace xrouter
{

/**
 * @brief Channel state enumeration
 */
enum class ChannelState {
    OPEN,        // Channel is active
    CHALLENGED,  // Channel close initiated
    CLOSED,      // Channel finalized
    INVALID      // Channel doesn't exist
};

/**
 * @brief Payment channel state update
 */
struct ChannelStateUpdate {
    uint256 channelId;           // Unique channel identifier
    uint64_t nonce;              // Monotonically increasing counter
    CAmount clientBalance;       // Client's current balance (in satoshis)
    CAmount serviceNodeBalance;  // Service node's current balance (in satoshis)
    std::vector<unsigned char> clientSignature;      // Client's signature
    std::vector<unsigned char> serviceNodeSignature; // Service node's signature
    uint64_t timestamp;          // State update timestamp

    ChannelStateUpdate() :
        channelId(), nonce(0), clientBalance(0), serviceNodeBalance(0),
        clientSignature(), serviceNodeSignature(), timestamp(0) {}

    /**
     * @brief Calculate hash of the state for signing
     */
    uint256 GetHash() const;

    /**
     * @brief Verify signatures on this state
     */
    bool VerifySignatures(const CPubKey& clientPubKey, const CPubKey& serviceNodePubKey) const;

    /**
     * @brief Sign the state update
     */
    bool Sign(const CKey& key, bool isClient);

    /**
     * @brief Serialize for storage or transmission
     */
    std::vector<unsigned char> Serialize() const;

    /**
     * @brief Deserialize from storage or transmission
     */
    bool Deserialize(const std::vector<unsigned char>& data);
};

/**
 * @brief Payment channel structure
 */
struct PaymentChannel {
    uint256 channelId;              // Unique channel identifier
    std::string contractAddress;    // Smart contract address on Ethereum
    std::string clientAddress;      // Client's Ethereum address
    std::string serviceNodeAddress; // Service node's Ethereum address
    CPubKey clientPubKey;           // Client's public key for signing
    CPubKey serviceNodePubKey;      // Service node's public key for signing

    CAmount clientDeposit;          // Client's deposited amount
    CAmount serviceNodeDeposit;     // Service node's deposited amount
    CAmount clientBalance;          // Current client balance
    CAmount serviceNodeBalance;     // Current service node balance

    uint64_t nonce;                 // Current state nonce
    uint64_t challengePeriod;       // Challenge period in seconds
    uint64_t openTime;              // Channel opening timestamp
    uint64_t closingTime;           // Channel closing initiation time

    ChannelState state;             // Current channel state

    ChannelStateUpdate latestState; // Latest signed state

    PaymentChannel() :
        channelId(), contractAddress(""), clientAddress(""), serviceNodeAddress(""),
        clientPubKey(), serviceNodePubKey(), clientDeposit(0), serviceNodeDeposit(0),
        clientBalance(0), serviceNodeBalance(0), nonce(0), challengePeriod(3600),
        openTime(0), closingTime(0), state(ChannelState::INVALID), latestState() {}

    /**
     * @brief Check if channel is open
     */
    bool IsOpen() const { return state == ChannelState::OPEN; }

    /**
     * @brief Check if channel is closed
     */
    bool IsClosed() const { return state == ChannelState::CLOSED; }

    /**
     * @brief Get total channel capacity
     */
    CAmount GetCapacity() const { return clientDeposit + serviceNodeDeposit; }

    /**
     * @brief Get available balance for payments (client perspective)
     */
    CAmount GetAvailableBalance() const { return clientBalance; }
};

/**
 * @brief Payment channel manager for XRouter
 *
 * Manages trustless payment channels with service nodes for off-chain payments
 */
class XRouterPaymentChannelManager
{
public:
    XRouterPaymentChannelManager() {}
    ~XRouterPaymentChannelManager() {}

    /**
     * @brief Initialize the payment channel manager
     */
    bool Init(const std::string& ethereumRpcUrl, const std::string& contractAddress);

    /**
     * @brief Open a new payment channel with a service node
     *
     * @param serviceNodeAddress Service node's Ethereum address
     * @param serviceNodePubKey Service node's public key
     * @param depositAmount Amount to deposit (in satoshis)
     * @param challengePeriod Challenge period in seconds (default 3600)
     * @return Channel ID if successful, null hash otherwise
     */
    uint256 OpenChannel(
        const std::string& serviceNodeAddress,
        const CPubKey& serviceNodePubKey,
        CAmount depositAmount,
        uint64_t challengePeriod = 3600
    );

    /**
     * @brief Create a payment within an existing channel
     *
     * @param channelId Channel identifier
     * @param amount Amount to pay to service node
     * @param outStateUpdate Updated channel state signed by client
     * @return True if payment created successfully
     */
    bool CreatePayment(
        const uint256& channelId,
        CAmount amount,
        ChannelStateUpdate& outStateUpdate
    );

    /**
     * @brief Verify and accept a payment from client
     *
     * @param stateUpdate Signed state update from client
     * @return True if payment is valid and accepted
     */
    bool VerifyPayment(const ChannelStateUpdate& stateUpdate);

    /**
     * @brief Sign a channel state update
     *
     * @param stateUpdate State update to sign
     * @param isClient True if signing as client, false if service node
     * @return True if signing successful
     */
    bool SignState(ChannelStateUpdate& stateUpdate, bool isClient);

    /**
     * @brief Cooperatively close a channel
     *
     * @param channelId Channel to close
     * @param finalState Final state signed by both parties
     * @return True if close initiated successfully
     */
    bool CooperativeClose(
        const uint256& channelId,
        const ChannelStateUpdate& finalState
    );

    /**
     * @brief Unilaterally close a channel (initiates challenge period)
     *
     * @param channelId Channel to close
     * @return True if challenge close initiated
     */
    bool ChallengeClose(const uint256& channelId);

    /**
     * @brief Dispute a channel close with newer state
     *
     * @param channelId Channel being closed
     * @param newerState Newer signed state to dispute with
     * @return True if dispute successful
     */
    bool DisputeClose(
        const uint256& channelId,
        const ChannelStateUpdate& newerState
    );

    /**
     * @brief Finalize channel close after challenge period
     *
     * @param channelId Channel to finalize
     * @return True if finalization successful
     */
    bool FinalizeClose(const uint256& channelId);

    /**
     * @brief Get channel information
     *
     * @param channelId Channel identifier
     * @return Pointer to channel if exists, nullptr otherwise
     */
    std::shared_ptr<PaymentChannel> GetChannel(const uint256& channelId);

    /**
     * @brief Get all channels for a service node
     *
     * @param serviceNodeAddress Service node's address
     * @return Vector of channel IDs
     */
    std::vector<uint256> GetChannelsByServiceNode(const std::string& serviceNodeAddress);

    /**
     * @brief Get all open channels
     *
     * @return Vector of channel IDs
     */
    std::vector<uint256> GetOpenChannels();

    /**
     * @brief Update channel state from blockchain
     *
     * @param channelId Channel to update
     * @return True if update successful
     */
    bool SyncChannelFromBlockchain(const uint256& channelId);

    /**
     * @brief Check if client has sufficient balance in channel
     *
     * @param channelId Channel identifier
     * @param amount Required amount
     * @return True if sufficient balance available
     */
    bool HasSufficientBalance(const uint256& channelId, CAmount amount);

    /**
     * @brief Get total available balance across all channels
     *
     * @return Total available balance
     */
    CAmount GetTotalAvailableBalance();

    /**
     * @brief Add deposit to existing channel
     *
     * @param channelId Channel identifier
     * @param amount Amount to deposit
     * @param isClient True if client deposit, false if service node
     * @return True if deposit successful
     */
    bool AddDeposit(const uint256& channelId, CAmount amount, bool isClient);

private:
    std::string ethereumRpcUrl_;      // Ethereum node RPC URL
    std::string contractAddress_;     // Payment channel contract address
    CKey clientKey_;                  // Client's signing key
    std::string clientEthAddress_;    // Client's Ethereum address

    // Channel storage
    std::map<uint256, std::shared_ptr<PaymentChannel>> channels_;

    /**
     * @brief Load channels from persistent storage
     */
    bool LoadChannels();

    /**
     * @brief Save channel to persistent storage
     */
    bool SaveChannel(const PaymentChannel& channel);

    /**
     * @brief Call Ethereum smart contract method
     */
    bool CallContractMethod(
        const std::string& method,
        const std::vector<std::string>& params,
        std::string& result
    );

    /**
     * @brief Send Ethereum transaction to contract
     */
    bool SendContractTransaction(
        const std::string& method,
        const std::vector<std::string>& params,
        std::string& txHash
    );

    /**
     * @brief Get channel state from blockchain
     */
    bool GetChannelFromBlockchain(const uint256& channelId, PaymentChannel& channel);

    /**
     * @brief Generate new channel ID
     */
    uint256 GenerateChannelId(const std::string& serviceNodeAddress);
};

} // namespace xrouter

#endif // BLOCKNET_XROUTER_XROUTERPAYMENTCHANNEL_H
