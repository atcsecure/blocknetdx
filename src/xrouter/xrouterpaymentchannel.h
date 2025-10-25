// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKNET_XROUTER_XROUTERPAYMENTCHANNEL_H
#define BLOCKNET_XROUTER_XROUTERPAYMENTCHANNEL_H

#include <xrouter/xrouterutils.h>

#include <amount.h>
#include <key.h>
#include <sync.h>
#include <uint256.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xrouter
{

//*****************************************************************************
//*****************************************************************************

/**
 * @brief Payment voucher structure for off-chain payments
 */
struct PaymentVoucher
{
    std::string channelId;                          // Channel identifier (32 bytes hex)
    uint64_t nonce;                                 // Request sequence number
    uint64_t cumulativeAmount;                      // Total amount in satoshis
    uint64_t timestamp;                             // Voucher creation time
    std::vector<unsigned char> signature;           // Client signature

    PaymentVoucher()
        : nonce(0), cumulativeAmount(0), timestamp(0) {}

    PaymentVoucher(const std::string& id, uint64_t n, uint64_t amt, uint64_t ts)
        : channelId(id), nonce(n), cumulativeAmount(amt), timestamp(ts) {}

    // Serialization
    std::vector<unsigned char> serialize() const;
    bool deserialize(const std::vector<unsigned char>& data);
    std::string toJson() const;
    bool fromJson(const std::string& json);

    // Get message hash for signing
    uint256 getMessageHash() const;
};

/**
 * @brief Payment receipt from service node
 */
struct PaymentReceipt
{
    std::string channelId;
    uint64_t nonce;
    uint64_t receivedAmount;
    bool accepted;
    std::string error;
    std::vector<unsigned char> nodeSignature;

    PaymentReceipt()
        : nonce(0), receivedAmount(0), accepted(false) {}

    // Serialization
    std::vector<unsigned char> serialize() const;
    bool deserialize(const std::vector<unsigned char>& data);
    std::string toJson() const;
};

/**
 * @brief Channel state information
 */
enum class ChannelState {
    PENDING,        // Opening transaction pending
    ACTIVE,         // Channel is active and ready
    CLOSING,        // Close initiated
    CLOSED,         // Channel closed
    DISPUTED        // Dispute raised
};

/**
 * @brief Channel information
 */
struct ChannelInfo
{
    std::string channelId;
    NodeAddr serviceNode;
    CAmount deposit;
    CAmount spent;
    uint64_t nonce;
    std::chrono::system_clock::time_point lastUsed;
    std::chrono::system_clock::time_point created;
    ChannelState state;
    std::string ethTxId;                            // Ethereum transaction ID for channel opening
    std::vector<unsigned char> privateKey;          // Channel-specific private key
    std::vector<unsigned char> publicKey;           // Channel-specific public key

    ChannelInfo()
        : deposit(0), spent(0), nonce(0), state(ChannelState::PENDING) {}

    bool isActive() const { return state == ChannelState::ACTIVE; }
    CAmount getBalance() const { return deposit - spent; }
    double getBalanceAsDouble() const {
        return static_cast<double>(getBalance()) / COIN;
    }
};

/**
 * @brief Client-side payment channel manager
 *
 * Manages payment channels for XRouter clients including:
 * - Opening and closing channels
 * - Generating payment vouchers
 * - Tracking channel state
 * - Processing payment receipts
 */
class PaymentChannelManager
{
public:
    PaymentChannelManager();
    ~PaymentChannelManager();

    /**
     * @brief Initialize the payment channel manager
     * @param dataDir Data directory for channel database
     * @return true on success
     */
    bool init(const std::string& dataDir);

    /**
     * @brief Shutdown the payment channel manager
     */
    void shutdown();

    // ===== Channel Management =====

    /**
     * @brief Open a new payment channel
     * @param node Service node address
     * @param deposit Deposit amount in satoshis
     * @param channelId Output: Created channel ID
     * @return true on success, false on error
     */
    bool openChannel(const NodeAddr& node, const CAmount& deposit, std::string& channelId);

    /**
     * @brief Close a payment channel
     * @param channelId Channel identifier
     * @return true on success
     */
    bool closeChannel(const std::string& channelId);

    /**
     * @brief Add deposit to existing channel
     * @param channelId Channel identifier
     * @param amount Amount to add
     * @return true on success
     */
    bool addDeposit(const std::string& channelId, const CAmount& amount);

    /**
     * @brief Get or create channel for a service node
     * @param node Service node address
     * @param channelId Output: Channel ID
     * @return true if channel exists or was created
     */
    bool getOrCreateChannel(const NodeAddr& node, std::string& channelId);

    /**
     * @brief Get channel for a service node
     * @param node Service node address
     * @param channelId Output: Channel ID
     * @return true if channel exists
     */
    bool getChannelForNode(const NodeAddr& node, std::string& channelId);

    // ===== Payment Operations =====

    /**
     * @brief Generate payment voucher for a request
     * @param channelId Channel identifier
     * @param fee Fee amount in satoshis
     * @param voucher Output: Generated voucher
     * @return true on success
     */
    bool generateVoucher(const std::string& channelId, const CAmount& fee,
                         PaymentVoucher& voucher);

    /**
     * @brief Process payment receipt from service node
     * @param receipt Payment receipt
     * @return true if receipt is valid
     */
    bool processReceipt(const PaymentReceipt& receipt);

    // ===== Channel Queries =====

    /**
     * @brief Get channel balance
     * @param channelId Channel identifier
     * @return Available balance in satoshis, -1 on error
     */
    CAmount getChannelBalance(const std::string& channelId);

    /**
     * @brief Get channel state
     * @param channelId Channel identifier
     * @return Channel state
     */
    ChannelState getChannelState(const std::string& channelId);

    /**
     * @brief Get channel information
     * @param channelId Channel identifier
     * @param info Output: Channel info
     * @return true if channel exists
     */
    bool getChannelInfo(const std::string& channelId, ChannelInfo& info);

    /**
     * @brief Get all active channels
     * @return Vector of channel IDs
     */
    std::vector<std::string> getActiveChannels();

    /**
     * @brief Get all channels for a specific node
     * @param node Service node address
     * @return Vector of channel IDs
     */
    std::vector<std::string> getNodeChannels(const NodeAddr& node);

    /**
     * @brief Check if channel exists and is active
     * @param channelId Channel identifier
     * @return true if exists and active
     */
    bool hasActiveChannel(const std::string& channelId);

    // ===== Statistics =====

    /**
     * @brief Get total number of channels
     * @return Channel count
     */
    size_t getChannelCount() const;

    /**
     * @brief Get total deposited amount across all channels
     * @return Total deposits
     */
    CAmount getTotalDeposits() const;

    /**
     * @brief Get total spent amount across all channels
     * @return Total spent
     */
    CAmount getTotalSpent() const;

    /**
     * @brief Get channel statistics as JSON
     * @return JSON string with statistics
     */
    std::string getStatistics() const;

private:
    // ===== Internal Methods =====

    /**
     * @brief Generate unique channel ID
     * @param node Service node address
     * @return Channel ID
     */
    std::string generateChannelId(const NodeAddr& node);

    /**
     * @brief Generate channel key pair
     * @param privKey Output: Private key
     * @param pubKey Output: Public key
     * @return true on success
     */
    bool generateChannelKeys(std::vector<unsigned char>& privKey,
                             std::vector<unsigned char>& pubKey);

    /**
     * @brief Sign payment voucher
     * @param voucher Voucher to sign
     * @param privKey Private key
     * @return true on success
     */
    bool signVoucher(PaymentVoucher& voucher, const std::vector<unsigned char>& privKey);

    /**
     * @brief Verify payment receipt signature
     * @param receipt Receipt to verify
     * @param nodePubKey Node's public key
     * @return true if valid
     */
    bool verifyReceipt(const PaymentReceipt& receipt, const std::vector<unsigned char>& nodePubKey);

    /**
     * @brief Update channel state in database
     * @param info Channel info
     * @return true on success
     */
    bool updateChannelState(const ChannelInfo& info);

    /**
     * @brief Load channel from database
     * @param channelId Channel identifier
     * @param info Output: Channel info
     * @return true if found
     */
    bool loadChannel(const std::string& channelId, ChannelInfo& info);

    /**
     * @brief Save channel to database
     * @param info Channel info
     * @return true on success
     */
    bool saveChannel(const ChannelInfo& info);

private:
    mutable Mutex mu;
    std::map<std::string, ChannelInfo> channels;        // channelId -> ChannelInfo
    std::map<NodeAddr, std::string> nodeChannels;       // node -> channelId
    std::string dataDir;
    bool initialized;
};

typedef std::shared_ptr<PaymentChannelManager> PaymentChannelManagerPtr;

} // namespace xrouter

#endif // BLOCKNET_XROUTER_XROUTERPAYMENTCHANNEL_H
