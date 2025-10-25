// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xrouterpaymentchannel.h>

#include <xrouter/xrouterlogger.h>

#include <hash.h>
#include <key.h>
#include <random.h>
#include <util/strencodings.h>

#include <json/json_spirit_reader_template.h>
#include <json/json_spirit_writer_template.h>

using namespace json_spirit;

namespace xrouter
{

//*****************************************************************************
// PaymentVoucher Implementation
//*****************************************************************************

std::vector<unsigned char> PaymentVoucher::serialize() const
{
    std::vector<unsigned char> result;
    result.reserve(32 + 8 + 8 + 8 + 1 + signature.size());

    // Channel ID (32 bytes)
    std::vector<unsigned char> idBytes = ParseHex(channelId);
    if (idBytes.size() != 32)
        idBytes.resize(32, 0);
    result.insert(result.end(), idBytes.begin(), idBytes.end());

    // Nonce (8 bytes)
    for (int i = 0; i < 8; i++)
        result.push_back((nonce >> (i * 8)) & 0xFF);

    // Cumulative amount (8 bytes)
    for (int i = 0; i < 8; i++)
        result.push_back((cumulativeAmount >> (i * 8)) & 0xFF);

    // Timestamp (8 bytes)
    for (int i = 0; i < 8; i++)
        result.push_back((timestamp >> (i * 8)) & 0xFF);

    // Signature length (1 byte)
    result.push_back(static_cast<unsigned char>(signature.size()));

    // Signature
    result.insert(result.end(), signature.begin(), signature.end());

    return result;
}

bool PaymentVoucher::deserialize(const std::vector<unsigned char>& data)
{
    if (data.size() < 32 + 8 + 8 + 8 + 1)
        return false;

    size_t offset = 0;

    // Channel ID
    channelId = HexStr(data.begin() + offset, data.begin() + offset + 32);
    offset += 32;

    // Nonce
    nonce = 0;
    for (int i = 0; i < 8; i++)
        nonce |= (static_cast<uint64_t>(data[offset++]) << (i * 8));

    // Cumulative amount
    cumulativeAmount = 0;
    for (int i = 0; i < 8; i++)
        cumulativeAmount |= (static_cast<uint64_t>(data[offset++]) << (i * 8));

    // Timestamp
    timestamp = 0;
    for (int i = 0; i < 8; i++)
        timestamp |= (static_cast<uint64_t>(data[offset++]) << (i * 8));

    // Signature length
    uint8_t sigLen = data[offset++];
    if (offset + sigLen > data.size())
        return false;

    // Signature
    signature.assign(data.begin() + offset, data.begin() + offset + sigLen);

    return true;
}

std::string PaymentVoucher::toJson() const
{
    Object obj;
    obj.push_back(Pair("channelId", channelId));
    obj.push_back(Pair("nonce", static_cast<uint64_t>(nonce)));
    obj.push_back(Pair("cumulativeAmount", static_cast<uint64_t>(cumulativeAmount)));
    obj.push_back(Pair("timestamp", static_cast<uint64_t>(timestamp)));
    obj.push_back(Pair("signature", HexStr(signature)));

    return write_string(Value(obj), false);
}

bool PaymentVoucher::fromJson(const std::string& json)
{
    try {
        Value val;
        if (!read_string(json, val) || val.type() != obj_type)
            return false;

        Object obj = val.get_obj();

        channelId = find_value(obj, "channelId").get_str();
        nonce = find_value(obj, "nonce").get_uint64();
        cumulativeAmount = find_value(obj, "cumulativeAmount").get_uint64();
        timestamp = find_value(obj, "timestamp").get_uint64();

        std::string sigHex = find_value(obj, "signature").get_str();
        signature = ParseHex(sigHex);

        return true;
    } catch (...) {
        return false;
    }
}

uint256 PaymentVoucher::getMessageHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << channelId;
    ss << nonce;
    ss << cumulativeAmount;
    ss << timestamp;
    return ss.GetHash();
}

//*****************************************************************************
// PaymentReceipt Implementation
//*****************************************************************************

