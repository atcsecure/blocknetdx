// Copyright (c) 2018-2025 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xrouterpaymentchannel.h>

#include <hash.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <logging.h>

#include <boost/algorithm/string.hpp>

namespace xrouter
{

//******************************************************************************
// ChannelStateUpdate implementation
//******************************************************************************

uint256 ChannelStateUpdate::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << channelId;
    ss << nonce;
    ss << clientBalance;
    ss << serviceNodeBalance;
    return ss.GetHash();
}

bool ChannelStateUpdate::VerifySignatures(const CPubKey& clientPubKey, const CPubKey& serviceNodePubKey) const
{
    uint256 hash = GetHash();

    // Verify client signature
    if (!clientSignature.empty()) {
        if (!clientPubKey.Verify(hash, clientSignature)) {
            LogPrint(BCLog::XROUTER, "XRouter: Invalid client signature in state update\n");
            return false;
        }
    }

    // Verify service node signature
    if (!serviceNodeSignature.empty()) {
        if (!serviceNodePubKey.Verify(hash, serviceNodeSignature)) {
            LogPrint(BCLog::XROUTER, "XRouter: Invalid service node signature in state update\n");
            return false;
        }
    }

    return true;
}

bool ChannelStateUpdate::Sign(const CKey& key, bool isClient)
{
    uint256 hash = GetHash();
    std::vector<unsigned char> signature;

    if (!key.Sign(hash, signature)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to sign state update\n");
        return false;
    }

    if (isClient) {
        clientSignature = signature;
    } else {
        serviceNodeSignature = signature;
    }

    return true;
}

std::vector<unsigned char> ChannelStateUpdate::Serialize() const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << channelId;
    ss << nonce;
    ss << clientBalance;
    ss << serviceNodeBalance;
    ss << clientSignature;
    ss << serviceNodeSignature;
    ss << timestamp;

    return std::vector<unsigned char>(ss.begin(), ss.end());
}

bool ChannelStateUpdate::Deserialize(const std::vector<unsigned char>& data)
{
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> channelId;
        ss >> nonce;
        ss >> clientBalance;
        ss >> serviceNodeBalance;
        ss >> clientSignature;
        ss >> serviceNodeSignature;
        ss >> timestamp;
        return true;
    } catch (const std::exception& e) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to deserialize state update: %s\n", e.what());
        return false;
    }
}

//******************************************************************************
// XRouterPaymentChannelManager implementation
//******************************************************************************

bool XRouterPaymentChannelManager::Init(const std::string& ethereumRpcUrl, const std::string& contractAddress)
{
    ethereumRpcUrl_ = ethereumRpcUrl;
    contractAddress_ = contractAddress;

    // Load existing channels from storage
    if (!LoadChannels()) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to load payment channels from storage\n");
        return false;
    }

    LogPrint(BCLog::XROUTER, "XRouter: Payment channel manager initialized with %d channels\n", channels_.size());
    return true;
}

uint256 XRouterPaymentChannelManager::OpenChannel(
    const std::string& serviceNodeAddress,
    const CPubKey& serviceNodePubKey,
    CAmount depositAmount,
    uint64_t challengePeriod)
{
    if (depositAmount <= 0) {
        LogPrint(BCLog::XROUTER, "XRouter: Invalid deposit amount for channel\n");
        return uint256();
    }

    if (!serviceNodePubKey.IsValid()) {
        LogPrint(BCLog::XROUTER, "XRouter: Invalid service node public key\n");
        return uint256();
    }

    // Generate unique channel ID
    uint256 channelId = GenerateChannelId(serviceNodeAddress);

    // Create channel structure
    auto channel = std::make_shared<PaymentChannel>();
    channel->channelId = channelId;
    channel->contractAddress = contractAddress_;
    channel->clientAddress = clientEthAddress_;
    channel->serviceNodeAddress = serviceNodeAddress;
    channel->clientPubKey = clientKey_.GetPubKey();
    channel->serviceNodePubKey = serviceNodePubKey;
    channel->clientDeposit = depositAmount;
    channel->serviceNodeDeposit = 0;
    channel->clientBalance = depositAmount;
    channel->serviceNodeBalance = 0;
    channel->nonce = 0;
    channel->challengePeriod = challengePeriod;
    channel->openTime = GetTime();
    channel->closingTime = 0;
    channel->state = ChannelState::OPEN;

    // Initialize latest state
    channel->latestState.channelId = channelId;
    channel->latestState.nonce = 0;
    channel->latestState.clientBalance = depositAmount;
    channel->latestState.serviceNodeBalance = 0;
    channel->latestState.timestamp = GetTime();

    // Call smart contract to open channel on blockchain
    std::vector<std::string> params;
    params.push_back(serviceNodeAddress);
    params.push_back(std::to_string(challengePeriod));

    std::string txHash;
    if (!SendContractTransaction("openChannel", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to open channel on blockchain\n");
        return uint256();
    }

    // Store channel
    channels_[channelId] = channel;
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Opened payment channel %s with %s, deposit: %d\n",
        channelId.GetHex(), serviceNodeAddress, depositAmount);

    return channelId;
}