std::vector<unsigned char> PaymentReceipt::serialize() const
{
    std::vector<unsigned char> result;

    // Channel ID (32 bytes)
    std::vector<unsigned char> idBytes = ParseHex(channelId);
    if (idBytes.size() != 32)
        idBytes.resize(32, 0);
    result.insert(result.end(), idBytes.begin(), idBytes.end());

    // Nonce (8 bytes)
    for (int i = 0; i < 8; i++)
        result.push_back((nonce >> (i * 8)) & 0xFF);

    // Received amount (8 bytes)
    for (int i = 0; i < 8; i++)
        result.push_back((receivedAmount >> (i * 8)) & 0xFF);

    // Accepted flag (1 byte)
    result.push_back(accepted ? 1 : 0);

    // Error message length (2 bytes)
    uint16_t errorLen = error.size();
    result.push_back((errorLen >> 0) & 0xFF);
    result.push_back((errorLen >> 8) & 0xFF);

    // Error message
    result.insert(result.end(), error.begin(), error.end());

    // Signature length (1 byte)
    result.push_back(static_cast<unsigned char>(nodeSignature.size()));

    // Signature
    result.insert(result.end(), nodeSignature.begin(), nodeSignature.end());

    return result;
}

bool PaymentReceipt::deserialize(const std::vector<unsigned char>& data)
{
    if (data.size() < 32 + 8 + 8 + 1 + 2 + 1)
        return false;

    size_t offset = 0;

    // Channel ID
    channelId = HexStr(data.begin() + offset, data.begin() + offset + 32);
    offset += 32;

    // Nonce
    nonce = 0;
    for (int i = 0; i < 8; i++)
        nonce |= (static_cast<uint64_t>(data[offset++]) << (i * 8));

    // Received amount
    receivedAmount = 0;
    for (int i = 0; i < 8; i++)
        receivedAmount |= (static_cast<uint64_t>(data[offset++]) << (i * 8));

    // Accepted flag
    accepted = data[offset++] != 0;

    // Error message length
    uint16_t errorLen = data[offset] | (data[offset+1] << 8);
    offset += 2;

    if (offset + errorLen > data.size())
        return false;

    // Error message
    if (errorLen > 0) {
        error.assign(data.begin() + offset, data.begin() + offset + errorLen);
        offset += errorLen;
    }

    // Signature length
    if (offset >= data.size())
        return false;
    uint8_t sigLen = data[offset++];

    if (offset + sigLen > data.size())
        return false;

    // Signature
    nodeSignature.assign(data.begin() + offset, data.begin() + offset + sigLen);

    return true;
}

std::string PaymentReceipt::toJson() const
{
    Object obj;
    obj.push_back(Pair("channelId", channelId));
    obj.push_back(Pair("nonce", static_cast<uint64_t>(nonce)));
    obj.push_back(Pair("receivedAmount", static_cast<uint64_t>(receivedAmount)));
    obj.push_back(Pair("accepted", accepted));
    obj.push_back(Pair("error", error));
    obj.push_back(Pair("nodeSignature", HexStr(nodeSignature)));

    return write_string(Value(obj), false);
}

//*****************************************************************************
// PaymentChannelManager Implementation
//*****************************************************************************

PaymentChannelManager::PaymentChannelManager()
    : initialized(false)
{
}

PaymentChannelManager::~PaymentChannelManager()
{
    shutdown();
}

bool PaymentChannelManager::init(const std::string& dir)
{
    LOCK(mu);

    dataDir = dir;
    initialized = true;

    LOG() << "Payment channel manager initialized with data dir: " << dataDir;

    // TODO: Load existing channels from database
    // For now we use in-memory storage only

    return true;
}

void PaymentChannelManager::shutdown()
{
    LOCK(mu);

    if (!initialized)
        return;

    LOG() << "Shutting down payment channel manager";

    // TODO: Save channels to database

    channels.clear();
    nodeChannels.clear();
    initialized = false;
}

bool PaymentChannelManager::openChannel(const NodeAddr& node, const CAmount& deposit,
                                        std::string& channelId)
{
    LOCK(mu);

    if (!initialized) {
        ERR() << "Payment channel manager not initialized";
        return false;
    }

    // Check if channel already exists for this node
    if (nodeChannels.count(node)) {
        channelId = nodeChannels[node];
        LOG() << "Channel already exists for node " << node << ": " << channelId;
        return false;
    }

    // Generate channel ID
    channelId = generateChannelId(node);

    // Generate channel keys
    std::vector<unsigned char> privKey, pubKey;
    if (!generateChannelKeys(privKey, pubKey)) {
        ERR() << "Failed to generate channel keys";
        return false;
    }

    // Create channel info
    ChannelInfo info;
    info.channelId = channelId;
    info.serviceNode = node;
    info.deposit = deposit;
    info.spent = 0;
    info.nonce = 0;
    info.state = ChannelState::PENDING;
    info.created = std::chrono::system_clock::now();
    info.lastUsed = info.created;
    info.privateKey = privKey;
    info.publicKey = pubKey;

    // Store channel
    channels[channelId] = info;
    nodeChannels[node] = channelId;

    // TODO: Create Ethereum transaction to open channel on-chain

    LOG() << "Opened payment channel " << channelId << " for node " << node
          << " with deposit " << FormatMoney(deposit);

    return true;
}