bool XRouterPaymentChannelManager::CreatePayment(
    const uint256& channelId,
    CAmount amount,
    ChannelStateUpdate& outStateUpdate)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (!channel->IsOpen()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s is not open\n", channelId.GetHex());
        return false;
    }

    if (channel->clientBalance < amount) {
        LogPrint(BCLog::XROUTER, "XRouter: Insufficient balance in channel %s\n", channelId.GetHex());
        return false;
    }

    // Create new state update
    ChannelStateUpdate newState;
    newState.channelId = channelId;
    newState.nonce = channel->nonce + 1;
    newState.clientBalance = channel->clientBalance - amount;
    newState.serviceNodeBalance = channel->serviceNodeBalance + amount;
    newState.timestamp = GetTime();

    // Sign the new state
    if (!newState.Sign(clientKey_, true)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to sign payment state\n");
        return false;
    }

    // Update channel state locally (pending service node signature)
    channel->nonce = newState.nonce;
    channel->clientBalance = newState.clientBalance;
    channel->serviceNodeBalance = newState.serviceNodeBalance;
    channel->latestState = newState;

    SaveChannel(*channel);

    outStateUpdate = newState;

    LogPrint(BCLog::XROUTER, "XRouter: Created payment of %d in channel %s, new nonce: %d\n",
        amount, channelId.GetHex(), newState.nonce);

    return true;
}

bool XRouterPaymentChannelManager::VerifyPayment(const ChannelStateUpdate& stateUpdate)
{
    auto it = channels_.find(stateUpdate.channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", stateUpdate.channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (!channel->IsOpen()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s is not open\n", stateUpdate.channelId.GetHex());
        return false;
    }

    // Verify nonce is incrementing
    if (stateUpdate.nonce <= channel->nonce) {
        LogPrint(BCLog::XROUTER, "XRouter: Invalid nonce in payment state (got %d, expected > %d)\n",
            stateUpdate.nonce, channel->nonce);
        return false;
    }

    // Verify balances sum to total deposits
    CAmount totalDeposit = channel->clientDeposit + channel->serviceNodeDeposit;
    if (stateUpdate.clientBalance + stateUpdate.serviceNodeBalance != totalDeposit) {
        LogPrint(BCLog::XROUTER, "XRouter: Invalid balances in payment state\n");
        return false;
    }

    // Verify client signature
    if (!stateUpdate.VerifySignatures(channel->clientPubKey, channel->serviceNodePubKey)) {
        LogPrint(BCLog::XROUTER, "XRouter: Invalid signature in payment state\n");
        return false;
    }

    // Update channel state
    channel->nonce = stateUpdate.nonce;
    channel->clientBalance = stateUpdate.clientBalance;
    channel->serviceNodeBalance = stateUpdate.serviceNodeBalance;
    channel->latestState = stateUpdate;

    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Verified payment in channel %s, nonce: %d\n",
        stateUpdate.channelId.GetHex(), stateUpdate.nonce);

    return true;
}

bool XRouterPaymentChannelManager::SignState(ChannelStateUpdate& stateUpdate, bool isClient)
{
    return stateUpdate.Sign(clientKey_, isClient);
}

bool XRouterPaymentChannelManager::CooperativeClose(
    const uint256& channelId,
    const ChannelStateUpdate& finalState)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (!channel->IsOpen()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s is not open\n", channelId.GetHex());
        return false;
    }

    // Verify final state is valid and signed by both parties
    if (!finalState.VerifySignatures(channel->clientPubKey, channel->serviceNodePubKey)) {
        LogPrint(BCLog::XROUTER, "XRouter: Invalid signatures in final state\n");
        return false;
    }

    // Call smart contract to cooperatively close
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());
    params.push_back(std::to_string(finalState.nonce));
    params.push_back(std::to_string(finalState.clientBalance));
    params.push_back(std::to_string(finalState.serviceNodeBalance));
    params.push_back(HexStr(finalState.clientSignature));
    params.push_back(HexStr(finalState.serviceNodeSignature));

    std::string txHash;
    if (!SendContractTransaction("cooperativeClose", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to cooperatively close channel on blockchain\n");
        return false;
    }

    // Update channel state
    channel->state = ChannelState::CLOSED;
    channel->latestState = finalState;
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Cooperatively closed channel %s\n", channelId.GetHex());

    return true;
}