bool PaymentChannelManager::closeChannel(const std::string& channelId)
{
    LOCK(mu);

    if (!channels.count(channelId)) {
        ERR() << "Channel not found: " << channelId;
        return false;
    }

    ChannelInfo& info = channels[channelId];

    // Update state
    info.state = ChannelState::CLOSING;

    // TODO: Create Ethereum transaction to close channel on-chain

    LOG() << "Closing payment channel " << channelId;

    // Remove from lookup maps (will be removed completely after on-chain confirmation)
    nodeChannels.erase(info.serviceNode);

    return true;
}

bool PaymentChannelManager::addDeposit(const std::string& channelId, const CAmount& amount)
{
    LOCK(mu);

    if (!channels.count(channelId)) {
        ERR() << "Channel not found: " << channelId;
        return false;
    }

    ChannelInfo& info = channels[channelId];

    if (info.state != ChannelState::ACTIVE) {
        ERR() << "Channel not active: " << channelId;
        return false;
    }

    // TODO: Create Ethereum transaction to add deposit on-chain

    info.deposit += amount;

    LOG() << "Added deposit " << FormatMoney(amount) << " to channel " << channelId
          << ", new total: " << FormatMoney(info.deposit);

    return true;
}

bool PaymentChannelManager::getOrCreateChannel(const NodeAddr& node, std::string& channelId)
{
    LOCK(mu);

    // Check if channel exists
    if (nodeChannels.count(node)) {
        channelId = nodeChannels[node];
        return true;
    }

    // Create new channel with default deposit
    // TODO: Get default deposit from config
    CAmount defaultDeposit = 100 * COIN;

    return openChannel(node, defaultDeposit, channelId);
}

bool PaymentChannelManager::getChannelForNode(const NodeAddr& node, std::string& channelId)
{
    LOCK(mu);

    if (!nodeChannels.count(node))
        return false;

    channelId = nodeChannels[node];
    return true;
}

bool PaymentChannelManager::generateVoucher(const std::string& channelId, const CAmount& fee,
                                            PaymentVoucher& voucher)
{
    LOCK(mu);

    if (!channels.count(channelId)) {
        ERR() << "Channel not found: " << channelId;
        return false;
    }

    ChannelInfo& info = channels[channelId];

    if (info.state != ChannelState::ACTIVE) {
        ERR() << "Channel not active: " << channelId;
        return false;
    }

    // Check sufficient balance
    CAmount newSpent = info.spent + fee;
    if (newSpent > info.deposit) {
        ERR() << "Insufficient channel balance. Deposit: " << FormatMoney(info.deposit)
              << ", Spent: " << FormatMoney(info.spent)
              << ", Fee: " << FormatMoney(fee);
        return false;
    }

    // Increment nonce
    uint64_t newNonce = info.nonce + 1;

    // Create voucher
    voucher.channelId = channelId;
    voucher.nonce = newNonce;
    voucher.cumulativeAmount = newSpent;
    voucher.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    // Sign voucher
    if (!signVoucher(voucher, info.privateKey)) {
        ERR() << "Failed to sign voucher";
        return false;
    }

    // Update channel state
    info.nonce = newNonce;
    info.spent = newSpent;
    info.lastUsed = std::chrono::system_clock::now();

    LOG() << "Generated voucher for channel " << channelId
          << ", nonce: " << newNonce
          << ", amount: " << FormatMoney(newSpent);

    return true;
}

bool PaymentChannelManager::processReceipt(const PaymentReceipt& receipt)
{
    LOCK(mu);

    if (!channels.count(receipt.channelId)) {
        ERR() << "Channel not found: " << receipt.channelId;
        return false;
    }

    ChannelInfo& info = channels[receipt.channelId];

    if (!receipt.accepted) {
        ERR() << "Payment rejected by node: " << receipt.error;
        // Revert the spend
        if (info.nonce > 0) {
            info.nonce--;
            // Note: We can't easily revert the spent amount without tracking previous values
            // This is a simplification - in production we'd need better state management
        }
        return false;
    }

    // TODO: Verify receipt signature from service node

    LOG() << "Payment receipt accepted for channel " << receipt.channelId
          << ", nonce: " << receipt.nonce
          << ", amount: " << FormatMoney(receipt.receivedAmount);

    return true;
}

CAmount PaymentChannelManager::getChannelBalance(const std::string& channelId)
{
    LOCK(mu);

    if (!channels.count(channelId))
        return -1;

    const ChannelInfo& info = channels[channelId];
    return info.getBalance();
}