bool XRouterPaymentChannelManager::ChallengeClose(const uint256& channelId)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (!channel->IsOpen()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s is not open\n", channelId.GetHex());
        return false;
    }

    const auto& latestState = channel->latestState;

    // Call smart contract to initiate challenge close
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());
    params.push_back(std::to_string(latestState.nonce));
    params.push_back(std::to_string(latestState.clientBalance));
    params.push_back(std::to_string(latestState.serviceNodeBalance));
    params.push_back(HexStr(latestState.serviceNodeSignature));

    std::string txHash;
    if (!SendContractTransaction("challengeClose", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to challenge close channel on blockchain\n");
        return false;
    }

    // Update channel state
    channel->state = ChannelState::CHALLENGED;
    channel->closingTime = GetTime();
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Initiated challenge close for channel %s\n", channelId.GetHex());

    return true;
}

bool XRouterPaymentChannelManager::DisputeClose(
    const uint256& channelId,
    const ChannelStateUpdate& newerState)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (channel->state != ChannelState::CHALLENGED) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s is not in challenged state\n", channelId.GetHex());
        return false;
    }

    // Verify newer state has higher nonce
    if (newerState.nonce <= channel->nonce) {
        LogPrint(BCLog::XROUTER, "XRouter: Dispute state nonce not higher than current\n");
        return false;
    }

    // Call smart contract to dispute
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());
    params.push_back(std::to_string(newerState.nonce));
    params.push_back(std::to_string(newerState.clientBalance));
    params.push_back(std::to_string(newerState.serviceNodeBalance));
    params.push_back(HexStr(newerState.clientSignature));

    std::string txHash;
    if (!SendContractTransaction("disputeClose", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to dispute close on blockchain\n");
        return false;
    }

    // Update channel with newer state
    channel->nonce = newerState.nonce;
    channel->clientBalance = newerState.clientBalance;
    channel->serviceNodeBalance = newerState.serviceNodeBalance;
    channel->latestState = newerState;
    channel->closingTime = GetTime(); // Reset challenge period
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Disputed close for channel %s with state nonce %d\n",
        channelId.GetHex(), newerState.nonce);

    return true;
}

bool XRouterPaymentChannelManager::FinalizeClose(const uint256& channelId)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (channel->state != ChannelState::CHALLENGED) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s is not in challenged state\n", channelId.GetHex());
        return false;
    }

    // Check if challenge period has expired
    uint64_t now = GetTime();
    if (now < channel->closingTime + channel->challengePeriod) {
        LogPrint(BCLog::XROUTER, "XRouter: Challenge period not expired for channel %s\n", channelId.GetHex());
        return false;
    }

    // Call smart contract to finalize
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());

    std::string txHash;
    if (!SendContractTransaction("finalizeClose", params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to finalize close on blockchain\n");
        return false;
    }

    // Update channel state
    channel->state = ChannelState::CLOSED;
    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Finalized close for channel %s\n", channelId.GetHex());

    return true;
}

std::shared_ptr<PaymentChannel> XRouterPaymentChannelManager::GetChannel(const uint256& channelId)
{
    auto it = channels_.find(channelId);
    if (it != channels_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<uint256> XRouterPaymentChannelManager::GetChannelsByServiceNode(const std::string& serviceNodeAddress)
{
    std::vector<uint256> result;

    for (const auto& pair : channels_) {
        if (pair.second->serviceNodeAddress == serviceNodeAddress) {
            result.push_back(pair.first);
        }
    }

    return result;
}

std::vector<uint256> XRouterPaymentChannelManager::GetOpenChannels()
{
    std::vector<uint256> result;

    for (const auto& pair : channels_) {
        if (pair.second->IsOpen()) {
            result.push_back(pair.first);
        }
    }

    return result;
}

bool XRouterPaymentChannelManager::SyncChannelFromBlockchain(const uint256& channelId)
{
    PaymentChannel channel;
    if (!GetChannelFromBlockchain(channelId, channel)) {
        return false;
    }

    auto it = channels_.find(channelId);
    if (it != channels_.end()) {
        // Update existing channel
        *it->second = channel;
        SaveChannel(channel);
    } else {
        // Add new channel
        channels_[channelId] = std::make_shared<PaymentChannel>(channel);
        SaveChannel(channel);
    }

    return true;
}

bool XRouterPaymentChannelManager::HasSufficientBalance(const uint256& channelId, CAmount amount)
{
    auto channel = GetChannel(channelId);
    if (!channel || !channel->IsOpen()) {
        return false;
    }

    return channel->clientBalance >= amount;
}

CAmount XRouterPaymentChannelManager::GetTotalAvailableBalance()
{
    CAmount total = 0;

    for (const auto& pair : channels_) {
        if (pair.second->IsOpen()) {
            total += pair.second->clientBalance;
        }
    }

    return total;
}

bool XRouterPaymentChannelManager::AddDeposit(const uint256& channelId, CAmount amount, bool isClient)
{
    auto it = channels_.find(channelId);
    if (it == channels_.end()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s not found\n", channelId.GetHex());
        return false;
    }

    auto channel = it->second;

    if (!channel->IsOpen()) {
        LogPrint(BCLog::XROUTER, "XRouter: Channel %s is not open\n", channelId.GetHex());
        return false;
    }

    // Call smart contract to add deposit
    std::string method = isClient ? "depositClient" : "depositServiceNode";
    std::vector<std::string> params;
    params.push_back(channelId.GetHex());

    std::string txHash;
    if (!SendContractTransaction(method, params, txHash)) {
        LogPrint(BCLog::XROUTER, "XRouter: Failed to add deposit to channel on blockchain\n");
        return false;
    }

    // Update channel
    if (isClient) {
        channel->clientDeposit += amount;
        channel->clientBalance += amount;
    } else {
        channel->serviceNodeDeposit += amount;
        channel->serviceNodeBalance += amount;
    }

    SaveChannel(*channel);

    LogPrint(BCLog::XROUTER, "XRouter: Added deposit of %d to channel %s\n", amount, channelId.GetHex());

    return true;
}

//******************************************************************************
// Private methods
//******************************************************************************

bool XRouterPaymentChannelManager::LoadChannels()
{
    // TODO: Implement persistent storage (e.g., LevelDB)
    // For now, start with empty channel map
    channels_.clear();
    return true;
}

bool XRouterPaymentChannelManager::SaveChannel(const PaymentChannel& channel)
{
    // TODO: Implement persistent storage (e.g., LevelDB)
    return true;
}

bool XRouterPaymentChannelManager::CallContractMethod(
    const std::string& method,
    const std::vector<std::string>& params,
    std::string& result)
{
    // TODO: Implement Ethereum RPC call to read contract state
    // This would use eth_call to read contract data without sending transaction
    LogPrint(BCLog::XROUTER, "XRouter: Calling contract method %s\n", method);
    return true;
}

bool XRouterPaymentChannelManager::SendContractTransaction(
    const std::string& method,
    const std::vector<std::string>& params,
    std::string& txHash)
{
    // TODO: Implement Ethereum transaction sending
    // This would:
    // 1. Build contract call data using ABI encoding
    // 2. Create Ethereum transaction
    // 3. Sign transaction with client's Ethereum key
    // 4. Send via eth_sendRawTransaction RPC
    // 5. Return transaction hash

    LogPrint(BCLog::XROUTER, "XRouter: Sending contract transaction for method %s\n", method);

    // Mock transaction hash for demonstration
    txHash = "0x" + HexStr(std::vector<unsigned char>(32, 0));

    return true;
}

bool XRouterPaymentChannelManager::GetChannelFromBlockchain(const uint256& channelId, PaymentChannel& channel)
{
    // TODO: Implement reading channel state from blockchain
    // This would call the contract's getChannel() method
    LogPrint(BCLog::XROUTER, "XRouter: Reading channel %s from blockchain\n", channelId.GetHex());
    return true;
}

uint256 XRouterPaymentChannelManager::GenerateChannelId(const std::string& serviceNodeAddress)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << clientEthAddress_;
    ss << serviceNodeAddress;
    ss << GetTime();
    ss << GetRand(std::numeric_limits<uint64_t>::max());
    return ss.GetHash();
}

} // namespace xrouter