ChannelState PaymentChannelManager::getChannelState(const std::string& channelId)
{
    LOCK(mu);

    if (!channels.count(channelId))
        return ChannelState::CLOSED;

    return channels[channelId].state;
}

bool PaymentChannelManager::getChannelInfo(const std::string& channelId, ChannelInfo& info)
{
    LOCK(mu);

    if (!channels.count(channelId))
        return false;

    info = channels[channelId];
    return true;
}

std::vector<std::string> PaymentChannelManager::getActiveChannels()
{
    LOCK(mu);

    std::vector<std::string> result;
    for (const auto& pair : channels) {
        if (pair.second.state == ChannelState::ACTIVE)
            result.push_back(pair.first);
    }
    return result;
}

std::vector<std::string> PaymentChannelManager::getNodeChannels(const NodeAddr& node)
{
    LOCK(mu);

    std::vector<std::string> result;
    if (nodeChannels.count(node))
        result.push_back(nodeChannels[node]);
    return result;
}

bool PaymentChannelManager::hasActiveChannel(const std::string& channelId)
{
    LOCK(mu);

    if (!channels.count(channelId))
        return false;

    return channels[channelId].state == ChannelState::ACTIVE;
}

size_t PaymentChannelManager::getChannelCount() const
{
    LOCK(mu);
    return channels.size();
}

CAmount PaymentChannelManager::getTotalDeposits() const
{
    LOCK(mu);

    CAmount total = 0;
    for (const auto& pair : channels)
        total += pair.second.deposit;
    return total;
}

CAmount PaymentChannelManager::getTotalSpent() const
{
    LOCK(mu);

    CAmount total = 0;
    for (const auto& pair : channels)
        total += pair.second.spent;
    return total;
}

std::string PaymentChannelManager::getStatistics() const
{
    LOCK(mu);

    Object stats;
    stats.push_back(Pair("channelCount", static_cast<uint64_t>(channels.size())));
    stats.push_back(Pair("totalDeposits", FormatMoney(getTotalDeposits())));
    stats.push_back(Pair("totalSpent", FormatMoney(getTotalSpent())));

    int activeCount = 0;
    for (const auto& pair : channels) {
        if (pair.second.state == ChannelState::ACTIVE)
            activeCount++;
    }
    stats.push_back(Pair("activeChannels", activeCount));

    return write_string(Value(stats), false);
}

//*****************************************************************************
// Private Methods
//*****************************************************************************

std::string PaymentChannelManager::generateChannelId(const NodeAddr& node)
{
    // Generate deterministic but unique channel ID
    CHashWriter ss(SER_GETHASH, 0);
    ss << node;
    ss << GetRandHash();
    ss << std::chrono::system_clock::now().time_since_epoch().count();

    return ss.GetHash().GetHex();
}

bool PaymentChannelManager::generateChannelKeys(std::vector<unsigned char>& privKey,
                                                 std::vector<unsigned char>& pubKey)
{
    try {
        CKey key;
        key.MakeNewKey(true);
        CPubKey pubkey = key.GetPubKey();

        privKey = ToByteVector(key);
        pubKey = ToByteVector(pubkey);

        return true;
    } catch (const std::exception& e) {
        ERR() << "Failed to generate channel keys: " << e.what();
        return false;
    }
}

bool PaymentChannelManager::signVoucher(PaymentVoucher& voucher,
                                        const std::vector<unsigned char>& privKey)
{
    try {
        // Get message hash
        uint256 hash = voucher.getMessageHash();

        // Create key from private key bytes
        CKey key;
        key.Set(privKey.begin(), privKey.end(), true);

        // Sign hash
        std::vector<unsigned char> sig;
        if (!key.Sign(hash, sig)) {
            ERR() << "Failed to sign voucher";
            return false;
        }

        voucher.signature = sig;
        return true;
    } catch (const std::exception& e) {
        ERR() << "Exception signing voucher: " << e.what();
        return false;
    }
}

bool PaymentChannelManager::verifyReceipt(const PaymentReceipt& receipt,
                                          const std::vector<unsigned char>& nodePubKey)
{
    // TODO: Implement receipt signature verification
    return true;
}

bool PaymentChannelManager::updateChannelState(const ChannelInfo& info)
{
    // TODO: Update database
    return true;
}

bool PaymentChannelManager::loadChannel(const std::string& channelId, ChannelInfo& info)
{
    // TODO: Load from database
    return false;
}

bool PaymentChannelManager::saveChannel(const ChannelInfo& info)
{
    // TODO: Save to database
    return true;
}

} // namespace xrouter
